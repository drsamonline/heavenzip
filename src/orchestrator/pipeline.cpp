// pipeline.cpp — run the plan, then write the thing that undoes it.
#include "heavenzip/pipeline.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "heavenzip/runner.hpp"

namespace fs = std::filesystem;

namespace heavenzip {
namespace {

// --- a JSON writer for one shape, and a reader for the same shape -----------
//
// This is not a general JSON library and does not pretend to be: it writes the
// manifest this file defines and reads back what it wrote. Anything else — a
// hand-edited manifest with a different shape — fails with a message rather than
// being guessed at.

std::string escape(const std::string& value) {
  std::string out;
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
          out += buffer;
        } else {
          out += c;
        }
    }
  }
  return out;
}

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<ValuePtr> items;
  std::vector<std::pair<std::string, ValuePtr>> members;

  const ValuePtr* find(const std::string& key) const {
    for (const auto& [name, value] : members) {
      if (name == key) return &value;
    }
    return nullptr;
  }
};

class Parser {
 public:
  explicit Parser(std::string source) : source_(std::move(source)) {}

  ValuePtr parse() {
    ValuePtr value = parse_value();
    skip_space();
    if (position_ != source_.size()) throw std::runtime_error("trailing bytes after JSON value");
    return value;
  }

 private:
  void skip_space() {
    while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])))
      ++position_;
  }

  char peek() {
    skip_space();
    if (position_ >= source_.size()) throw std::runtime_error("unexpected end of JSON");
    return source_[position_];
  }

  void expect(char wanted) {
    if (peek() != wanted) {
      throw std::runtime_error(std::string("expected `") + wanted + "` at offset " +
                               std::to_string(position_));
    }
    ++position_;
  }

  ValuePtr parse_value() {
    const char c = peek();
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return parse_string();
    if (c == 't' || c == 'f') return parse_bool();
    if (c == 'n') return parse_null();
    return parse_number();
  }

  ValuePtr parse_object() {
    auto value = std::make_shared<Value>();
    value->kind = Value::Kind::Object;
    expect('{');
    if (peek() == '}') { ++position_; return value; }
    while (true) {
      ValuePtr key = parse_string();
      expect(':');
      value->members.emplace_back(key->text, parse_value());
      if (peek() == ',') { ++position_; continue; }
      expect('}');
      break;
    }
    return value;
  }

  ValuePtr parse_array() {
    auto value = std::make_shared<Value>();
    value->kind = Value::Kind::Array;
    expect('[');
    if (peek() == ']') { ++position_; return value; }
    while (true) {
      value->items.push_back(parse_value());
      if (peek() == ',') { ++position_; continue; }
      expect(']');
      break;
    }
    return value;
  }

  ValuePtr parse_string() {
    expect('"');
    auto value = std::make_shared<Value>();
    value->kind = Value::Kind::String;
    while (true) {
      if (position_ >= source_.size()) throw std::runtime_error("unterminated JSON string");
      const char c = source_[position_++];
      if (c == '"') break;
      if (c != '\\') { value->text += c; continue; }
      if (position_ >= source_.size()) throw std::runtime_error("unterminated escape");
      const char esc = source_[position_++];
      switch (esc) {
        case 'n': value->text += '\n'; break;
        case 'r': value->text += '\r'; break;
        case 't': value->text += '\t'; break;
        case 'u': {
          if (position_ + 4 > source_.size()) throw std::runtime_error("short \\u escape");
          const std::string hex = source_.substr(position_, 4);
          position_ += 4;
          value->text += static_cast<char>(std::stoul(hex, nullptr, 16) & 0xFF);
          break;
        }
        default: value->text += esc;
      }
    }
    return value;
  }

  ValuePtr parse_bool() {
    auto value = std::make_shared<Value>();
    value->kind = Value::Kind::Bool;
    if (source_.compare(position_, 4, "true") == 0) { position_ += 4; value->boolean = true; }
    else if (source_.compare(position_, 5, "false") == 0) { position_ += 5; value->boolean = false; }
    else throw std::runtime_error("bad boolean");
    return value;
  }

  ValuePtr parse_null() {
    if (source_.compare(position_, 4, "null") != 0) throw std::runtime_error("bad null");
    position_ += 4;
    return std::make_shared<Value>();
  }

  ValuePtr parse_number() {
    const std::size_t start = position_;
    while (position_ < source_.size() &&
           (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
            source_[position_] == '-' || source_[position_] == '+' || source_[position_] == '.' ||
            source_[position_] == 'e' || source_[position_] == 'E'))
      ++position_;
    if (start == position_) throw std::runtime_error("expected a value");
    auto value = std::make_shared<Value>();
    value->kind = Value::Kind::Number;
    value->number = std::stod(source_.substr(start, position_ - start));
    return value;
  }

  std::string source_;
  std::size_t position_ = 0;
};

std::string number(double value) {
  std::ostringstream out;
  out.precision(3);
  out << std::fixed << value;
  return out.str();
}

std::string array_of(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out += ", ";
    out += "\"" + escape(values[i]) + "\"";
  }
  return out + "]";
}

}  // namespace

RunOutcome run_plan(const Plan& plan) {
  RunOutcome outcome;
  const fs::path log_dir = plan.output.parent_path();

  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    const StepCommand& command = plan.steps[i];
    StepOutcome step;
    step.step = command.step;

    // A tool that writes its result to stdout has that stream pointed at the file it
    // is meant to produce. The log would otherwise swallow the payload.
    const fs::path step_output = command.stdout_to_output
        ? (i + 1 == plan.steps.size() ? plan.output : fs::path(plan.output.string() + "." + command.step + ".tmp"))
        : fs::path();
    const fs::path out_log = command.stdout_to_output
        ? step_output
        : log_dir / (plan.output.filename().string() + "." + command.step + ".stdout");
    const fs::path err_log = log_dir / (plan.output.filename().string() + "." + command.step + ".stderr");

    RunResult result;
    try {
      result = run_capture(command.argv, out_log, err_log);
    } catch (const std::exception& error) {
      outcome.ok = false;
      outcome.failed_step = command.step;
      outcome.exit_code = 127;
      outcome.detail = error.what();
      step.exit_code = 127;
      step.stdout_log = out_log;
      step.stderr_log = err_log;
      outcome.steps.push_back(step);
      return outcome;
    }

    step.exit_code = result.exit_code;
    step.seconds = result.seconds;
    step.stdout_log = out_log;
    step.stderr_log = err_log;
    outcome.steps.push_back(step);

    if (result.exit_code != 0) {
      outcome.ok = false;
      outcome.failed_step = command.step;
      outcome.exit_code = result.exit_code;
      outcome.detail = result.stderr_tail.empty() ? result.stdout_tail : result.stderr_tail;
      return outcome;
    }
  }
  outcome.ok = true;
  return outcome;
}

void write_manifest(const Plan& plan, const RunOutcome& outcome) {
  std::ostringstream json;
  double total = 0;
  for (const StepOutcome& step : outcome.steps) total += step.seconds;

  json << "{\n"
       << "  \"heavenzip\": \"" << HEAVENZIP_VERSION << "\",\n"
       << "  \"profile\": \"" << escape(plan.profile) << "\",\n"
       << "  \"detect\": \"" << escape(plan.detect) << "\",\n"
       << "  \"found\": \"" << escape(to_string(plan.found)) << "\",\n"
       << "  \"input\": \"" << escape(plan.input.string()) << "\",\n"
       << "  \"output\": \"" << escape(plan.output.string()) << "\",\n"
       << "  \"ok\": " << (outcome.ok ? "true" : "false") << ",\n"
       << "  \"failed_step\": \"" << escape(outcome.failed_step) << "\",\n"
       << "  \"seconds\": " << number(total) << ",\n"
       << "  \"steps\": [\n";

  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    const StepCommand& command = plan.steps[i];
    const StepOutcome* step = i < outcome.steps.size() ? &outcome.steps[i] : nullptr;
    json << "    {\"step\": \"" << escape(command.step) << "\", "
         << "\"argv\": " << array_of(command.argv) << ", "
         << "\"reverse\": " << array_of(command.reverse) << ", "
         << "\"reverse_out_prefix\": \"" << command.reverse_out_prefix << "\", "
         << "\"reverse_out_index\": "
         // -1 means "the reverse writes to stdout, there is no argument to replace".
         // Written as a negative rather than SIZE_MAX because JSON numbers are read
         // as doubles here, and 2^64-1 does not survive that trip.
         << (command.reverse_out_index == static_cast<std::size_t>(-1)
                 ? -1
                 : static_cast<long long>(command.reverse_out_index))
         << ", "
         << "\"stdout_to_output\": " << (command.stdout_to_output ? "true" : "false") << ", "
         << "\"produced\": \"" << escape(command.produced) << "\", "
         << "\"exit_code\": " << (step ? step->exit_code : -1) << ", "
         << "\"seconds\": " << number(step ? step->seconds : 0.0) << "}";
    if (i + 1 < plan.steps.size()) json << ",";
    json << "\n";
  }
  json << "  ],\n";
  // What the profile declared optional and the machine did not have. The reverse plan
  // is built from `steps`, which is what ran, so this is a record and not an input:
  // it says the archive was made without SREP, which is the difference between two
  // archives of different sizes and the same name.
  json << "  \"skipped\": [";
  for (std::size_t i = 0; i < plan.notices.size(); ++i) {
    if (i > 0) json << ", ";
    json << "\"" << escape(plan.notices[i]) << "\"";
  }
  json << "]\n}\n";

  std::ofstream out(plan.manifest);
  if (!out) throw std::runtime_error(plan.manifest.string() + ": cannot be written");
  out << json.str();
}

std::vector<ReverseStep> read_reverse_plan(const fs::path& manifest) {
  std::ifstream in(manifest);
  if (!in) throw std::runtime_error(manifest.string() + ": cannot be opened");
  std::ostringstream buffer;
  buffer << in.rdbuf();

  const ValuePtr root = Parser(buffer.str()).parse();
  const ValuePtr* steps = root->find("steps");
  if (steps == nullptr || (*steps)->kind != Value::Kind::Array) {
    throw std::runtime_error(manifest.string() + ": no `steps` array");
  }

  std::vector<ReverseStep> reverse;
  for (const ValuePtr& entry : (*steps)->items) {
    const ValuePtr* argv = entry->find("argv");
    const ValuePtr* name = entry->find("step");
    if (argv == nullptr || (*argv)->kind != Value::Kind::Array) {
      throw std::runtime_error(manifest.string() + ": a step has no `argv`");
    }
    ReverseStep step;
    step.step = name != nullptr ? (*name)->text : "step";
    const ValuePtr* to_stdout = entry->find("stdout_to_output");
    if (to_stdout != nullptr && to_stdout->get()->kind == Value::Kind::Bool) {
      step.stdout_to_output = to_stdout->get()->boolean;
    }
    const ValuePtr* out_prefix = entry->find("reverse_out_prefix");
    if (out_prefix != nullptr && out_prefix->get()->kind == Value::Kind::String) {
      step.out_prefix = out_prefix->get()->text;
    }
    const ValuePtr* produced = entry->find("produced");
    if (produced != nullptr && produced->get()->kind == Value::Kind::String) {
      step.produced = produced->get()->text;
    }
    const ValuePtr* out_index = entry->find("reverse_out_index");
    if (out_index != nullptr && out_index->get()->kind == Value::Kind::Number) {
      const double recorded = out_index->get()->number;
      step.out_index = recorded < 0 ? static_cast<std::size_t>(-1)
                                    : static_cast<std::size_t>(recorded);
    }
    // Prefer the recorded reverse command; fall back to the documented convention
    // only when an older manifest has no `reverse` field.
    const ValuePtr* recorded = entry->find("reverse");
    if (recorded != nullptr && (*recorded)->kind == Value::Kind::Array) {
      for (const ValuePtr& token : (*recorded)->items) step.argv.push_back(token->text);
    } else {
      const auto& tokens = (*argv)->items;
      if (tokens.size() < 2) throw std::runtime_error(manifest.string() + ": a step argv is too short");
      step.argv = {tokens.front()->text, "d", tokens.back()->text, tokens[tokens.size() - 2]->text};
    }
    reverse.push_back(step);
  }
  std::reverse(reverse.begin(), reverse.end());
  return reverse;
}

RunOutcome run_reverse(const fs::path& manifest, const fs::path& out) {
  const std::vector<ReverseStep> steps = read_reverse_plan(manifest);
  RunOutcome outcome;
  fs::path log_base = manifest.filename();

  for (std::size_t i = 0; i < steps.size(); ++i) {
    ReverseStep command = steps[i];
    // The last reverse step would write back over the original file; send it to
    // `out` instead, so decompressing never destroys the input it came from.
    if (i + 1 == steps.size() && !out.empty() && command.out_index < command.argv.size()) {
      command.argv[command.out_index] = command.out_prefix + out.string();
    }
    // Clear the file this step is about to produce. The forward run keeps its
    // intermediates, and ziptastic refuses to overwrite ("exists (use -f)"), so
    // reversing any pipeline longer than one step failed on its first step while
    // the bytes were perfectly recoverable. Decompression owns its output; the
    // forward direction does not, which is why only this side clears anything.
    // Where this reverse step writes. Three cases, and the third was the bug:
    //
    //   * the command names the file (`-oOUT`, or OUT last) — substitute, as before;
    //   * a stdout-writing step that is the last one — stdout is pointed at `out`;
    //   * a stdout-writing step in the *middle* of the chain — stdout must be pointed
    //     at the file the next reverse step reads, which is what this step produced
    //     going forward. Deriving it as "nowhere" (the old behaviour) sent the payload
    //     to the caller's terminal, left no file for the next step, and — worse — let
    //     the verify pass read the forward run's leftover intermediate instead of the
    //     archive, so `verified` was reported for a chain whose reverse never ran.
    const fs::path reverse_output = command.out_index < command.argv.size()
        ? fs::path(command.argv[command.out_index].substr(command.out_prefix.size()))
        : (command.stdout_to_output
               // The *next* reverse step's `produced`, not this one's: reverse step i
               // undoes forward step i, so what it writes is what the step before it in
               // the forward chain produced. Using this step's own `produced` pointed
               // stdout at the archive for the first reverse step and truncated the
               // file it was about to read.
               ? (i + 1 == steps.size() || out.empty() ? out : fs::path(steps[i + 1].produced))
               : fs::path());
    {
      std::error_code ec;
      if (!reverse_output.empty()) fs::remove(reverse_output, ec);
    }

    StepOutcome step;
    step.step = command.step;
    const fs::path out_log = command.stdout_to_output
        ? reverse_output
        : manifest.parent_path() / (log_base.string() + "." + command.step + ".reverse.stdout");
    const fs::path err_log = manifest.parent_path() / (log_base.string() + "." + command.step + ".reverse.stderr");
    const RunResult result = run_capture(command.argv, out_log, err_log);
    step.exit_code = result.exit_code;
    step.seconds = result.seconds;
    step.stdout_log = out_log;
    step.stderr_log = err_log;
    outcome.steps.push_back(step);
    if (result.exit_code != 0) {
      outcome.ok = false;
      outcome.failed_step = command.step;
      outcome.exit_code = result.exit_code;
      outcome.detail = result.stderr_tail.empty() ? result.stdout_tail : result.stderr_tail;
      return outcome;
    }
  }
  outcome.ok = true;
  return outcome;
}

}  // namespace heavenzip

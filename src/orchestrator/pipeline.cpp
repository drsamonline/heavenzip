// pipeline.cpp — run the plan, then write the thing that undoes it.
#include "heavenzip/pipeline.hpp"

#include <cstdio>
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

// The reverse of `<bin> <args...> <consumed> <produced>` is `<bin> d <produced>
// <consumed>`. Recorded per step so a reader never has to infer it.
std::vector<std::string> reverse_of(const StepCommand& command) {
  std::vector<std::string> reverse;
  reverse.push_back(command.argv.front());          // program
  reverse.push_back("d");                          // verb: decompress
  reverse.push_back(command.argv.back());          // what the step produced
  reverse.push_back(command.argv[command.argv.size() - 2]);  // what it consumed
  return reverse;
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

    const fs::path out_log = log_dir / (plan.output.filename().string() + "." + command.step + ".stdout");
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
         << "\"reverse\": " << array_of(reverse_of(command)) << ", "
         << "\"exit_code\": " << (step ? step->exit_code : -1) << ", "
         << "\"seconds\": " << number(step ? step->seconds : 0.0) << "}";
    if (i + 1 < plan.steps.size()) json << ",";
    json << "\n";
  }
  json << "  ]\n}\n";

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
    if (i + 1 == steps.size() && !out.empty()) {
      command.argv.back() = out.string();
    }
    StepOutcome step;
    step.step = command.step;
    const fs::path out_log = manifest.parent_path() / (log_base.string() + "." + command.step + ".reverse.stdout");
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

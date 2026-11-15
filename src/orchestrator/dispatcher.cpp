// dispatcher.cpp — the plan for a pipeline. Phase 3 adds the spawn.
#include "heavenzip/dispatcher.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace heavenzip {
namespace {

std::string quote(const std::string& token) {
  const bool needs_quotes = token.empty() || token.find_first_of(" \t\"'") != std::string::npos;
  if (!needs_quotes) return token;
  std::string out = "\"";
  for (const char c : token) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

}  // namespace

std::string StepCommand::shell_line() const {
  std::string line;
  for (const std::string& token : argv) {
    if (!line.empty()) line += ' ';
    line += quote(token);
  }
  return line;
}

namespace {

// The shipped profiles name Windows binaries (`bin/srep.exe`) because that is
// where the shelf came from, while a Linux release of the engine is
// `ziptastic-1.4.2-linux-x86_64`. Nothing resolved the difference, so a profile
// was correct on one platform and a refusal on the other.
//
// The candidate list is the name as written, then the name with the platform's
// own convention applied: `.exe` stripped on POSIX, `.exe` added on Windows. The
// configured name is always tried first, so an explicit path that exists is
// never second-guessed.
std::vector<std::string> suffix_candidates(const std::string& program) {
  std::vector<std::string> candidates{program};
#ifdef _WIN32
  const std::string exe = ".exe";
  if (program.size() < exe.size() ||
      program.compare(program.size() - exe.size(), exe.size(), exe) != 0) {
    candidates.push_back(program + exe);
  }
#else
  const std::string exe = ".exe";
  if (program.size() > exe.size() &&
      program.compare(program.size() - exe.size(), exe.size(), exe) == 0) {
    candidates.push_back(program.substr(0, program.size() - exe.size()));
  }
#endif
  return candidates;
}

bool exists_on_path(const std::string& program) {
  const char* path = std::getenv("PATH");
  if (path == nullptr) return false;
#ifdef _WIN32
  const char separator = ';';
#else
  const char separator = ':';
#endif
  std::stringstream stream(path);
  std::string directory;
  while (std::getline(stream, directory, separator)) {
    if (directory.empty()) continue;
    if (fs::exists(fs::path(directory) / program)) return true;
  }
  return false;
}

}  // namespace

std::string resolve_program(const std::string& program) {
  if (program.empty()) return {};
  const bool has_separator =
      program.find('/') != std::string::npos || program.find('\\') != std::string::npos;
  for (const std::string& candidate : suffix_candidates(program)) {
    if (has_separator) {
      if (fs::exists(candidate)) return candidate;
    } else if (exists_on_path(candidate)) {
      return candidate;
    }
  }
  return {};
}

bool program_available(const std::string& program) {
  return !resolve_program(program).empty();
}

Plan build_plan(const MasterConfig& master, const Profile& profile, const std::string& profile_name,
                const fs::path& input, const fs::path& output) {
  const Detection found = detect_file(input);

  if (!matches(profile.detect, found.value)) {
    std::ostringstream message;
    message << "profile `" << profile_name << "` does not apply: it detects " << profile.detect
            << ", and " << input.string() << " is " << to_string(found.value) << " (" << found.why
            << ")";
    throw std::runtime_error(message.str());
  }

  Plan plan;
  plan.profile = profile_name;
  plan.detect = profile.detect;
  plan.found = found.value;
  plan.input = input;
  plan.output = output;
  plan.manifest = input.string() + ".hzpipeline";

  // Step i reads what step i-1 wrote; the first reads the input, the last writes
  // the .hzpkg. The intermediates are named after the step so a partial run says
  // where it stopped.
  fs::path current = input;
  for (std::size_t i = 0; i < profile.steps.size(); ++i) {
    const Step& step = profile.steps[i];
    StepCommand command;
    command.step = step.name;
    command.program = step.bin.empty() ? master.ziptastic_bin : step.bin;

    // Spawn what resolves, not what was written: on POSIX a profile naming
    // `bin/srep.exe` runs `bin/srep`. The manifest records the configured name in
    // `program` and the argv it actually used, so the difference is visible.
    const std::string resolved = resolve_program(command.program);
    command.argv.push_back(resolved.empty() ? command.program : resolved);
    for (const std::string& token : split_args(step.args)) command.argv.push_back(token);
    command.argv.push_back(current.string());

    const bool last = (i + 1 == profile.steps.size());
    const fs::path next = last ? output : fs::path(output.string() + "." + step.name + ".tmp");
    // The step's own output argument: every compressor in the family writes beside
    // its input or takes an explicit name, so it is passed explicitly.
    command.argv.push_back(next.string());

    // The reverse of a step: the same binary, its own reverse arguments when the
    // profile gives them, then what it produced and what it consumed. A tool whose
    // decompress verb is not `d` says so with `reverse_args`, and the manifest
    // records the result rather than a convention the reader has to guess.
    // The reverse command is spawned too, so it needs the resolved path for the
    // same reason the forward one does: a profile naming `bin/ziptastic.exe`
    // must decompress with `bin/ziptastic`. Leaving the configured name here
    // made every decompress fail with exec 127 on a machine where the forward
    // step had just worked.
    command.reverse.push_back(resolved.empty() ? command.program : resolved);
    if (!step.reverse_args.empty()) {
      for (const std::string& token : split_args(step.reverse_args)) command.reverse.push_back(token);
    } else {
      command.reverse.push_back("d");
    }
    command.reverse.push_back(next.string());   // produced
    command.reverse.push_back(current.string()); // consumed

    plan.steps.push_back(std::move(command));
    current = next;
  }
  return plan;
}

}  // namespace heavenzip

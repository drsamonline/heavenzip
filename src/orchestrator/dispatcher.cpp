// dispatcher.cpp — the plan for a pipeline. Phase 3 adds the spawn.
#include "heavenzip/dispatcher.hpp"

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

    command.argv.push_back(command.program);
    for (const std::string& token : split_args(step.args)) command.argv.push_back(token);
    command.argv.push_back(current.string());

    const bool last = (i + 1 == profile.steps.size());
    const fs::path next = last ? output : fs::path(output.string() + "." + step.name + ".tmp");
    // The step's own output argument: every compressor in the family writes beside
    // its input or takes an explicit name, so it is passed explicitly.
    command.argv.push_back(next.string());

    plan.steps.push_back(std::move(command));
    current = next;
  }
  return plan;
}

}  // namespace heavenzip

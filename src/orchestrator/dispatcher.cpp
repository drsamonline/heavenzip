// dispatcher.cpp — the plan for a pipeline. Phase 3 adds the spawn.
#include <algorithm>

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
  // Sentinel for "the reverse writes to stdout, so there is no argument to replace".
  constexpr std::size_t kNoReverseOutIndex = static_cast<std::size_t>(-1);

  // Which steps will actually run. A step whose tool is missing and which the profile
  // declares `optional` is dropped with a notice rather than taken as a refusal: the
  // rest of the chain still compresses, and the alternative is a profile that can
  // never run for anyone. SREP is the case that forced this — "all rights reserved,
  // not redistributable in any form" — and `BUNDLING.md` already specified the
  // behaviour ("should degrade to `ziptastic:lzma` with a one-line notice, not fail").
  //
  // Dropping happens here, not in the caller, so every caller agrees: `compress`,
  // `bench` and `autotune` all see the same steps, and the manifest can only ever
  // record a chain that ran.
  std::vector<const Step*> kept;
  for (const Step& step : profile.steps) {
    const std::string program = step.bin.empty() ? master.ziptastic_bin : step.bin;
    if (!step.optional || program_available(program)) {
      kept.push_back(&step);
      continue;
    }
    plan.notices.push_back("step `" + step.name + "` dropped: " + program +
                           " is not present, and the profile declares it optional");
  }
  if (kept.empty()) {
    std::ostringstream message;
    message << "profile `" << profile_name
            << "` cannot run: every step in it needs a tool that is not present on this machine";
    for (const std::string& notice : plan.notices) message << "\n  " << notice;
    throw std::runtime_error(message.str());
  }

  // Step i reads what step i-1 wrote; the first reads the input, the last writes
  // the .hzpkg. The intermediates are named after the step so a partial run says
  // where it stopped.
  fs::path current = input;
  for (std::size_t i = 0; i < kept.size(); ++i) {
    const Step& step = *kept[i];
    StepCommand command;
    command.step = step.name;
    command.program = step.bin.empty() ? master.ziptastic_bin : step.bin;
    command.stdout_to_output = step.stdout_to_output;

    // Spawn what resolves, not what was written: on POSIX a profile naming
    // `bin/srep.exe` runs `bin/srep`. The manifest records the configured name in
    // `program` and the argv it actually used, so the difference is visible.
    const std::string resolved = resolve_program(command.program);
    command.argv.push_back(resolved.empty() ? command.program : resolved);

    const bool last = (i + 1 == kept.size());
    const fs::path next = last ? output : fs::path(output.string() + "." + step.name + ".tmp");
    command.produced = next.string();

    // A step's arguments may name `{in}` and `{out}` anywhere. Without them the
    // input and output are appended in that order, which is what ziptastic, srep
    // and paq8px expect — but not brotli (`-o OUT IN`), nor xz, nor zpaq. Those
    // tools could not be expressed at all, so the placeholder is what lets a
    // profile describe them instead of needing a wrapper script per tool.
    const std::vector<std::string> tokens = split_args(step.args);
    const bool placed = std::any_of(tokens.begin(), tokens.end(), [](const std::string& token) {
      return token.find("{in}") != std::string::npos || token.find("{out}") != std::string::npos;
    });
    for (const std::string& token : tokens) {
      std::string expanded = token;
      const auto replace_all = [&expanded](const std::string& what, const std::string& with) {
        for (auto at = expanded.find(what); at != std::string::npos;
             at = expanded.find(what, at + with.size())) {
          expanded.replace(at, what.size(), with);
        }
      };
      replace_all("{in}", current.string());
      replace_all("{out}", next.string());
      command.argv.push_back(expanded);
    }
    if (!placed) {
      command.argv.push_back(current.string());
      // A tool that writes to stdout takes no output argument: the runner points its
      // stdout at the file instead.
      if (!step.stdout_to_output) command.argv.push_back(next.string());
    }

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
    const std::vector<std::string> reverse_tokens = split_args(step.reverse_args);
    const bool reverse_placed =
        std::any_of(reverse_tokens.begin(), reverse_tokens.end(), [](const std::string& token) {
          return token.find("{in}") != std::string::npos || token.find("{out}") != std::string::npos;
        });
    if (step.reverse_args.empty()) {
      command.reverse.push_back("d");
    }
    // In reverse, `{in}` is what the step consumes (the forward output) and `{out}`
    // is what it restores (the forward input) — the opposite way round from the
    // forward direction. Getting this backwards made a brotli-shaped tool write its
    // restore over the file it was reading.
    for (const std::string& token : reverse_tokens) {
      std::string expanded = token;
      const auto replace_all = [&expanded](const std::string& what, const std::string& with) {
        for (auto at = expanded.find(what); at != std::string::npos;
             at = expanded.find(what, at + with.size())) {
          expanded.replace(at, what.size(), with);
        }
      };
      replace_all("{in}", next.string());
      replace_all("{out}", current.string());
      command.reverse.push_back(expanded);
    }
    if (!reverse_placed) {
      command.reverse.push_back(next.string());    // produced
      command.reverse.push_back(current.string()); // consumed
    }
    // Where the restore target sits, or "nowhere". The default used to be the last
    // argument, which is right for `tool d IN OUT` and wrong for a tool that writes
    // to stdout: `gzip -d -c {in}` has no output argument at all, and pointing the
    // substitution at its last argument made decompression read the file it was
    // meant to write.
    command.reverse_out_index = kNoReverseOutIndex;
    command.reverse_out_prefix.clear();
    // An exact token match wins. Only if there is none does an embedded match count,
    // and only when something precedes the path — `-oFILE`. Scanning for a substring
    // in one pass was wrong: `<input>.hzpkg` contains `<input>`, so the archive was
    // taken for the restore target and the last step wrote over the user's input.
    for (std::size_t k = 1; k < command.reverse.size(); ++k) {
      if (command.reverse[k] == current.string()) {
        command.reverse_out_index = k;
        break;
      }
    }
    if (command.reverse_out_index == kNoReverseOutIndex) {
      for (std::size_t k = 1; k < command.reverse.size(); ++k) {
        const std::size_t at = command.reverse[k].find(current.string());
        if (at != std::string::npos && at > 0) {
          command.reverse_out_index = k;
          command.reverse_out_prefix = command.reverse[k].substr(0, at);
          break;
        }
      }
    }

    // Where the restore target sits, or "nowhere". The default used to be the last
    // argument, which is right for `tool d IN OUT` and wrong for a tool that writes
    // to stdout: `gzip -d -c {in}` has no output argument at all, and pointing the
    // substitution at its last argument made decompression read the file it was
    // meant to write.
    plan.steps.push_back(std::move(command));
    current = next;
  }
  return plan;
}

}  // namespace heavenzip

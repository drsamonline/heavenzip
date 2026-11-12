// dispatcher.hpp — turn a profile into the commands that would run.
//
// Phase 2 is the plan, not the execution: resolve which profile applies, refuse
// one that does not match the file, and print the argv that phase 3 will spawn.
// Nothing here starts a process.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "heavenzip/detector.hpp"
#include "heavenzip/ini.hpp"

namespace heavenzip {

struct StepCommand {
  std::string step;     // section name from the profile
  std::string program;  // step's bin, or ziptastic_bin from [general]
  std::vector<std::string> argv;      // program first, then its arguments
  std::vector<std::string> reverse;   // the command that undoes this step

  // Shell-quoted for humans: `bin/ziptastic.exe -c lzma -9 "in put.txt"`.
  std::string shell_line() const;
};

struct Plan {
  std::string profile;   // profile name, for the message and the manifest
  std::string detect;    // the profile's detect rule
  FileClass found;       // what the input actually is
  std::filesystem::path input;
  std::filesystem::path output;    // <input>.hzpkg unless --out
  std::filesystem::path manifest;  // <input>.hzpipeline
  std::vector<StepCommand> steps;
};

// True when the program can be found: an explicit path must exist, a bare name is
// looked up on PATH. Used by the pre-flight check and by `validate`.
bool program_available(const std::string& program);

// Resolve the profile and build the plan. Throws std::runtime_error when the
// profile does not match the file, or when a step's program cannot be resolved.
//
// The intermediate names are part of the contract: step i reads what step i-1
// wrote, and those files live beside the output as <output>.<step>.tmp so a
// failed run leaves evidence rather than a mystery.
Plan build_plan(const MasterConfig& master, const Profile& profile, const std::string& profile_name,
                const std::filesystem::path& input, const std::filesystem::path& output);

}  // namespace heavenzip

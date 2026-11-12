// runner.hpp — spawn one step and report what it did.
//
// heavenzip's whole reason to exist is this file: it never links a compressor, it
// starts one. That keeps a GPL tool at arm's length and makes the pipeline a
// process boundary rather than a licence question.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace heavenzip {

struct RunResult {
  int exit_code = -1;
  double seconds = 0.0;
  std::filesystem::path stdout_path;
  std::filesystem::path stderr_path;
  std::string stdout_tail;  // last lines, for the error message
  std::string stderr_tail;
};

// Run argv[0] with argv[1..], capturing both streams to files. Never throws for a
// non-zero exit — the caller decides what a failure means. Throws only when the
// process cannot be started at all, because that is a different problem from a
// step that ran and refused.
RunResult run_capture(const std::vector<std::string>& argv, const std::filesystem::path& stdout_path,
                      const std::filesystem::path& stderr_path);

}  // namespace heavenzip

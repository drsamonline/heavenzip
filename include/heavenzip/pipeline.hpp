// pipeline.hpp — run a plan, and write the manifest that undoes it.
//
// AGENT_PROMPT.md section 6: `<input>.hzpkg` holds the payload and
// `<input>.hzpipeline` describes the reverse pipeline as JSON. The manifest is the
// only record of how the payload was made, so it is written before the run can be
// called a success and it names every step's argv in both directions.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "heavenzip/dispatcher.hpp"

namespace heavenzip {

struct StepOutcome {
  std::string step;
  int exit_code = 0;
  double seconds = 0.0;
  std::filesystem::path stdout_log;
  std::filesystem::path stderr_log;
};

struct RunOutcome {
  bool ok = false;
  std::string failed_step;   // empty when ok
  int exit_code = 0;
  std::string detail;        // last lines of the failing step's stderr
  std::vector<StepOutcome> steps;
};

// Spawn every step in order. Stops at the first non-zero exit, keeping the logs
// and the intermediates, because a failed pipeline that leaves evidence is worth
// more than one that cleans up after itself.
RunOutcome run_plan(const Plan& plan);

// Write the JSON manifest. `reverse` per step is the command that undoes it:
// `<bin> d <out> <in>` by convention, recorded explicitly so a reader never has to
// guess what the reverse of a step is.
void write_manifest(const Plan& plan, const RunOutcome& outcome);

// The reverse plan read back out of a manifest: steps in reverse order, each with
// the argv recorded for it.
struct ReverseStep {
  std::string step;
  std::vector<std::string> argv;
};
std::vector<ReverseStep> read_reverse_plan(const std::filesystem::path& manifest);

// Run a manifest's reverse steps, writing the restored file to `out`.
RunOutcome run_reverse(const std::filesystem::path& manifest, const std::filesystem::path& out);

}  // namespace heavenzip

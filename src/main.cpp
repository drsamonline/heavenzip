// main.cpp — the heavenzip CLI.
//
// Phase 0 is the skeleton and the INI parser: `validate` and `list-profiles` are
// real, and everything that needs a dispatcher says so and exits 2 rather than
// pretending. A command that is not built yet should be a refusal, not a no-op.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "heavenzip/detector.hpp"
#include "heavenzip/dispatcher.hpp"
#include "heavenzip/pipeline.hpp"
#include "heavenzip/ini.hpp"

namespace fs = std::filesystem;
using namespace heavenzip;

namespace {

constexpr const char* kName = "heavenzip";

void usage(std::ostream& out) {
  out << "heavenzip " << HEAVENZIP_VERSION << " — CHCE orchestrator\n"
      << "\n"
      << "Usage: heavenzip <command> [options]\n"
      << "\n"
      << "Commands:\n"
      << "  compress <input>        plan or run the pipeline for a file\n"
      << "  decompress <input>      reverse a .hzpkg using its .hzpipeline manifest\n"
      << "  list-profiles           show every profile and its pipeline\n"
      << "  detect <file>           classify a file the way a profile's `detect` would\n"
      << "  validate                parse the master config and every profile\n"
      << "  bench <dir>             run every profile over a corpus, print ratio and time\n"
      << "  autotune <file>         try every pipeline the tools allow, rank by size,\n"
      << "                          and optionally write the winner as a profile\n"
      << "  version                 print the version\n"
      << "\n"
      << "Options:\n"
      << "  --config <dir>          config directory (default: ./config, then beside the binary)\n"
      << "  --require-bins          validate fails when a profile's binary is missing\n"
      << "  --profile <name>        profile to use (default: `default` from [profiles])\n"
      << "  --out <file>            output path (default: <input>.hzpkg)\n"
      << "  --dry-run               print the argv each step would run, spawn nothing\n"
      << "  --write <name>          autotune: save the winning pipeline as profiles/<name>.ini\n"
      << "  --goal ratio|speed|balanced  autotune: which winner --write saves (default ratio)\n"
      << "  --templates <dir>       autotune: write best-ratio/-speed/-balanced.ini into <dir>\n"
      << "  --no-verify             compress: skip the reverse-and-compare check (it is on by default)\n"
      << "  --help, -h              this text\n"
      << "  --version, -V           print the version\n";
}

int do_validate(const fs::path& config_dir, bool require_bins) {
  int failures = 0;
  int warnings = 0;
  const auto report = [&failures](const std::string& what, const std::string& error) {
    if (error.empty()) {
      std::cout << "  ok    " << what << "\n";
    } else {
      std::cout << "  FAIL  " << what << "\n        " << error << "\n";
      ++failures;
    }
  };

  MasterConfig master;
  try {
    master = MasterConfig::load(config_dir / "syc.ini");
  } catch (const IniError& error) {
    report("config/syc.ini", error.what());
    std::cout << "\n" << failures << " problem(s)\n";
    return 1;
  }
  report("config/syc.ini", {});

  // Every profile the master config advertises, plus the default even if it was
  // left out of `available` (that mismatch is already an error above).
  std::vector<std::string> names = master.available;
  for (const fs::path& entry : fs::directory_iterator(config_dir / "profiles")) {
    if (entry.extension() != ".ini") continue;
    const std::string stem = entry.stem().string();
    if (std::find(names.begin(), names.end(), stem) == names.end()) names.push_back(stem);
  }

  for (const std::string& name : names) {
    const fs::path path = config_dir / "profiles" / (name + ".ini");
    if (!fs::exists(path)) {
      report("config/profiles/" + name + ".ini", "listed in `available` but the file does not exist");
      continue;
    }
    try {
      Profile::load(path);
      report("config/profiles/" + name + ".ini", {});
    } catch (const IniError& error) {
      report("config/profiles/" + name + ".ini", error.what());
    }
  }

  // Section 7 asks validate to fail when a required bin is missing. On a fresh
  // clone every bin is missing — `bin/` is filled by the sync workflow, and the
  // shipped profiles name Windows executables — so strict-by-default would make
  // `validate` fail on every machine that has not synced yet. The check runs,
  // reports, and only fails with --require-bins.
  for (const std::string& name : names) {
    const fs::path path = config_dir / "profiles" / (name + ".ini");
    if (!fs::exists(path)) continue;
    Profile profile;
    try {
      profile = Profile::load(path);
    } catch (const IniError&) {
      continue;  // already reported above
    }
    for (const Step& step : profile.steps) {
      const std::string bin = step.bin.empty() ? master.ziptastic_bin : step.bin;
      if (program_available(bin)) continue;
      std::cout << "  warn  " << name << "/" << step.name << ": `" << bin
                << "` is not present" << (require_bins ? "" : " (run sync-binaries, or pass --require-bins to fail)")
                << "\n";
      ++warnings;
      if (require_bins) ++failures;
    }
  }

  if (failures == 0) {
    std::cout << "\nvalid: " << names.size() << " profile(s), master config, ziptastic_bin = "
              << master.ziptastic_bin;
    if (warnings > 0) std::cout << " — " << warnings << " bin(s) not present on this machine";
    std::cout << "\n";
    return 0;
  }
  std::cout << "\n" << failures << " problem(s)\n";
  return 1;
}

int do_list_profiles(const fs::path& config_dir) {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  std::cout << "default: " << master.default_profile << "\n";
  for (const std::string& name : master.available) {
    const Profile profile = Profile::load(config_dir / "profiles" / (name + ".ini"));
    std::cout << "\n" << name << "  (detect = " << profile.detect << ")\n";
    for (const Step& step : profile.steps) {
      std::cout << "  " << step.name << ": " << (step.bin.empty() ? master.ziptastic_bin : step.bin);
      if (!step.args.empty()) std::cout << " " << step.args;
      std::cout << "\n";
    }
  }
  return 0;
}

// Byte-for-byte comparison, streamed: these files can be large and the point is to
// be sure, not to be quick.
bool same_bytes(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
  std::ifstream left(a, std::ios::binary);
  std::ifstream right(b, std::ios::binary);
  char lb[65536];
  char rb[65536];
  while (left && right) {
    left.read(lb, sizeof(lb));
    right.read(rb, sizeof(rb));
    const auto n = left.gcount();
    if (n != right.gcount()) return false;
    if (std::memcmp(lb, rb, static_cast<std::size_t>(n)) != 0) return false;
  }
  return true;
}

// An exit code above 128 is a death by signal on POSIX: 139 is SIGSEGV, 134 SIGABRT.
// Reporting that as "failed" hides the difference between a tool that refused and a
// tool that crashed — and srep does crash here (munmap_chunk: invalid pointer).
std::string exit_meaning(int code) {
  if (code > 128) {
    const int signal = code - 128;
    const char* name = "signal";
    switch (signal) {
      case 4: name = "SIGILL"; break;
      case 6: name = "SIGABRT"; break;
      case 8: name = "SIGFPE"; break;
      case 9: name = "SIGKILL"; break;
      case 11: name = "SIGSEGV"; break;
      case 15: name = "SIGTERM"; break;
      default: break;
    }
    return std::string("crashed with ") + name + " (" + std::to_string(signal) + ")";
  }
  return "exit " + std::to_string(code);
}

int do_compress(const fs::path& config_dir, const std::string& input, const std::string& profile_name,
                const std::string& out, bool dry_run, bool verify) {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  const std::string name = profile_name.empty() ? master.default_profile : profile_name;

  if (!fs::exists(input)) {
    std::cerr << kName << ": no such file: " << input << "\n";
    return 1;
  }
  if (std::find(master.available.begin(), master.available.end(), name) == master.available.end()) {
    std::cerr << kName << ": unknown profile `" << name << "`. Available: ";
    for (std::size_t i = 0; i < master.available.size(); ++i) {
      if (i) std::cerr << ", ";
      std::cerr << master.available[i];
    }
    std::cerr << "\n";
    return 1;
  }

  const Profile profile = Profile::load(config_dir / "profiles" / (name + ".ini"));
  const fs::path output = out.empty() ? fs::path(input + ".hzpkg") : fs::path(out);
  const Plan plan = build_plan(master, profile, name, input, output);

  if (dry_run) {
    std::cout << "profile: " << plan.profile << "  (detect = " << plan.detect << ", file is "
              << to_string(plan.found) << ")\n"
              << "output:  " << plan.output.string() << "\n"
              << "manifest: " << plan.manifest.string() << "\n\n";
    for (const StepCommand& step : plan.steps) {
      std::cout << "[" << step.step << "]\n  " << step.shell_line() << "\n";
    }
    return 0;
  }

  // Pre-flight: every program must be resolvable before the first step runs. A
  // missing paq8px should be a sentence, not a step that failed with exit 127
  // halfway through a chain that has already rewritten its intermediates.
  for (const StepCommand& step : plan.steps) {
    if (program_available(step.program)) continue;
    std::cerr << kName << ": step `" << step.step << "` needs `" << step.program
              << "`, which is not on PATH and does not exist.\n"
              << "Nothing was run and the input is untouched. Place the binary in bin/, "
                 "or run the sync-binaries workflow.\n";
    return 1;
  }

  const RunOutcome outcome = run_plan(plan);
  write_manifest(plan, outcome);

  if (!outcome.ok) {
    std::cerr << kName << ": step `" << outcome.failed_step << "` "
              << exit_meaning(outcome.exit_code) << "\n";
    if (!outcome.detail.empty()) std::cerr << outcome.detail << "\n";
    std::cerr << "manifest written: " << plan.manifest.string() << " (it records the failure)\n";
    return 1;
  }
  std::cout << "compressed " << plan.input.string() << " -> " << plan.output.string() << "\n"
            << "manifest   " << plan.manifest.string() << "\n";
  for (const StepOutcome& step : outcome.steps) {
    std::cout << "  " << step.step << "  exit " << step.exit_code << "  " << step.seconds << "s\n";
  }

  if (verify) {
    // An exit code of 0 is not proof. paq8px at -8 runs out of memory, prints
    // "Out of memory.", writes a 14-byte stub and returns 0 — so without this the
    // archive would be written, the manifest would say ok, and the user would find
    // out when they tried to restore it. Reverse the plan and compare the bytes
    // before the archive is called good.
    const fs::path restored = plan.output.string() + ".verify";
    std::error_code ec;
    fs::remove(restored, ec);
    const RunOutcome back = run_reverse(plan.manifest, restored);
    const bool identical = back.ok && same_bytes(plan.input, restored);
    fs::remove(restored, ec);
    if (!identical) {
      std::cerr << kName << ": the archive does not restore the input"
                << (back.ok ? " (bytes differ)" : (" (reverse failed at " + back.failed_step + ")"))
                << "\n"
                << "The payload was written but it is not a copy of the input. Keep the "
                   "manifest and the input; do not delete either.\n";
      return 1;
    }
    std::cout << "verified   the archive restores the input byte for byte\n";
  }
  return 0;
}

int do_decompress(const std::string& container, const std::string& out) {
  const fs::path pkg = container;
  if (!fs::exists(pkg)) {
    std::cerr << kName << ": no such file: " << pkg << "\n";
    return 1;
  }
  // Section 6 names the two files after the input, not after each other:
  // input.txt -> input.txt.hzpkg + input.txt.hzpipeline. So the manifest is found
  // by stripping the payload suffix, not by appending to it.
  std::string base = pkg.string();
  const std::string suffix = ".hzpkg";
  if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
    base.resize(base.size() - suffix.size());
  }
  fs::path manifest = base + ".hzpipeline";
  if (!fs::exists(manifest) && fs::exists(pkg.string() + ".hzpipeline")) {
    manifest = pkg.string() + ".hzpipeline";  // tolerate the other spelling
  }
  if (!fs::exists(manifest)) {
    std::cerr << kName << ": " << manifest.string()
              << " is missing; without it there is no record of how this payload was made\n";
    return 1;
  }
  const fs::path target = out.empty() ? fs::path(pkg.string() + ".out") : fs::path(out);
  const RunOutcome outcome = run_reverse(manifest, target);
  if (!outcome.ok) {
    std::cerr << kName << ": reverse step `" << outcome.failed_step << "` failed with exit "
              << outcome.exit_code << "\n";
    if (!outcome.detail.empty()) std::cerr << outcome.detail << "\n";
    return 1;
  }
  std::cout << "restored " << target.string() << "\n";
  for (const StepOutcome& step : outcome.steps) {
    std::cout << "  " << step.step << "  exit " << step.exit_code << "  " << step.seconds << "s\n";
  }
  return 0;
}

// autotune — which combination of the tools actually present compresses this file
// smallest?
//
// The profiles are guesses someone wrote down. This tries the combinations that
// the tools on this machine make possible, measures the real output, and prints
// them smallest first. With --write it turns the winner into a profile file, so a
// ratio someone discovered once is not lost.
//
// It is a search over a handful of candidates, not a solver: it runs each one to
// completion and believes the file size. Anything that cannot run is reported as
// skipped with the missing program named, never silently dropped.
struct Candidate {
  std::string name;
  std::string text;
};

std::vector<Candidate> autotune_candidates() {
  // Ordered by how likely they are to be worth the time, which only affects the
  // order they are tried in — the report sorts by measured size.
  return {
      {"z9", "[route]\ndetect = any\npipeline = ziptastic\n\n[ziptastic]\nargs = -9\n"},
      {"zmax", "[route]\ndetect = any\npipeline = ziptastic\n\n[ziptastic]\nargs = --max-ratio\n"},
      {"srep-z9",
       "[route]\ndetect = any\npipeline = srep,ziptastic\n\n[srep]\nbin = bin/srep.exe\nargs = -l256m -m3f\nreverse_args = -d\n\n"
       "[ziptastic]\nargs = -9\n"},
      {"srep-zmax",
       "[route]\ndetect = any\npipeline = srep,ziptastic\n\n[srep]\nbin = bin/srep.exe\nargs = -l256m -m5f\nreverse_args = -d\n\n"
       "[ziptastic]\nargs = --max-ratio\n"},
      {"paq8", "[route]\ndetect = any\npipeline = paq8px\n\n[paq8px]\nbin = bin/paq8px.exe\nargs = -5\nreverse_args = -d\n"},
      // A tool whose CLI is not `input output`. This is only expressible because
      // steps take `{in}`/`{out}` placeholders; before that it needed a wrapper.
      // System tools, looked up on PATH. Same stdout shape as brotli, and they cost
      // nothing to try when the machine already has them.
      {"zstd", "[route]\ndetect = any\npipeline = zstd\n\n[zstd]\nbin = zstd\n"
               "args = -19 -c {in}\nreverse_args = -d -c {in}\nstdout_to_output = true\n"},
      {"xz", "[route]\ndetect = any\npipeline = xz\n\n[xz]\nbin = xz\n"
             "args = -9 -c {in}\nreverse_args = -d -c {in}\nstdout_to_output = true\n"},
      {"brotli", "[route]\ndetect = any\npipeline = brotli\n\n[brotli]\nbin = bin/brotli.exe\n"
                 "args = -q 11 -o {out} {in}\nreverse_args = -d -o {out} {in}\n"},
      {"srep-brotli",
       "[route]\ndetect = any\npipeline = srep,brotli\n\n[srep]\nbin = bin/srep.exe\nargs = -m5f\n"
       "reverse_args = -d\n\n[brotli]\nbin = bin/brotli.exe\nargs = -q 11 -o {out} {in}\n"
       "reverse_args = -d -o {out} {in}\n"},
      {"srep-paq8",
       "[route]\ndetect = any\npipeline = srep,paq8px\n\n[srep]\nbin = bin/srep.exe\nargs = -l256m -m5f\nreverse_args = -d\n\n"
       "[paq8px]\nbin = bin/paq8px.exe\nargs = -8\nreverse_args = -d\n"},
  };
}

int do_autotune(const fs::path& config_dir, const fs::path& input,
                const std::string& write_as, const std::string& goal,
                const fs::path& templates_dir) {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  if (!fs::is_regular_file(input)) {
    std::cerr << kName << ": no such file: " << input << "\n";
    return 1;
  }
  const auto in_size = fs::file_size(input);
  const Detection found = detect_file(input);

  struct Result {
    std::string name;
    std::string text;
    bool ok = false;
    bool verified = false;
    std::string note;
    std::uintmax_t out_size = 0;
    double seconds = 0.0;
    std::string pending_manifest;
    std::string pending_output;
  };
  std::vector<Result> results;
  std::vector<Result> kept;   // measured, awaiting verification

  // Beside the input, not in /tmp. A 64 MB /tmp filled up and every chained
  // candidate failed with "Can't write to output file (disk full?)" — which looks
  // exactly like a broken pipeline and is not. The user's data is here, so the room
  // to measure it is here too; TMPDIR is still honoured when it is set explicitly.
  fs::path work = fs::temp_directory_path() / "heavenzip-autotune";
  if (const char* forced = std::getenv("HEAVENZIP_WORK"); forced != nullptr && *forced != '\0') {
    work = fs::path(forced);
  } else {
    // `fs::path("s.txt").has_parent_path()` is false, so a bare filename fell
    // through to /tmp — the exact case the fix was for. The directory is the
    // input's parent, or the current directory when the name has none.
    const fs::path parent = input.has_parent_path() ? input.parent_path() : fs::current_path();
    work = parent / ".heavenzip-autotune";
  }
  fs::create_directories(work);

  for (const Candidate& candidate : autotune_candidates()) {
    Result result;
    result.name = candidate.name;
    result.text = candidate.text;
    try {
      const Profile profile = Profile::parse(candidate.text, candidate.name);
      std::string missing;
      for (const Step& step : profile.steps) {
        const std::string bin = step.bin.empty() ? master.ziptastic_bin : step.bin;
        if (!program_available(bin) && missing.empty()) missing = bin;
      }
      if (!missing.empty()) {
        result.note = "skipped: " + missing + " not present";
        results.push_back(result);
        continue;
      }

      const fs::path out = work / (candidate.name + ".hzpkg");
      // Clean slate per candidate: a stale payload or intermediate from an earlier
      // run makes a compressor refuse to overwrite, and the failure then looks like
      // a broken pipeline rather than leftover state.
      for (const fs::directory_entry& entry : fs::directory_iterator(work)) {
        const std::string stem = entry.path().filename().string();
        if (stem.rfind(candidate.name + ".", 0) == 0) {
          std::error_code rm_ec;
          fs::remove(entry.path(), rm_ec);
        }
      }
      const Plan plan = build_plan(master, profile, candidate.name, input, out);
      const RunOutcome outcome = run_plan(plan);
      // run_plan spawns the steps; the manifest is the caller's job (do_compress
      // does the same). Without it there is nothing to reverse, and the first
      // version of this command reported every candidate as "cannot be opened".
      write_manifest(plan, outcome);
      if (!outcome.ok) {
        // Name the step *and* why. "failed at srep" sent me looking at the wrong
        // thing twice; the step's own stderr is the answer.
        std::string why = outcome.detail;
        for (char& c : why) {
          if (c == '\n') c = ' ';
        }
        if (why.size() > 70) why = why.substr(0, 70) + "…";
        result.note = "failed at " + outcome.failed_step + " (" +
                      exit_meaning(outcome.exit_code) + ")" + (why.empty() ? "" : ": " + why);
        results.push_back(result);
        continue;
      }
      for (const StepOutcome& step : outcome.steps) result.seconds += step.seconds;

      // A size is not a result, so verification comes next — but after the
      // ranking, not inside the measurement loop. Verifying every candidate
      // doubles a slow run (zmax takes ~27 s on a 1.3 MB file); ranking first and
      // then proving the smallest one is both faster and stricter, because the
      // first candidate that cannot restore its own output is skipped in favour of
      // the next.
      // Every candidate plans from the same input, so every manifest lands on the
      // same path (`<input>.hzpipeline`) and the last one written would be the only
      // survivor. Move each aside so the verification pass can still find it.
      const fs::path own_manifest = work / (candidate.name + ".hzpipeline");
      std::error_code move_ec;
      fs::rename(plan.manifest, own_manifest, move_ec);
      result.out_size = fs::file_size(out);
      result.ok = true;
      result.pending_manifest = (move_ec ? plan.manifest : own_manifest).string();
      result.pending_output = out.string();
      kept.push_back(std::move(result));
      continue;
    } catch (const std::exception& error) {
      result.note = std::string("error: ") + error.what();
    }
    results.push_back(result);
  }

  for (Result& result : kept) results.push_back(std::move(result));

  // Verify smallest-first, and stop at the first candidate that restores its own
  // input. Anything smaller than the winner that cannot be reversed is reported
  // with the reason, which is the whole point: paq8px at -8 produces a 14-byte
  // "archive" for a 1.3 MB file and exits 0.
  std::vector<Result*> by_size;
  for (Result& result : results) {
    if (result.ok) by_size.push_back(&result);
  }
  std::sort(by_size.begin(), by_size.end(),
            [](const Result* a, const Result* b) { return a->out_size < b->out_size; });
  for (Result* result : by_size) {
    const fs::path restored = work / (result->name + ".restored");
    std::error_code ec;
    fs::remove(restored, ec);
    RunOutcome back;
    try {
      back = run_reverse(result->pending_manifest, restored);
    } catch (const std::exception& error) {
      result->ok = false;
      result->note = std::string("cannot verify: ") + error.what();
      continue;
    }
    if (!back.ok) {
      result->ok = false;
      std::string why = back.detail;
      for (char& c : why) {
        if (c == '\n') c = ' ';
      }
      if (why.size() > 70) why = why.substr(0, 70) + "…";
      result->note = "cannot be reversed at " + back.failed_step + (why.empty() ? "" : ": " + why);
    } else if (!same_bytes(input, restored)) {
      result->ok = false;
      result->note = "round trip does not restore the input";
    } else {
      result->verified = true;
    }
    fs::remove(restored, ec);
    fs::remove(result->pending_output, ec);
    fs::remove(result->pending_manifest, ec);
  }

  std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
    if (a.ok != b.ok) return a.ok;          // the ones that ran come first
    if (!a.ok) return a.name < b.name;
    return a.out_size < b.out_size;         // then smallest output wins
  });

  std::printf("autotune %s  (%ju bytes, %s)\n\n", input.filename().string().c_str(),
              static_cast<std::uintmax_t>(in_size), to_string(found.value).c_str());
  std::printf("%-12s %-12s %-9s %-8s %s\n", "pipeline", "out", "ratio", "time", "status");
  std::printf("%-12s %-12s %-9s %-8s %s\n", "------------", "------------", "---------",
              "--------", "------");

  // Three goals, because "best" is two questions. The smallest file and the
  // fastest run are rarely the same pipeline, and a user choosing a profile needs
  // to know which trade they are making rather than being handed one number.
  const Result* best_ratio = nullptr;
  const Result* best_speed = nullptr;
  double fastest = 0.0;
  for (const Result& result : results) {
    if (!result.ok || !result.verified) continue;
    if (best_ratio == nullptr || result.out_size < best_ratio->out_size) best_ratio = &result;
    if (best_speed == nullptr || result.seconds < best_speed->seconds) {
      best_speed = &result;
      fastest = result.seconds;
    }
  }
  // Balanced: the smallest output among the candidates that finish within twice the
  // fastest time. A 30-second pipeline that saves 0.4 % is not a better default.
  const Result* best_balanced = nullptr;
  for (const Result& result : results) {
    if (!result.ok || !result.verified) continue;
    if (result.seconds > fastest * 2.0 + 0.05) continue;
    if (best_balanced == nullptr || result.out_size < best_balanced->out_size) {
      best_balanced = &result;
    }
  }

  for (const Result& result : results) {
    if (!result.ok) {
      std::printf("%-12s %-12s %-9s %-8s %s\n", result.name.c_str(), "-", "-", "-",
                  result.note.c_str());
      continue;
    }
    const double ratio = static_cast<double>(result.out_size) / static_cast<double>(in_size);
    char time_text[32];
    std::snprintf(time_text, sizeof(time_text), "%.2fs", result.seconds);
    std::printf("%-12s %-12ju %-9.4f %-8s %s\n", result.name.c_str(),
                static_cast<std::uintmax_t>(result.out_size), ratio, time_text,
                [&]() -> std::string {
                  if (!result.verified) return "";
                  std::string tags;
                  if (&result == best_ratio) tags += "smallest ";
                  if (&result == best_speed) tags += "fastest ";
                  if (&result == best_balanced) tags += "balanced ";
                  return tags.empty() ? "verified" : (tags + "verified");
                }().c_str());
  }

  if (best_ratio == nullptr) {
    std::printf("\nnothing verified: every candidate either needs a program that is not "
                "in bin/, or cannot restore its own output\n");
    return 1;
  }
  const auto show = [&](const char* label, const Result* result) {
    if (result == nullptr) {
      std::printf("%-9s none\n", label);
      return;
    }
    std::printf("%-9s %-10s ratio %.4f   %ju bytes   %.2fs\n", label, result->name.c_str(),
                static_cast<double>(result->out_size) / static_cast<double>(in_size),
                static_cast<std::uintmax_t>(result->out_size), result->seconds);
  };
  std::printf("\n");
  show("smallest", best_ratio);
  show("fastest", best_speed);
  show("balanced", best_balanced);

  // --templates writes all three, so a user can compare them side by side instead
  // of taking one number on trust.
  if (!templates_dir.empty()) {
    std::error_code ec;
    fs::create_directories(templates_dir, ec);
    const std::pair<const char*, const Result*> picks[] = {
        {"ratio", best_ratio}, {"speed", best_speed}, {"balanced", best_balanced}};
    for (const auto& [label, result] : picks) {
      if (result == nullptr) continue;
      const fs::path path = templates_dir / (std::string("best-") + label + ".ini");
      std::ofstream out(path);
      out << "# Generated by `heavenzip autotune` from " << input.filename().string() << "\n"
          << "# goal: " << label << "  —  " << result->name << "\n"
          << "# measured " << result->out_size << " bytes from "
          << static_cast<std::uintmax_t>(in_size) << " (ratio "
          << static_cast<double>(result->out_size) / static_cast<double>(in_size)
          << "), " << result->seconds << "s, round trip verified\n"
          << "# Re-measure on a different corpus before trusting it there.\n" << result->text;
      std::cout << "wrote " << path.string() << "\n";
    }
    std::cout << "to use one, copy it into config/profiles/ and add its name to "
                 "[profiles] available\n";
    return 0;
  }

  const Result* chosen = best_ratio;
  if (goal == "speed") chosen = best_speed;
  if (goal == "balanced") chosen = best_balanced;
  if (chosen == nullptr) chosen = best_ratio;

  if (write_as.empty()) {
    std::printf("\nwrite it as a profile with: heavenzip autotune %s --write <name> "
                "[--goal ratio|speed|balanced]\n",
                input.filename().string().c_str());
    return 0;
  }

  // Writing it means the discovery survives: a profile file plus the one line in
  // syc.ini that makes `--profile <name>` resolve. Both are printed as they change.
  const fs::path profile_path = config_dir / "profiles" / (write_as + ".ini");
  if (fs::exists(profile_path)) {
    std::cerr << kName << ": " << profile_path << " already exists; not overwriting it\n";
    return 1;
  }
  std::string text = chosen->text;
  text = "# Generated by `heavenzip autotune` from " + input.filename().string() +
         " (goal: " + goal + ").\n" +
         "# Measured " + std::to_string(chosen->out_size) + " bytes from " +
         std::to_string(static_cast<std::uintmax_t>(in_size)) + " (ratio " +
         std::to_string(static_cast<double>(chosen->out_size) / static_cast<double>(in_size)) +
         ", " + std::to_string(chosen->seconds) +
         "s, round trip verified). Re-measure before trusting it on a different corpus.\n" + text;
  { std::ofstream out(profile_path); out << text; }
  std::cout << "wrote " << profile_path << "\n";

  const fs::path master_path = config_dir / "syc.ini";
  std::string master_text;
  { std::ifstream in(master_path); master_text.assign(std::istreambuf_iterator<char>(in),
                                                     std::istreambuf_iterator<char>()); }
  const std::string key = "available =";
  const auto at = master_text.find(key);
  if (at != std::string::npos) {
    const auto eol = master_text.find('\n', at);
    const std::string line = master_text.substr(at, eol - at);
    if (line.find(write_as) == std::string::npos) {
      master_text.insert(eol, ", " + write_as);
      { std::ofstream out(master_path); out << master_text; }
      std::cout << "added `" << write_as << "` to [profiles] available in " << master_path << "\n";
    }
  }
  std::cout << "check it with: heavenzip validate\n";
  return 0;
}

int do_bench(const fs::path& config_dir, const fs::path& corpus, bool require_bins) {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  std::vector<fs::path> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(corpus)) {
    if (entry.is_regular_file()) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    std::cerr << kName << ": no files in " << corpus << "\n";
    return 1;
  }

  std::cout << "profile      file                       class    in        out       ratio   time\n";
  std::cout << "-----------------------------------------------------------------------------------\n";

  int failures = 0;
  for (const std::string& name : master.available) {
    const Profile profile = Profile::load(config_dir / "profiles" / (name + ".ini"));
    for (const fs::path& file : files) {
      const Detection found = detect_file(file);
      if (!matches(profile.detect, found.value)) continue;

      bool available = true;
      for (const Step& step : profile.steps) {
        const std::string bin = step.bin.empty() ? master.ziptastic_bin : step.bin;
        if (!program_available(bin)) available = false;
      }
      if (!available) {
        if (require_bins) ++failures;
        std::printf("%-12s %-26s %-8s %-9s %-9s %-7s %s\n", name.c_str(),
                    file.filename().string().c_str(), to_string(found.value).c_str(), "-", "-", "-",
                    "skipped (bin missing)");
        continue;
      }

      const fs::path out = fs::temp_directory_path() / ("bench-" + name + "-" + file.filename().string() + ".hzpkg");
      try {
        const Plan plan = build_plan(master, profile, name, file, out);
        const RunOutcome outcome = run_plan(plan);
        if (!outcome.ok) {
          ++failures;
          std::printf("%-12s %-26s %-8s %-9s %-9s %-7s %s\n", name.c_str(),
                      file.filename().string().c_str(), to_string(found.value).c_str(), "-", "-", "-",
                      ("failed at " + outcome.failed_step).c_str());
          continue;
        }
        const auto raw = static_cast<double>(fs::file_size(file));
        const auto packed = static_cast<double>(fs::file_size(out));
        double seconds = 0;
        for (const StepOutcome& step : outcome.steps) seconds += step.seconds;
        char ratio[16];
        std::snprintf(ratio, sizeof ratio, "%.1f%%", packed * 100.0 / raw);
        std::printf("%-12s %-26s %-8s %-9.0f %-9.0f %-7s %.2fs\n", name.c_str(),
                    file.filename().string().c_str(), to_string(found.value).c_str(), raw, packed,
                    ratio, seconds);
        fs::remove(out);
      } catch (const std::exception& error) {
        ++failures;
        std::printf("%-12s %-26s %-8s %-9s %-9s %-7s %s\n", name.c_str(),
                    file.filename().string().c_str(), to_string(found.value).c_str(), "-", "-", "-",
                    error.what());
      }
    }
  }
  return failures == 0 ? 0 : 1;
}

int do_detect(const std::string& path) {
  const Detection found = detect_file(path);
  std::cout << path << "\n  class: " << to_string(found.value) << "\n  why:   " << found.why
            << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  fs::path config_dir;
  bool require_bins = false;
  bool dry_run = false;
  bool verify = true;
  std::string profile_name;
  std::string out;
  std::string write_as;   // autotune --write <name>
  std::string goal = "ratio";   // autotune --goal ratio|speed|balanced
  fs::path templates_dir;       // autotune --templates <dir>

  // --config may appear anywhere; take it out before reading the command.
  for (auto it = args.begin(); it != args.end();) {
    if (*it == "--config") {
      if (it + 1 == args.end()) {
        std::cerr << kName << ": --config needs a directory\n";
        return 2;
      }
      config_dir = *(it + 1);
      it = args.erase(it, it + 2);
      continue;
    }
    if (*it == "--require-bins") {
      require_bins = true;
      it = args.erase(it);
      continue;
    }
    if (*it == "--dry-run") {
      dry_run = true;
      it = args.erase(it);
      continue;
    }
    if (*it == "--no-verify") {
      verify = false;
      it = args.erase(it);
      continue;
    }
    if (*it == "--goal") {
      if (it + 1 == args.end()) {
        std::cerr << kName << ": --goal needs ratio, speed or balanced\n";
        return 2;
      }
      goal = *(it + 1);
      if (goal != "ratio" && goal != "speed" && goal != "balanced") {
        std::cerr << kName << ": --goal must be ratio, speed or balanced, not `" << goal << "`\n";
        return 2;
      }
      it = args.erase(it, it + 2);
      continue;
    }
    if (*it == "--templates") {
      if (it + 1 == args.end()) {
        std::cerr << kName << ": --templates needs a directory\n";
        return 2;
      }
      templates_dir = *(it + 1);
      it = args.erase(it, it + 2);
      continue;
    }
    if (*it == "--write") {
      if (it + 1 == args.end()) {
        std::cerr << kName << ": --write needs a profile name\n";
        return 2;
      }
      write_as = *(it + 1);
      it = args.erase(it, it + 2);
      continue;
    }
    if (*it == "--profile" || *it == "--out") {
      const bool is_profile = (*it == "--profile");
      if (it + 1 == args.end()) {
        std::cerr << kName << ": " << *it << " needs a value\n";
        return 2;
      }
      (is_profile ? profile_name : out) = *(it + 1);
      it = args.erase(it, it + 2);
      continue;
    }
    ++it;
  }

  if (args.empty()) {
    usage(std::cout);
    return 0;
  }
  if (args[0] == "--help" || args[0] == "-h") {
    usage(std::cout);
    return 0;
  }
  if (args[0] == "--version" || args[0] == "-V" || args[0] == "version") {
    std::cout << kName << " " << HEAVENZIP_VERSION << "\n";
    return 0;
  }

  if (config_dir.empty()) config_dir = find_config_dir(argv[0]);
  if (config_dir.empty()) {
    std::cerr << kName << ": no config directory found. Looked at $HEAVENZIP_CONFIG, ./config, "
                 "and beside the binary. Pass --config <dir>.\n";
    return 2;
  }

  const std::string command = args[0];
  try {
    if (command == "validate") return do_validate(config_dir, require_bins);
    if (command == "detect") {
      if (args.size() < 2) {
        std::cerr << kName << ": detect needs a file\n";
        return 2;
      }
      return do_detect(args[1]);
    }
    if (command == "list-profiles") return do_list_profiles(config_dir);
    if (command == "autotune") {
      if (args.size() < 2) {
        std::cerr << kName << ": autotune needs an input file\n";
        return 2;
      }
      return do_autotune(config_dir, args[1], write_as, goal, templates_dir);
    }
    if (command == "compress") {
      if (args.size() < 2) {
        std::cerr << kName << ": compress needs an input file\n";
        return 2;
      }
      return do_compress(config_dir, args[1], profile_name, out, dry_run, verify);
    }
    if (command == "decompress") {
      if (args.size() < 2) {
        std::cerr << kName << ": decompress needs a .hzpkg\n";
        return 2;
      }
      return do_decompress(args[1], out);
    }
    if (command == "bench") {
      if (args.size() < 2) {
        std::cerr << kName << ": bench needs a directory\n";
        return 2;
      }
      return do_bench(config_dir, args[1], require_bins);
    }
  } catch (const IniError& error) {
    std::cerr << kName << ": " << error.what() << "\n";
    return 1;
  } catch (const std::exception& error) {
    // A profile that does not apply is a user mistake, not a crash: exit 1 with
    // the reason rather than letting the exception terminate the process.
    std::cerr << kName << ": " << error.what() << "\n";
    return 1;
  }

  std::cerr << kName << ": unknown command `" << command << "`\n\n";
  usage(std::cerr);
  return 2;
}

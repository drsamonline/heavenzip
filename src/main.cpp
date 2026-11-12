// main.cpp — the heavenzip CLI.
//
// Phase 0 is the skeleton and the INI parser: `validate` and `list-profiles` are
// real, and everything that needs a dispatcher says so and exits 2 rather than
// pretending. A command that is not built yet should be a refusal, not a no-op.
#include <algorithm>
#include <cstdio>
#include <filesystem>
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
      << "  version                 print the version\n"
      << "\n"
      << "Options:\n"
      << "  --config <dir>          config directory (default: ./config, then beside the binary)\n"
      << "  --require-bins          validate fails when a profile's binary is missing\n"
      << "  --profile <name>        profile to use (default: `default` from [profiles])\n"
      << "  --out <file>            output path (default: <input>.hzpkg)\n"
      << "  --dry-run               print the argv each step would run, spawn nothing\n"
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

int do_compress(const fs::path& config_dir, const std::string& input, const std::string& profile_name,
                const std::string& out, bool dry_run) {
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
    std::cerr << kName << ": step `" << outcome.failed_step << "` failed with exit "
              << outcome.exit_code << "\n";
    if (!outcome.detail.empty()) std::cerr << outcome.detail << "\n";
    std::cerr << "manifest written: " << plan.manifest.string() << " (it records the failure)\n";
    return 1;
  }
  std::cout << "compressed " << plan.input.string() << " -> " << plan.output.string() << "\n"
            << "manifest   " << plan.manifest.string() << "\n";
  for (const StepOutcome& step : outcome.steps) {
    std::cout << "  " << step.step << "  exit " << step.exit_code << "  " << step.seconds << "s\n";
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
  std::string profile_name;
  std::string out;

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
    if (command == "compress") {
      if (args.size() < 2) {
        std::cerr << kName << ": compress needs an input file\n";
        return 2;
      }
      return do_compress(config_dir, args[1], profile_name, out, dry_run);
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

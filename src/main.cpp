// main.cpp — the heavenzip CLI.
//
// Phase 0 is the skeleton and the INI parser: `validate` and `list-profiles` are
// real, and everything that needs a dispatcher says so and exits 2 rather than
// pretending. A command that is not built yet should be a refusal, not a no-op.
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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
      << "  compress <input>        run the pipeline for a file (not in phase 0)\n"
      << "  decompress <input>      reverse a .hzpkg using its manifest (not in phase 0)\n"
      << "  list-profiles           show every profile and its pipeline\n"
      << "  validate                parse the master config and every profile\n"
      << "  bench <input>           time the pipeline (not in phase 0)\n"
      << "  version                 print the version\n"
      << "\n"
      << "Options:\n"
      << "  --config <dir>          config directory (default: ./config, then beside the binary)\n"
      << "  --help, -h              this text\n"
      << "  --version, -V           print the version\n";
}

int not_yet(const std::string& command) {
  std::cerr << kName << ": `" << command << "` is not implemented in phase 0.\n"
            << "Phase 0 is the CLI skeleton and the INI parser; `validate` and `list-profiles` work.\n"
            << "See AGENT_PROMPT.md section 8.\n";
  return 2;
}

int do_validate(const fs::path& config_dir) {
  int failures = 0;
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

  if (failures == 0) {
    std::cout << "\nvalid: " << names.size() << " profile(s), master config, ziptastic_bin = "
              << master.ziptastic_bin << "\n";
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

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  fs::path config_dir;

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
    if (command == "validate") return do_validate(config_dir);
    if (command == "list-profiles") return do_list_profiles(config_dir);
    if (command == "compress" || command == "decompress" || command == "bench") return not_yet(command);
  } catch (const IniError& error) {
    std::cerr << kName << ": " << error.what() << "\n";
    return 1;
  }

  std::cerr << kName << ": unknown command `" << command << "`\n\n";
  usage(std::cerr);
  return 2;
}

// test_ini.cpp — the phase-0 tests from AGENT_PROMPT.md section 8.
//
// No framework: a test is a function that returns void and throws on failure, and
// the runner prints which one broke. Adding a dependency to test a parser that
// parses dependencies would be the wrong order of business.
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "heavenzip/ini.hpp"

namespace fs = std::filesystem;
using namespace heavenzip;

namespace {

int g_failures = 0;
std::string g_current;

void check(bool condition, const std::string& what, int line) {
  if (condition) return;
  std::cerr << "FAIL " << g_current << " (line " << line << "): " << what << "\n";
  ++g_failures;
}

#define CHECK(expr) check((expr), #expr, __LINE__)

template <typename Fn>
void expect_ini_error(Fn&& fn, const std::string& needle, int line) {
  try {
    fn();
  } catch (const IniError& error) {
    const std::string message = error.what();
    if (message.find(needle) == std::string::npos) {
      std::cerr << "FAIL " << g_current << " (line " << line << "): message `" << message
                << "` does not contain `" << needle << "`\n";
      ++g_failures;
    }
    return;
  }
  std::cerr << "FAIL " << g_current << " (line " << line << "): expected an IniError\n";
  ++g_failures;
}

#define EXPECT_ERROR(fn, needle) expect_ini_error((fn), (needle), __LINE__)

fs::path config_dir;

// --- the shipped config must parse -----------------------------------------

void test_master_config_parses() {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  CHECK(master.version == "0.1.0");
  CHECK(master.log_level == "info");
  CHECK(!master.ziptastic_bin.empty());
  CHECK(master.default_profile == "text");
  CHECK(master.available.size() == 3);
  CHECK(master.available[0] == "text");
  CHECK(master.available[2] == "max-ratio");
}

void test_every_shipped_profile_parses() {
  const MasterConfig master = MasterConfig::load(config_dir / "syc.ini");
  for (const std::string& name : master.available) {
    const Profile profile = Profile::load(config_dir / "profiles" / (name + ".ini"));
    CHECK(!profile.detect.empty());
    CHECK(!profile.pipeline.empty());
    CHECK(profile.steps.size() == profile.pipeline.size());
    for (std::size_t i = 0; i < profile.steps.size(); ++i) {
      CHECK(profile.steps[i].name == profile.pipeline[i]);
    }
  }
}

// --- round trip: parse -> dump -> parse is the same structure ---------------

void test_round_trip_is_stable() {
  for (const fs::path& path : {config_dir / "syc.ini", config_dir / "profiles" / "text.ini",
                               config_dir / "profiles" / "media.ini",
                               config_dir / "profiles" / "max-ratio.ini"}) {
    const IniFile first = IniFile::load(path);
    const IniFile second = IniFile::parse(first.dump(), first.dump());
    CHECK(second.dump() == first.dump());
    CHECK(second.sections.size() == first.sections.size());
  }
}

// --- grammar ---------------------------------------------------------------

void test_comments_and_blank_lines_are_ignored() {
  const IniFile file = IniFile::parse(
      "; a leading comment\n"
      "\n"
      "[route]\n"
      "detect = text    ; trailing comment\n"
      "pipeline = one\n",
      "inline");
  const IniSection* route = file.find("route");
  CHECK(route != nullptr);
  CHECK(*route->find("detect") == "text");
}

void test_semicolon_inside_quotes_is_data() {
  const IniFile file = IniFile::parse("[step1]\nargs = -x \";not a comment\"\n", "inline");
  const std::string* args = file.find("step1")->find("args");
  CHECK(args != nullptr);
  CHECK(*args == "-x \";not a comment\"");
}

void test_quoted_value_loses_its_quotes() {
  const IniFile file = IniFile::parse("[step1]\nargs = \"-c lzma -9\"\n", "inline");
  CHECK(*file.find("step1")->find("args") == "-c lzma -9");
}

void test_duplicate_section_is_an_error() {
  EXPECT_ERROR([] { IniFile::parse("[a]\nx = 1\n[a]\ny = 2\n", "inline"); }, "duplicate section");
}

void test_duplicate_key_is_an_error() {
  EXPECT_ERROR([] { IniFile::parse("[a]\nx = 1\nx = 2\n", "inline"); }, "duplicate key");
}

void test_key_outside_section_is_an_error() {
  EXPECT_ERROR([] { IniFile::parse("x = 1\n", "inline"); }, "outside any section");
}

void test_garbage_line_is_an_error_with_its_line_number() {
  EXPECT_ERROR([] { IniFile::parse("[a]\nnonsense\n", "inline"); }, "inline:2");
}

// --- profile validation ----------------------------------------------------

void test_route_section_is_mandatory() {
  EXPECT_ERROR([] { Profile::parse("[step1]\nargs = -9\n", "inline"); }, "missing required [route]");
}

void test_pipeline_must_name_existing_sections() {
  EXPECT_ERROR(
      [] { Profile::parse("[route]\ndetect = text\npipeline = ghost\n", "inline"); },
      "no [ghost] section");
}

void test_detect_value_is_checked() {
  EXPECT_ERROR(
      [] { Profile::parse("[route]\ndetect = video\npipeline = one\n[one]\nargs = -9\n", "inline"); },
      "not one of text, media, archive, any");
}

void test_unknown_key_in_step_is_an_error() {
  EXPECT_ERROR(
      [] {
        Profile::parse("[route]\ndetect = any\npipeline = one\n[one]\nargs = -9\nextra = 1\n", "inline");
      },
      "unknown key");
}

void test_reverse_args_are_parsed() {
  const Profile profile = Profile::parse(
      "[route]\ndetect = any\npipeline = one\n[one]\nargs = e -9\nreverse_args = d\n", "inline");
  CHECK(profile.steps[0].args == "e -9");
  CHECK(profile.steps[0].reverse_args == "d");
}

void test_a_repeated_step_is_a_cycle() {
  EXPECT_ERROR(
      [] { Profile::parse("[route]\ndetect = any\npipeline = one,two,one\n[one]\n[two]\n", "inline"); },
      "twice");
}

void test_step_without_args_or_bin_is_allowed_but_has_neither() {
  const Profile profile = Profile::parse(
      "[route]\ndetect = any\npipeline = one\n[one]\n", "inline");
  CHECK(profile.steps.size() == 1);
  CHECK(profile.steps[0].bin.empty());
  CHECK(profile.steps[0].args.empty());
}

// --- argv splitting --------------------------------------------------------

void test_split_args_keeps_quoted_groups() {
  const std::vector<std::string> tokens = split_args("-c lzma -9 --name \"two words\"");
  CHECK(tokens.size() == 5);
  CHECK(tokens[3] == "--name");
  CHECK(tokens[4] == "two words");
}

void test_split_args_of_empty_value_is_empty() {
  CHECK(split_args("").empty());
  CHECK(split_args("   ").empty());
}

struct Case {
  const char* name;
  std::function<void()> fn;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: unit_ini <config-dir>\n";
    return 2;
  }
  config_dir = argv[1];

  const std::vector<Case> cases = {
      {"master config parses", test_master_config_parses},
      {"every shipped profile parses", test_every_shipped_profile_parses},
      {"round trip is stable", test_round_trip_is_stable},
      {"comments ignored", test_comments_and_blank_lines_are_ignored},
      {"semicolon inside quotes", test_semicolon_inside_quotes_is_data},
      {"quoted value", test_quoted_value_loses_its_quotes},
      {"duplicate section", test_duplicate_section_is_an_error},
      {"duplicate key", test_duplicate_key_is_an_error},
      {"key outside section", test_key_outside_section_is_an_error},
      {"garbage line reports its line", test_garbage_line_is_an_error_with_its_line_number},
      {"route mandatory", test_route_section_is_mandatory},
      {"pipeline names existing sections", test_pipeline_must_name_existing_sections},
      {"detect value checked", test_detect_value_is_checked},
      {"unknown step key", test_unknown_key_in_step_is_an_error},
      {"reverse_args parsed", test_reverse_args_are_parsed},
      {"repeated step is a cycle", test_a_repeated_step_is_a_cycle},
      {"step with neither bin nor args", test_step_without_args_or_bin_is_allowed_but_has_neither},
      {"split_args quotes", test_split_args_keeps_quoted_groups},
      {"split_args empty", test_split_args_of_empty_value_is_empty},
  };

  for (const Case& item : cases) {
    g_current = item.name;
    try {
      item.fn();
    } catch (const std::exception& error) {
      std::cerr << "FAIL " << item.name << ": threw " << error.what() << "\n";
      ++g_failures;
    }
  }

  if (g_failures == 0) {
    std::cout << "ok: " << cases.size() << " cases\n";
    return 0;
  }
  std::cerr << g_failures << " failure(s) across " << cases.size() << " cases\n";
  return 1;
}

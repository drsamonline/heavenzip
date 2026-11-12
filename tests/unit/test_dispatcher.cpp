// test_dispatcher.cpp — phase 2: the right profile, and the argv it would run.
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "heavenzip/dispatcher.hpp"
#include "heavenzip/ini.hpp"

namespace fs = std::filesystem;
using namespace heavenzip;

namespace {

int g_failures = 0;
std::string g_current;
fs::path g_config;
fs::path g_fixtures;

void check(bool condition, const std::string& what, int line) {
  if (condition) return;
  std::cerr << "FAIL " << g_current << " (line " << line << "): " << what << "\n";
  ++g_failures;
}

#define CHECK(expr) check((expr), #expr, __LINE__)

MasterConfig master() { return MasterConfig::load(g_config / "syc.ini"); }
Profile profile(const std::string& name) { return Profile::load(g_config / "profiles" / (name + ".ini")); }

void test_text_profile_plans_two_steps() {
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "notes.txt.hzpkg");
  CHECK(plan.steps.size() == 2);
  CHECK(plan.steps[0].step == "srep");
  CHECK(plan.steps[1].step == "ziptastic:lzma");
  CHECK(plan.found == FileClass::Text);
  CHECK(plan.output == g_fixtures / "notes.txt.hzpkg");
  CHECK(plan.manifest == g_fixtures / "notes.txt.hzpipeline");

  // Step 0: its own bin and args, the input, and the intermediate it writes.
  CHECK(plan.steps[0].program == "bin/srep.exe");
  CHECK(plan.steps[0].argv.size() == 4);
  CHECK(plan.steps[0].argv[1] == "-m3f");
  CHECK(plan.steps[0].argv[2] == (g_fixtures / "notes.txt").string());
  CHECK(plan.steps[0].argv[3].find(".tmp") != std::string::npos);

  // Step 1 reads what step 0 wrote and writes the output. The argv tail is
  // [input, output] whatever the step's own arguments were.
  CHECK(plan.steps[1].argv[plan.steps[1].argv.size() - 2] ==
        plan.steps[0].argv[plan.steps[0].argv.size() - 1]);
  CHECK(plan.steps[1].argv.back() == plan.output.string());
  CHECK(plan.steps[1].program == "bin/ziptastic.exe");
}

void test_a_media_profile_never_matches_a_text_file() {
  bool threw = false;
  std::string message;
  try {
    build_plan(master(), profile("media"), "media", g_fixtures / "notes.txt", g_fixtures / "out.hzpkg");
  } catch (const std::runtime_error& error) {
    threw = true;
    message = error.what();
  }
  CHECK(threw);
  CHECK(message.find("does not apply") != std::string::npos);
  CHECK(message.find("text") != std::string::npos);
}

void test_any_matches_a_binary_file() {
  // max-ratio declares `detect = any`, so it plans for a file nothing else claims.
  const Plan plan = build_plan(master(), profile("max-ratio"), "max-ratio", g_fixtures / "random.bin",
                               g_fixtures / "random.bin.hzpkg");
  CHECK(plan.found == FileClass::Binary);
  CHECK(plan.steps.size() == 2);
}

void test_a_step_without_bin_uses_ziptastic_bin() {
  const Profile custom = Profile::parse(
      "[route]\ndetect = any\npipeline = only\n[only]\nargs = -c lzma -9\n", "inline");
  const Plan plan = build_plan(master(), custom, "custom", g_fixtures / "notes.txt",
                               g_fixtures / "x.hzpkg");
  CHECK(plan.steps.size() == 1);
  CHECK(plan.steps[0].program == master().ziptastic_bin);
}

void test_reverse_uses_reverse_args_when_given() {
  const Profile custom = Profile::parse(
      "[route]\ndetect = any\npipeline = only\n[only]\nbin = /bin/sh\nargs = c\n"
      "reverse_args = decompress --force\n",
      "inline");
  const Plan plan = build_plan(master(), custom, "custom", g_fixtures / "notes.txt",
                               g_fixtures / "z.hzpkg");
  const std::vector<std::string>& reverse = plan.steps[0].reverse;
  CHECK(reverse.size() == 5);
  CHECK(reverse[0] == "/bin/sh");
  CHECK(reverse[1] == "decompress");
  CHECK(reverse[2] == "--force");
  CHECK(reverse[3] == (g_fixtures / "z.hzpkg").string());   // produced
  CHECK(reverse[4] == (g_fixtures / "notes.txt").string()); // consumed
}

void test_reverse_defaults_to_the_d_verb() {
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "w.hzpkg");
  CHECK(plan.steps[0].reverse[1] == "d");
}

void test_program_availability_is_checked() {
  CHECK(program_available("/bin/sh"));
  CHECK(!program_available("/nonexistent/binary"));
  CHECK(!program_available("a-binary-that-does-not-exist-anywhere"));
}

void test_shell_line_quotes_spaces() {
  StepCommand command;
  command.step = "s";
  command.program = "bin/ziptastic.exe";
  command.argv = {"bin/ziptastic.exe", "-c", "lzma", "-9", "a file with spaces.txt"};
  const std::string line = command.shell_line();
  CHECK(line.find("\"a file with spaces.txt\"") != std::string::npos);
  CHECK(line.find("bin/ziptastic.exe -c lzma -9") == 0);
}

void test_single_step_plan_writes_straight_to_output() {
  const Profile custom = Profile::parse("[route]\ndetect = any\npipeline = only\n[only]\nargs = -9\n",
                                        "inline");
  const Plan plan = build_plan(master(), custom, "custom", g_fixtures / "notes.txt",
                               g_fixtures / "y.hzpkg");
  CHECK(plan.steps[0].argv.back() == plan.output.string());
}

struct Case {
  const char* name;
  std::function<void()> fn;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: unit_dispatcher <config-dir> <fixtures-dir>\n";
    return 2;
  }
  g_config = argv[1];
  g_fixtures = argv[2];

  const std::vector<Case> cases = {
      {"text profile plans two steps", test_text_profile_plans_two_steps},
      {"media never matches text", test_a_media_profile_never_matches_a_text_file},
      {"any matches binary", test_any_matches_a_binary_file},
      {"step without bin uses ziptastic_bin", test_a_step_without_bin_uses_ziptastic_bin},
      {"reverse honours reverse_args", test_reverse_uses_reverse_args_when_given},
      {"reverse defaults to d", test_reverse_defaults_to_the_d_verb},
      {"program availability", test_program_availability_is_checked},
      {"shell line quotes spaces", test_shell_line_quotes_spaces},
      {"single step writes to output", test_single_step_plan_writes_straight_to_output},
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

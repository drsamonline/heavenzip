// test_dispatcher.cpp — phase 2: the right profile, and the argv it would run.
#include <filesystem>
#include <fstream>
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

void test_text_profile_drops_the_unshippable_step() {
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "notes.txt.hzpkg");
  // `text` chains srep then ziptastic, and srep is "all rights reserved, not
  // redistributable in any form", so on any machine that follows BUNDLING.md the plan
  // is one step and there is a notice. A machine where the user has supplied srep
  // gets two steps and no notice; both are correct, and the invariants below hold
  // either way. Asserting `steps.size() == 2` made this test a statement about the
  // machine rather than about the planner.
  const bool have_srep = program_available("bin/srep.exe");
  CHECK(plan.steps.size() == (have_srep ? 2u : 1u));
  CHECK(plan.notices.size() == (have_srep ? 0u : 1u));
  CHECK(plan.found == FileClass::Text);
  CHECK(plan.output == g_fixtures / "notes.txt.hzpkg");
  CHECK(plan.manifest == g_fixtures / "notes.txt.hzpipeline");

  // The last step writes the archive and reads what the step before it produced —
  // including when the step before it was dropped.
  CHECK(plan.steps.back().step == "ziptastic");
  CHECK(plan.steps.back().program == "bin/ziptastic.exe");
  CHECK(plan.steps.back().argv.back() == plan.output.string());
  CHECK(plan.steps.back().argv[plan.steps.back().argv.size() - 2] ==
        (g_fixtures / "notes.txt").string());

  // Nothing the bundle is not allowed to carry survives into the plan. This is the
  // invariant that keeps the pre-flight from being the only thing between a profile
  // and a half-run chain. It is stated as "srep is gone", not as "every program
  // resolves": the engine's own path is relative to the directory heavenzip runs in,
  // and ctest runs this from the build directory.
  if (!have_srep) {
    for (const StepCommand& step : plan.steps) {
      CHECK(step.program.find("srep") == std::string::npos);
    }
    CHECK(plan.notices[0].find("srep") != std::string::npos);
  }
}

void test_a_step_carries_its_accepted_exit_codes() {
  // The manifest records these so the reverse pass applies the same rule backwards.
  const Plan plan = build_plan(master(), profile("max-deflate"), "max-deflate",
                               g_fixtures / "image.png", g_fixtures / "image.png.hzpkg");
  bool saw_precomp = false;
  for (const StepCommand& step : plan.steps) {
    if (step.step != "precomp") continue;
    saw_precomp = true;
    CHECK(step.ok_exit.size() == 2);
    CHECK(step.ok_exit[0] == 0);
    CHECK(step.ok_exit[1] == 2);
  }
  CHECK(saw_precomp);
  // And everything else keeps the default, so the rule stays per-step.
  for (const StepCommand& step : plan.steps) {
    if (step.step == "precomp") continue;
    CHECK(step.ok_exit.size() == 1);
    CHECK(step.ok_exit[0] == 0);
  }
}

void test_a_step_records_what_it_produced() {
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "notes.txt.hzpkg");
  // `produced` is what the reverse pass uses to place the payload of a stdout-writing
  // step that is not the last one. Step i produces what step i+1 consumes, and the
  // last step produces the archive.
  for (std::size_t i = 0; i + 1 < plan.steps.size(); ++i) {
    CHECK(plan.steps[i].produced ==
          plan.steps[i + 1].argv[plan.steps[i + 1].argv.size() - 2]);
  }
  CHECK(plan.steps.back().produced == plan.output.string());
}

void test_a_profile_whose_every_step_is_optional_and_missing_refuses() {
  // `strong` is paq8px alone, and paq8px is GPL-2+ and never linked in. Dropping the
  // only step leaves nothing to run, which is a refusal with the reason — not a
  // silent no-op and not a crash.
  bool threw = false;
  std::string why;
  try {
    build_plan(master(), profile("strong"), "strong", g_fixtures / "notes.txt",
               g_fixtures / "notes.txt.hzpkg");
  } catch (const std::runtime_error& error) {
    threw = true;
    why = error.what();
  }
  CHECK(threw);
  CHECK(why.find("cannot run") != std::string::npos);
  CHECK(why.find("paq8px") != std::string::npos);
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
  // `plain` declares `detect = any`, so it plans for a file nothing else claims.
  // This used `max-ratio`, whose every step is now declared optional — srep is
  // all-rights-reserved and paq8px is GPL-2+ — so on a machine without either it
  // correctly refuses instead of planning, and it stopped being a test of `detect`.
  const Plan plan = build_plan(master(), profile("plain"), "plain", g_fixtures / "random.bin",
                               g_fixtures / "random.bin.hzpkg");
  CHECK(plan.found == FileClass::Binary);
  CHECK(plan.steps.size() == 1);
  CHECK(plan.steps[0].step == "ziptastic");
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
  // The default applies to a step that does not set `reverse_args`. The `text`
  // profile's srep step sets `-d` now (srep has no `d` verb), so this checks the
  // ziptastic step — the one that still relies on the convention.
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "w.hzpkg");
  const auto& last = plan.steps.back();
  CHECK(last.step == "ziptastic");
  CHECK(last.reverse[1] == "d");
}

void test_reverse_args_override_the_default_verb() {
  // srep and paq8px both undo with `-d`, not `d`. A profile that says so must have
  // it honoured, or the archive cannot be reversed at all.
  const Plan plan = build_plan(master(), profile("text"), "text", g_fixtures / "notes.txt",
                               g_fixtures / "w.hzpkg");
  CHECK(plan.steps.front().step == "srep");
  CHECK(plan.steps.front().reverse[1] == "-d");
}

void test_program_availability_is_checked() {
#ifdef _WIN32
  CHECK(program_available("C:\\Windows\\System32\\cmd.exe"));
#else
  CHECK(program_available("/bin/sh"));
#endif
  CHECK(!program_available("/nonexistent/binary"));
  CHECK(!program_available("a-binary-that-does-not-exist-anywhere"));
}

void test_a_windows_shaped_profile_resolves_on_this_platform() {
  // The shipped profiles say `bin/srep.exe`; a Linux release has `bin/srep`. The
  // resolver must bridge that, or every profile is a refusal on one platform.
  //
  // Only ONE of the two names exists in each case, so the fallback is what is
  // being tested — with both present the configured name wins and the test would
  // pass without the resolver doing anything.
  const fs::path dir = fs::temp_directory_path() / "heavenzip-suffix-test";
  fs::remove_all(dir);
  fs::create_directories(dir);

#ifdef _WIN32
  const fs::path present = dir / "tool.exe";   // only the .exe exists
  { std::ofstream(present.string()) << "x"; }
  CHECK(resolve_program((dir / "tool").string()) == present.string());
  CHECK(program_available((dir / "tool").string()));
  CHECK(resolve_program(present.string()) == present.string());
#else
  const fs::path present = dir / "tool";       // only the bare name exists
  { std::ofstream(present.string()) << "x"; }
  CHECK(resolve_program((dir / "tool.exe").string()) == present.string());
  CHECK(program_available((dir / "tool.exe").string()));
  CHECK(resolve_program(present.string()) == present.string());
#endif

  // Nothing there is still nothing, in either spelling.
  CHECK(resolve_program((dir / "absent.exe").string()).empty());
  CHECK(resolve_program((dir / "absent").string()).empty());

  fs::remove_all(dir);
}

void test_a_plan_spawns_the_resolved_path() {
  const fs::path dir = fs::temp_directory_path() / "heavenzip-resolve-plan";
  fs::create_directories(dir);
  const fs::path bare = dir / "engine";
  { std::ofstream(bare.string()) << "x"; }

  const Profile custom =
      Profile::parse("[route]\ndetect = any\npipeline = only\n[only]\nbin = " +
                         (dir / "engine.exe").string() + "\nargs = -9\n",
                     "inline");
  const Plan plan = build_plan(master(), custom, "custom", g_fixtures / "notes.txt",
                               g_fixtures / "r.hzpkg");
  // argv[0] is what will be spawned; `program` stays as configured. The direction of
  // the suffix resolution is platform-dependent: `engine.exe` becomes `engine` on
  // POSIX, and a bare name becomes `engine.exe` on Windows.
#ifdef _WIN32
  CHECK(plan.steps[0].argv[0] == (dir / "engine.exe").string());
#else
  CHECK(plan.steps[0].argv[0] == bare.string());
#endif
  CHECK(plan.steps[0].program == (dir / "engine.exe").string());

  fs::remove_all(dir);
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
      {"text profile drops the unshippable step", test_text_profile_drops_the_unshippable_step},
      {"a step records what it produced", test_a_step_records_what_it_produced},
      {"a step carries its accepted exit codes", test_a_step_carries_its_accepted_exit_codes},
      {"all-optional-and-missing refuses", test_a_profile_whose_every_step_is_optional_and_missing_refuses},
      {"media never matches text", test_a_media_profile_never_matches_a_text_file},
      {"any matches binary", test_any_matches_a_binary_file},
      {"step without bin uses ziptastic_bin", test_a_step_without_bin_uses_ziptastic_bin},
      {"reverse honours reverse_args", test_reverse_uses_reverse_args_when_given},
      {"reverse defaults to d", test_reverse_defaults_to_the_d_verb},
      {"program availability", test_program_availability_is_checked},
      {"windows-shaped profile resolves", test_a_windows_shaped_profile_resolves_on_this_platform},
      {"plan spawns the resolved path", test_a_plan_spawns_the_resolved_path},
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

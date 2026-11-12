// test_detector.cpp — phase 1: every class recognised, and the rules that decide.
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "heavenzip/detector.hpp"

namespace fs = std::filesystem;
using namespace heavenzip;

namespace {

int g_failures = 0;
std::string g_current;
fs::path g_fixtures;

void check(bool condition, const std::string& what, int line) {
  if (condition) return;
  std::cerr << "FAIL " << g_current << " (line " << line << "): " << what << "\n";
  ++g_failures;
}

#define CHECK(expr) check((expr), #expr, __LINE__)

Detection classify(const std::string& name) { return detect_file(g_fixtures / name); }

void expect(const std::string& name, FileClass wanted, const std::string& why_part = {}) {
  const Detection found = classify(name);
  CHECK(found.value == wanted);
  if (found.value != wanted) {
    std::cerr << "     " << name << " -> " << to_string(found.value) << " (" << found.why
              << "), wanted " << to_string(wanted) << "\n";
  }
  if (!why_part.empty()) CHECK(found.why.find(why_part) != std::string::npos);
}

// --- one fixture per class -------------------------------------------------

void test_media() {
  expect("image.png", FileClass::Media, "png");
  expect("image.jpg", FileClass::Media, "jpeg");
  expect("image.gif", FileClass::Media, "gif");
  expect("audio.mp3", FileClass::Media, "mp3");
  expect("video.mp4", FileClass::Media, "mp4");
  expect("document.pdf", FileClass::Media, "pdf");
}

void test_archives() {
  expect("archive.zip", FileClass::Archive, "zip");
  expect("archive.7z", FileClass::Archive, "7z");
  expect("archive.gz", FileClass::Archive, "gzip");
  expect("archive.bz2", FileClass::Archive, "bzip2");
  expect("archive.xz", FileClass::Archive, "xz");
  expect("archive.tar", FileClass::Archive, "tar");
}

void test_text() {
  expect("notes.txt", FileClass::Text, "printable");
  // Valid UTF-8 with almost no ASCII is text, not binary.
  expect("hindi.txt", FileClass::Text, "utf-8");
}

void test_binary() {
  expect("random.bin", FileClass::Binary);
  expect("empty.bin", FileClass::Binary, "empty");
  expect("nul-inside.txt", FileClass::Binary, "NUL");
}

// --- the rules -------------------------------------------------------------

void test_extension_does_not_decide() {
  // A ZIP named like a text file is still an archive.
  const fs::path copy = g_fixtures / "disguised.txt";
  fs::copy_file(g_fixtures / "archive.zip", copy, fs::copy_options::overwrite_existing);
  const Detection found = detect_file(copy);
  CHECK(found.value == FileClass::Archive);
  fs::remove(copy);
}

void test_zip_is_an_archive_not_media() {
  // ZIP appears in both of the plan's tables; one class per file, and archive wins.
  CHECK(classify("archive.zip").value == FileClass::Archive);
}

void test_detect_any_matches_everything() {
  for (FileClass value : {FileClass::Text, FileClass::Media, FileClass::Archive, FileClass::Binary}) {
    CHECK(matches("any", value));
  }
}

void test_specific_detect_matches_only_its_class() {
  CHECK(matches("text", FileClass::Text));
  CHECK(!matches("text", FileClass::Media));
  CHECK(!matches("text", FileClass::Binary));
  CHECK(matches("archive", FileClass::Archive));
  CHECK(!matches("archive", FileClass::Text));
}

void test_unknown_detect_matches_nothing() {
  // A profile cannot name a class that does not exist; Profile validation rejects
  // it before this is reached, and the runtime rule refuses it too.
  CHECK(!matches("video", FileClass::Media));
}

void test_large_file_reads_only_the_window() {
  // 2 MiB of text, with a PNG header spliced at 100 KiB — beyond the 64 KiB magic
  // window. The detector must not see it, because it must not read the whole file.
  const fs::path path = g_fixtures / "big.txt";
  {
    std::ofstream out(path, std::ios::binary);
    std::string text(100 * 1024, 'a');
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.write("\x89PNG", 4);
    std::string tail(120 * 1024, 'b');
    out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
  }
  const Detection found = detect_file(path);
  CHECK(found.value == FileClass::Text);
  fs::remove(path);
}

void test_missing_file_throws() {
  bool threw = false;
  try {
    detect_file(g_fixtures / "no-such-fixture");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

struct Case {
  const char* name;
  std::function<void()> fn;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: unit_detector <fixtures-dir>\n";
    return 2;
  }
  g_fixtures = argv[1];

  const std::vector<Case> cases = {
      {"media fixtures", test_media},
      {"archive fixtures", test_archives},
      {"text fixtures", test_text},
      {"binary fixtures", test_binary},
      {"extension does not decide", test_extension_does_not_decide},
      {"zip is an archive", test_zip_is_an_archive_not_media},
      {"detect = any matches everything", test_detect_any_matches_everything},
      {"specific detect matches its class", test_specific_detect_matches_only_its_class},
      {"unknown detect matches nothing", test_unknown_detect_matches_nothing},
      {"large file reads only the window", test_large_file_reads_only_the_window},
      {"missing file throws", test_missing_file_throws},
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

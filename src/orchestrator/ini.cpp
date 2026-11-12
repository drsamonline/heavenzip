// ini.cpp — parse, validate and serialise the two kinds of INI this project has.
#include "heavenzip/ini.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace heavenzip {
namespace {

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// A `;` or `#` outside quotes starts a comment. Inside quotes it is data — a
// profile that passes `-x ";not a comment"` must keep it.
std::string strip_comment(const std::string& line) {
  bool in_double = false, in_single = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '\\' && in_double) { ++i; continue; }
    if (c == '"' && !in_single) { in_double = !in_double; continue; }
    if (c == '\'' && !in_double) { in_single = !in_single; continue; }
    if ((c == ';' || c == '#') && !in_double && !in_single) return line.substr(0, i);
  }
  return line;
}

std::string unquote(const std::string& value) {
  if (value.size() >= 2) {
    const char first = value.front(), last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      return value.substr(1, value.size() - 2);
    }
  }
  return value;
}

[[noreturn]] void fail(const std::string& where, const std::string& what) {
  throw IniError(where + ": " + what);
}

}  // namespace

bool IniSection::has(const std::string& key) const { return find(key) != nullptr; }

const std::string* IniSection::find(const std::string& key) const {
  for (const auto& [k, v] : entries) {
    if (k == key) return &v;
  }
  return nullptr;
}

const IniSection* IniFile::find(const std::string& name) const {
  for (const auto& section : sections) {
    if (section.name == name) return &section;
  }
  return nullptr;
}

IniFile IniFile::parse(const std::string& text, const std::string& name_for_errors) {
  IniFile file;
  file.path = name_for_errors;
  std::istringstream in(text);
  std::string raw;
  int line_no = 0;
  IniSection* current = nullptr;

  while (std::getline(in, raw)) {
    ++line_no;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    const std::string line = trim(strip_comment(raw));
    if (line.empty()) continue;
    const std::string where = name_for_errors + ":" + std::to_string(line_no);

    if (line.front() == '[') {
      if (line.back() != ']') fail(where, "section header is not closed with ']'");
      const std::string name = trim(line.substr(1, line.size() - 2));
      if (name.empty()) fail(where, "section header is empty");
      if (file.has(name)) fail(where, "duplicate section [" + name + "]");
      file.sections.push_back(IniSection{name, {}});
      current = &file.sections.back();
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) fail(where, "expected `key = value` or `[section]`, got `" + line + "`");
    if (current == nullptr) fail(where, "key outside any section");
    const std::string key = trim(line.substr(0, eq));
    if (key.empty()) fail(where, "key is empty");
    if (current->has(key)) fail(where, "duplicate key `" + key + "` in [" + current->name + "]");
    current->entries.emplace_back(key, unquote(trim(line.substr(eq + 1))));
  }
  return file;
}

IniFile IniFile::load(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw IniError(path.string() + ": cannot be opened");
  std::ostringstream buffer;
  buffer << in.rdbuf();
  IniFile file = parse(buffer.str(), path.string());
  file.path = path;
  return file;
}

std::string IniFile::dump() const {
  std::string out;
  for (const auto& section : sections) {
    if (!out.empty()) out += "\n";
    out += "[" + section.name + "]\n";
    for (const auto& [key, value] : section.entries) {
      out += key + " = " + value + "\n";
    }
  }
  return out;
}

std::vector<std::string> split_args(const std::string& value) {
  std::vector<std::string> tokens;
  std::string token;
  bool in_double = false, in_single = false, has_token = false;

  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (in_double) {
      if (c == '\\' && i + 1 < value.size()) { token += value[++i]; continue; }
      if (c == '"') { in_double = false; continue; }
      token += c;
      continue;
    }
    if (in_single) {
      if (c == '\'') { in_single = false; continue; }
      token += c;
      continue;
    }
    if (c == '"') { in_double = true; has_token = true; continue; }
    if (c == '\'') { in_single = true; has_token = true; continue; }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (has_token) { tokens.push_back(token); token.clear(); has_token = false; }
      continue;
    }
    token += c;
    has_token = true;
  }
  if (has_token) tokens.push_back(token);
  return tokens;
}

namespace {

const std::vector<std::string> kDetectValues = {"text", "media", "archive", "any"};

// "text, media , max-ratio" -> {"text","media","max-ratio"}, rejecting empties.
std::vector<std::string> split_list(const std::string& value, const std::string& where,
                                    const std::string& key) {
  std::vector<std::string> items;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    const std::string trimmed = trim(item);
    if (trimmed.empty()) fail(where, "`" + key + "` has an empty item");
    items.push_back(trimmed);
  }
  if (items.empty()) fail(where, "`" + key + "` is empty");
  return items;
}

std::string require(const IniSection& section, const std::string& key, const std::string& where) {
  const std::string* value = section.find(key);
  if (value == nullptr || value->empty()) {
    fail(where, "[" + section.name + "] is missing required key `" + key + "`");
  }
  return *value;
}

}  // namespace

MasterConfig MasterConfig::load(const fs::path& path) {
  const IniFile file = IniFile::load(path);
  const std::string where = path.string();

  const IniSection* general = file.find("general");
  if (general == nullptr) fail(where, "missing [general] section");
  const IniSection* profiles = file.find("profiles");
  if (profiles == nullptr) fail(where, "missing [profiles] section");

  MasterConfig config;
  config.path = path;
  config.version = require(*general, "version", where);
  config.log_level = require(*general, "log_level", where);
  config.ziptastic_bin = require(*general, "ziptastic_bin", where);
  config.default_profile = require(*profiles, "default", where);
  config.available = split_list(require(*profiles, "available", where), where, "available");

  if (std::find(config.available.begin(), config.available.end(), config.default_profile) ==
      config.available.end()) {
    fail(where, "default profile `" + config.default_profile + "` is not in `available`");
  }
  return config;
}

Profile Profile::load(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw IniError(path.string() + ": cannot be opened");
  std::ostringstream buffer;
  buffer << in.rdbuf();
  Profile profile = Profile::parse(buffer.str(), path.string());
  profile.path = path;
  return profile;
}

Profile Profile::parse(const std::string& text, const std::string& name_for_errors) {
  const IniFile file = IniFile::parse(text, name_for_errors);
  const std::string& where = name_for_errors;

  const IniSection* route = file.find("route");
  if (route == nullptr) fail(where, "missing required [route] section");

  Profile profile;
  profile.detect = lower(require(*route, "detect", where));
  if (std::find(kDetectValues.begin(), kDetectValues.end(), profile.detect) == kDetectValues.end()) {
    fail(where, "`detect = " + profile.detect + "` is not one of text, media, archive, any");
  }
  profile.pipeline = split_list(require(*route, "pipeline", where), where, "pipeline");

  for (const auto& name : profile.pipeline) {
    const IniSection* section = file.find(name);
    if (section == nullptr) {
      fail(where, "`pipeline` names step `" + name + "` but there is no [" + name + "] section");
    }
    Step step;
    step.name = name;
    if (const std::string* bin = section->find("bin"); bin != nullptr) step.bin = *bin;
    if (const std::string* args = section->find("args"); args != nullptr) step.args = *args;
    for (const auto& [key, value] : section->entries) {
      if (key != "bin" && key != "args") {
        fail(where, "[" + name + "] has unknown key `" + key + "` (only bin and args)");
      }
    }
    profile.steps.push_back(std::move(step));
  }

  // A section that is not the route and not a named step is dead weight: either a
  // typo in `pipeline` or a leftover. Say so rather than silently ignoring it.
  for (const auto& section : file.sections) {
    if (section.name == "route") continue;
    if (std::find(profile.pipeline.begin(), profile.pipeline.end(), section.name) ==
        profile.pipeline.end()) {
      fail(where, "[" + section.name + "] is not named in `pipeline`");
    }
  }
  return profile;
}

fs::path find_config_dir(const fs::path& argv0) {
  if (const char* env = std::getenv("HEAVENZIP_CONFIG"); env != nullptr && *env != '\0') {
    return env;
  }
  const fs::path cwd_config = "config";
  if (fs::is_directory(cwd_config)) return cwd_config;

  std::error_code error;
  const fs::path exe = fs::weakly_canonical(argv0, error);
  if (!error) {
    for (const fs::path& candidate : {exe.parent_path().parent_path() / "config",
                                      exe.parent_path() / "config"}) {
      if (fs::is_directory(candidate)) return candidate;
    }
  }
  return {};
}

}  // namespace heavenzip

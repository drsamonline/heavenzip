// ini.hpp — the config reader for heavenzip.
//
// One grammar, two readers: the master config (config/syc.ini) and one file per
// profile (config/profiles/<name>.ini). Both are the same INI: sections, `key =
// value`, `;` comments. Keeping one parser means a malformed profile and a
// malformed master config fail the same way, with a file and a line.
#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace heavenzip {

// A parse or validation failure. `where` is "file:line" when a line is known.
class IniError : public std::runtime_error {
 public:
  explicit IniError(const std::string& message) : std::runtime_error(message) {}
};

// Section keys keep their file order, because `pipeline = a,b,c` is order-bearing
// and a map would be a quiet lie about what the file said.
struct IniSection {
  std::string name;
  std::vector<std::pair<std::string, std::string>> entries;

  bool has(const std::string& key) const;
  const std::string* find(const std::string& key) const;
};

struct IniFile {
  std::filesystem::path path;
  std::vector<IniSection> sections;

  const IniSection* find(const std::string& name) const;
  bool has(const std::string& name) const { return find(name) != nullptr; }

  // Parse. Throws IniError naming file:line for anything malformed: a duplicate
  // section, a duplicate key, a line that is neither a section nor `key = value`.
  static IniFile load(const std::filesystem::path& path);
  static IniFile parse(const std::string& text, const std::string& name_for_errors);

  // Serialise back to INI text. Not byte-identical to the source (comments are
  // gone, spacing is normalised) but parse(dump(x)) == x, which is the property
  // the round-trip test asserts.
  std::string dump() const;
};

// One pipeline step: `[step1] bin = ... args = ...`.
struct Step {
  std::string name;
  std::string bin;          // empty means "use ziptastic_bin from [general]"
  std::string args;         // passed verbatim, split on whitespace
  std::string reverse_args; // optional; empty means the `<bin> d` convention
};

struct Profile {
  std::filesystem::path path;
  std::string detect;              // text | media | archive | any
  std::vector<std::string> pipeline;
  std::vector<Step> steps;         // in pipeline order

  static Profile load(const std::filesystem::path& path);

  // Same validation from text, so a profile can be tested without a file on disk.
  static Profile parse(const std::string& text, const std::string& name_for_errors);
};

// config/syc.ini
struct MasterConfig {
  std::filesystem::path path;
  std::string version;
  std::string log_level;
  std::string ziptastic_bin;
  std::string default_profile;
  std::vector<std::string> available;

  static MasterConfig load(const std::filesystem::path& path);
};

// Where the config lives, in order: $HEAVENZIP_CONFIG, ./config,
// <exe>/../config, <exe>/config. Returns empty if none exists.
std::filesystem::path find_config_dir(const std::filesystem::path& argv0);

// Split a value into argv tokens on whitespace, honouring "double" and 'single'
// quotes and backslash escapes inside double quotes.
std::vector<std::string> split_args(const std::string& value);

}  // namespace heavenzip

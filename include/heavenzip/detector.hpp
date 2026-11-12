// detector.hpp — what kind of file is this?
//
// AGENT_PROMPT.md section 5. Classification is by content, never by extension: a
// ZIP renamed to .txt is still an archive, and a profile that says `detect =
// media` must not fire on it. The read is bounded — 64 KiB for the magic window
// and 4 KiB for the text heuristic — because heavenzip may be pointed at a
// multi-gigabyte image.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace heavenzip {

enum class FileClass { Text, Media, Archive, Binary };

// The name a profile would write in `detect =`, and the name printed by
// `heavenzip detect`.
std::string to_string(FileClass value);

// Parse "text" / "media" / "archive" / "any". Returns false for anything else;
// `any` is a profile-level rule rather than a class, and is reported as Binary
// with matched=true by matches().
bool parse_class(const std::string& name, FileClass& out);

struct Detection {
  FileClass value = FileClass::Binary;
  std::string why;  // "png magic", "printable 99.8%", "no magic, 3% printable"
};

// Classify a buffer. `size` bytes of `data` are examined; the caller decides how
// much to read, but at least 512 bytes are needed to see the TAR marker at 257.
Detection detect_bytes(const std::uint8_t* data, std::size_t size);

// Classify a file. Reads at most 64 KiB; throws std::runtime_error if the file
// cannot be opened.
Detection detect_file(const std::filesystem::path& path);

// The rule for a profile's `detect =` value: `any` always matches, otherwise the
// class must equal the named one. `binary` is a class a profile may name, even
// though the grammar's table lists only text/media/archive/any.
bool matches(const std::string& detect, FileClass actual);

}  // namespace heavenzip

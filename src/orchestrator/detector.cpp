// detector.cpp — magic bytes, then a printable-ratio test, then the truth.
//
// Content, never extension: a ZIP renamed to .txt is still an archive, and a
// profile that says `detect = media` must not fire on it.
#include "heavenzip/detector.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace heavenzip {
namespace {

constexpr std::size_t kMagicWindow = 64 * 1024;
constexpr std::size_t kTextWindow = 4 * 1024;

bool starts_with(const std::uint8_t* data, std::size_t size, const char* magic,
                 std::size_t magic_size) {
  return size >= magic_size &&
         std::equal(magic, magic + magic_size, reinterpret_cast<const char*>(data));
}

bool has_at(const std::uint8_t* data, std::size_t size, std::size_t offset, const char* magic,
            std::size_t magic_size) {
  return size >= offset + magic_size &&
         std::equal(magic, magic + magic_size, reinterpret_cast<const char*>(data + offset));
}

// Archive first, then media: a ZIP appears in both of the plan's tables, and one
// class per file is the whole point. ZIP is an archive; PDF, which appears only in
// the media table, is media.
const char* archive_magic(const std::uint8_t* data, std::size_t size) {
  if (starts_with(data, size, "PK\x03\x04", 4)) return "zip";
  if (starts_with(data, size, "\x37\x7A\xBC\xAF", 4)) return "7z";
  if (starts_with(data, size, "\x1F\x8B", 2)) return "gzip";
  if (starts_with(data, size, "BZh", 3)) return "bzip2";
  if (starts_with(data, size, "\xFD\x37\x7A\x58", 4)) return "xz";
  if (has_at(data, size, 257, "ustar", 5)) return "tar";
  return nullptr;
}

const char* media_magic(const std::uint8_t* data, std::size_t size) {
  if (starts_with(data, size, "\xFF\xD8\xFF", 3)) return "jpeg";
  if (starts_with(data, size, "\x89PNG", 4)) return "png";
  if (starts_with(data, size, "GIF8", 4)) return "gif";
  if (starts_with(data, size, "ID3", 3)) return "mp3";
  if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) return "mp3";
  if (has_at(data, size, 4, "ftyp", 4)) return "mp4";
  if (starts_with(data, size, "%PDF", 4)) return "pdf";
  return nullptr;
}

bool is_valid_utf8(const std::uint8_t* data, std::size_t size) {
  std::size_t i = 0;
  bool saw_multibyte = false;
  while (i < size) {
    const std::uint8_t byte = data[i];
    if (byte < 0x80) {
      ++i;
      continue;
    }
    std::size_t extra = 0;
    std::uint32_t code = 0;
    if ((byte & 0xE0) == 0xC0) {
      extra = 1;
      code = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
      extra = 2;
      code = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
      extra = 3;
      code = byte & 0x07;
    } else {
      return false;  // continuation byte or invalid lead
    }
    if (i + extra >= size) return false;  // truncated sequence at the window edge
    for (std::size_t k = 1; k <= extra; ++k) {
      if ((data[i + k] & 0xC0) != 0x80) return false;
      code = (code << 6) | (data[i + k] & 0x3F);
    }
    // Reject overlong encodings and surrogates: a sequence that decodes to these
    // is not text, it is a binary file that happens to look structured.
    if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
        (extra == 3 && code < 0x10000))
      return false;
    if (code >= 0xD800 && code <= 0xDFFF) return false;
    if (code > 0x10FFFF) return false;
    saw_multibyte = true;
    i += extra + 1;
  }
  return saw_multibyte;
}

bool looks_like_text(const std::uint8_t* data, std::size_t size, std::string& why) {
  if (size == 0) {
    why = "empty";
    return false;
  }
  const std::size_t window = std::min(size, kTextWindow);
  if (std::find(data, data + window, static_cast<std::uint8_t>(0)) != data + window) {
    why = "contains a NUL byte";
    return false;
  }

  std::size_t printable = 0;
  for (std::size_t i = 0; i < window; ++i) {
    const std::uint8_t byte = data[i];
    if ((byte >= 0x20 && byte < 0x7F) || byte == '\n' || byte == '\r' || byte == '\t' || byte == 0x0C)
      ++printable;
  }
  const double ratio = static_cast<double>(printable) / static_cast<double>(window);

  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "printable %.1f%%", ratio * 100.0);

  if (ratio >= 0.95) {
    why = buffer;
    return true;
  }
  // Below 95% ASCII, valid UTF-8 still counts as text: prose in Devanagari or
  // Cyrillic has almost no ASCII in it.
  if (is_valid_utf8(data, window)) {
    why = std::string(buffer) + ", valid utf-8";
    return true;
  }
  why = buffer;
  return false;
}

}  // namespace

std::string to_string(FileClass value) {
  switch (value) {
    case FileClass::Text: return "text";
    case FileClass::Media: return "media";
    case FileClass::Archive: return "archive";
    case FileClass::Binary: return "binary";
  }
  return "binary";
}

bool parse_class(const std::string& name, FileClass& out) {
  if (name == "text") { out = FileClass::Text; return true; }
  if (name == "media") { out = FileClass::Media; return true; }
  if (name == "archive") { out = FileClass::Archive; return true; }
  if (name == "binary") { out = FileClass::Binary; return true; }
  return false;
}

Detection detect_bytes(const std::uint8_t* data, std::size_t size) {
  if (const char* magic = archive_magic(data, size); magic != nullptr) {
    return {FileClass::Archive, std::string(magic) + " magic"};
  }
  if (const char* magic = media_magic(data, size); magic != nullptr) {
    return {FileClass::Media, std::string(magic) + " magic"};
  }
  std::string why;
  if (looks_like_text(data, size, why)) return {FileClass::Text, why};
  return {FileClass::Binary, why.empty() ? "no magic, not text" : "no magic, " + why};
}

Detection detect_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error(path.string() + ": cannot be opened");
  std::vector<std::uint8_t> buffer(kMagicWindow);
  in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
  const std::size_t read = static_cast<std::size_t>(in.gcount());
  buffer.resize(read);
  return detect_bytes(buffer.data(), buffer.size());
}

bool matches(const std::string& detect, FileClass actual) {
  if (detect == "any") return true;
  FileClass wanted{};
  if (!parse_class(detect, wanted)) return false;
  return wanted == actual;
}

}  // namespace heavenzip

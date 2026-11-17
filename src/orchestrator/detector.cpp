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

std::uint16_t rd16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t rd32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool fourcc(const std::uint8_t* p, const char* tag) {
  return std::equal(tag, tag + 4, reinterpret_cast<const char*>(p));
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

// WAVE — a class, not a container check.
//
// `RIFF....WAVE` alone would put an A-law or ADPCM file in this class, and the route
// it feeds is a delta filter with a 2-byte stride: right for 16-bit PCM, wrong for
// 8-bit companded audio. So the class asks for a `fmt ` chunk whose sample format the
// engine has a route for — integer PCM at 16/24/32 bits, or 32-bit IEEE float — with
// 1–8 channels, and a `data` chunk to follow.
//
// The chunk walk is bounded by the detector's own window, so a `data` chunk whose
// declared size runs past the end of it is the normal case, not a malformed file:
// every WAV over 64 KiB looks exactly like that from here. Only the headers are read.
std::string wave_class(const std::uint8_t* data, std::size_t size, std::string& declined) {
  const bool riff = starts_with(data, size, "RIFF", 4);
  const bool rf64 = starts_with(data, size, "RF64", 4);  // the >4 GiB form of the same container
  if (!riff && !rf64) return {};
  if (!has_at(data, size, 8, "WAVE", 4)) {
    declined = "RIFF container, but not WAVE";
    return {};
  }

  bool have_fmt = false;
  bool have_data = false;
  std::string shape;
  std::size_t p = 12;
  while (p + 8 <= size) {
    const std::uint32_t sz = rd32(data + p + 4);
    const std::size_t body = p + 8;
    if (fourcc(data + p, "fmt ")) {
      if (sz < 16 || body + 16 > size) return {};
      const std::uint16_t format = rd16(data + body);
      const std::uint16_t channels = rd16(data + body + 2);
      const std::uint16_t bits = rd16(data + body + 14);
      const bool pcm = format == 1 && (bits == 16 || bits == 24 || bits == 32);
      const bool ieee = format == 3 && bits == 32;
      // A WAVE file, but not one this engine has a route for. The reason is kept:
      // `detect` prints it, and "no magic" for a file that plainly has magic is the
      // kind of message that sends the next session looking in the wrong place.
      if (!pcm && !ieee) {
        char note[96];
        std::snprintf(note, sizeof note, "wav magic, format %u at %u bits has no route",
                      static_cast<unsigned>(format), static_cast<unsigned>(bits));
        declined = note;
        return {};
      }
      if (channels == 0 || channels > 8) {
        declined = "wav magic, " + std::to_string(channels) + " channels is out of range";
        return {};
      }
      char buffer[64];
      std::snprintf(buffer, sizeof buffer, "wav magic, %u-bit %s, %u ch",
                    static_cast<unsigned>(bits), ieee ? "float" : "PCM",
                    static_cast<unsigned>(channels));
      shape = buffer;
      have_fmt = true;
    } else if (fourcc(data + p, "data")) {
      have_data = true;
      break;
    }
    if (sz > size - body) break;  // the next header is past the window
    p = body + sz + (sz & 1);     // chunks are word-aligned
  }
  if (!have_fmt) {
    declined = "wav magic, no fmt chunk in the window";
    return {};
  }
  if (!have_data) {
    declined = "wav magic, no data chunk in the window";
    return {};
  }
  return shape;
}

// BMP — `BM` plus a DIB header size that is one of the seven Microsoft has shipped,
// and BI_RGB. The class exists because uncompressed pixels are what the colour-plane
// split and row delta need; an RLE or BITFIELDS bitmap is a different problem and is
// not claimed. BITMAPCOREHEADER (12) has no compression field — it is BI_RGB by
// definition.
std::string bmp_class(const std::uint8_t* data, std::size_t size, std::string& declined) {
  if (!starts_with(data, size, "BM", 2)) return {};
  if (size < 30) {
    declined = "bmp magic, truncated before the DIB header";
    return {};
  }
  const std::uint32_t dib = rd32(data + 14);
  const bool known = dib == 12 || dib == 40 || dib == 52 || dib == 56 || dib == 64 ||
                     dib == 108 || dib == 124;
  if (!known) {
    declined = "bmp magic, DIB header size " + std::to_string(dib) + " is not one Microsoft ships";
    return {};
  }
  if (dib >= 40) {
    if (size < 34) {
      declined = "bmp magic, truncated before the compression field";
      return {};
    }
    if (rd32(data + 30) != 0) {  // BI_RGB only
      declined = "bmp magic, compression " + std::to_string(rd32(data + 30)) +
                 " is not BI_RGB (no colour planes to split)";
      return {};
    }
  }
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "bmp magic, DIB %u", static_cast<unsigned>(dib));
  return buffer;
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
    case FileClass::Wav: return "wav";
    case FileClass::Bmp: return "bmp";
    case FileClass::Archive: return "archive";
    case FileClass::Binary: return "binary";
  }
  return "binary";
}

bool parse_class(const std::string& name, FileClass& out) {
  if (name == "text") { out = FileClass::Text; return true; }
  if (name == "media") { out = FileClass::Media; return true; }
  if (name == "wav") { out = FileClass::Wav; return true; }
  if (name == "bmp") { out = FileClass::Bmp; return true; }
  if (name == "archive") { out = FileClass::Archive; return true; }
  if (name == "binary") { out = FileClass::Binary; return true; }
  return false;
}

Detection detect_bytes(const std::uint8_t* data, std::size_t size) {
  if (const char* magic = archive_magic(data, size); magic != nullptr) {
    return {FileClass::Archive, std::string(magic) + " magic"};
  }
  // WAVE and BMP are their own classes rather than media: the routes they want are
  // the delta filter and the colour-plane split, not the container recompression the
  // media route is for.
  std::string declined;  // why a container matched but its class did not
  if (std::string why = wave_class(data, size, declined); !why.empty()) {
    return {FileClass::Wav, why};
  }
  if (std::string why = bmp_class(data, size, declined); !why.empty()) {
    return {FileClass::Bmp, why};
  }
  if (const char* magic = media_magic(data, size); magic != nullptr) {
    return {FileClass::Media, std::string(magic) + " magic"};
  }
  std::string why;
  if (looks_like_text(data, size, why)) return {FileClass::Text, why};
  if (!declined.empty()) return {FileClass::Binary, declined};
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

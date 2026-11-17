#!/usr/bin/env python3
"""make_fixtures.py — the bytes each detector class must be recognised by.

Small and generated rather than downloaded: a fixture nobody can explain is a test
that fails for reasons nobody can explain. Run it to regenerate; the files are
committed so the C++ test needs no Python.
"""
import pathlib
import struct

HERE = pathlib.Path(__file__).resolve().parent


def write(name: str, data: bytes) -> None:
    (HERE / name).write_bytes(data)


def main() -> None:
    # --- media ---
    write("image.png", b"\x89PNG\r\n\x1a\n" + b"\x00" * 120)
    write("image.jpg", b"\xff\xd8\xff\xe0\x00\x10JFIF\x00" + b"\x00" * 120)
    write("image.gif", b"GIF89a" + b"\x00" * 120)
    write("audio.mp3", b"ID3\x04\x00\x00\x00\x00\x00\x00" + b"\x00" * 120)
    write("video.mp4", b"\x00\x00\x00\x18ftypisom" + b"\x00" * 120)
    write("document.pdf", b"%PDF-1.7\n1 0 obj\n" + b"\x00" * 120)

    # --- wav and bmp: their own classes, not media ---
    # 16-bit PCM stereo, 8 frames. RIFF/WAVE, a 16-byte `fmt ` chunk and a `data`
    # chunk: the exact shape the audio route (xz --delta=dist=2) is written for.
    samples = struct.pack("<8h", 0, 100, 200, 300, 400, 500, 600, 700)
    fmt = struct.pack("<HHIIHH", 1, 2, 8000, 32000, 4, 16)
    write("tone.wav",
          b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(samples)) + b"WAVE" +
          b"fmt " + struct.pack("<I", len(fmt)) + fmt +
          b"data" + struct.pack("<I", len(samples)) + samples)
    # The same container with format tag 6 (A-law): a WAVE file, but not one the
    # delta route can help, so it must stay `binary`.
    alaw = bytearray((pathlib.Path(HERE) / "tone.wav").read_bytes())
    alaw[20:22] = struct.pack("<H", 6)
    write("alaw.wav", bytes(alaw))
    # Uncompressed 2x2 24-bit BMP: BM, 14-byte file header, 40-byte BITMAPINFOHEADER,
    # BI_RGB, four pixels.
    dib = struct.pack("<IiiHHIIiiII", 40, 2, 2, 1, 24, 0, 16, 2835, 2835, 0, 0)
    pixels = b"\x00\x00\xff" * 2 + b"\xff\x00\x00" * 2
    write("image.bmp", b"BM" + struct.pack("<IHHI", 14 + len(dib) + len(pixels), 0, 0,
                                           14 + len(dib)) + dib + pixels)
    # BI_RLE8: a bitmap, but not one the colour-plane split can claim.
    rle = bytearray((pathlib.Path(HERE) / "image.bmp").read_bytes())
    rle[30:34] = struct.pack("<I", 1)
    write("rle.bmp", bytes(rle))

    # --- archives ---
    write("archive.zip", b"PK\x03\x04\x14\x00\x00\x00" + b"\x00" * 120)
    write("archive.7z", b"7z\xbc\xaf\x27\x1c" + b"\x00" * 120)
    write("archive.gz", b"\x1f\x8b\x08\x00" + b"\x00" * 120)
    write("archive.bz2", b"BZh9" + b"\x00" * 120)
    write("archive.xz", b"\xfd7zXZ\x00" + b"\x00" * 120)
    tar = bytearray(600)
    tar[257:262] = b"ustar"
    write("archive.tar", bytes(tar))

    # --- text ---
    write("notes.txt", (b"the pipeline reads this line and the next one\n" * 40))
    # Devanagari: almost no ASCII, valid UTF-8, plainly text.
    write("hindi.txt", ("यह पंक्ति पढ़ी जाती है\n" * 20).encode("utf-8"))

    # --- binary ---
    write("random.bin", bytes((i * 37 + 11) % 256 for i in range(4096)))
    write("empty.bin", b"")

    # A NUL in the first 4 KiB is binary even when the rest is ASCII.
    write("nul-inside.txt", b"looks like text" + b"\x00" * 8 + b"but is not")

    print("wrote", len(list(HERE.glob("*.*"))), "fixtures")


if __name__ == "__main__":
    main()

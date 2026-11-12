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

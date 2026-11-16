# Changelog

All notable changes to heavenzip. The version here is the version the binary
reports, and the release tag must match it — `v1.0.0` carries a binary that says
`1.0.0`, not `0.1.0`.

## 1.0.0 — 2026-11-11

The first release that is a complete, self-contained bundle rather than a binary.

**Compression**

- Five tools ship in one archive: `ziptastic`, `paq8px`, `srep`, `brotli`, and the
  orchestrator itself. Every one is exercised by a round trip before the bundle is
  published.
- Nine profiles: `plain`, `strong`, `text`, `max-ratio`, `brotli`, `zstd`, `xz`,
  `gzip`, `zpaq`. The last four use tools already on the machine; `media` (preflate)
  refuses on Linux and is built for Windows separately.
- `compress` verifies by default: every archive is decompressed and compared before
  the command reports success. `--no-verify` turns that off.

**Step model**

- Three ways to describe a tool: appended arguments, `{in}`/`{out}` placeholders, and
  `stdout_to_output` for the tools that write their result to standard output.
- `reverse_args` and `reverse_out_index` describe how a tool restores, and the
  manifest records both, so decompression needs no config.

**Autotune**

- `heavenzip autotune FILE` ranks candidates by measured ratio and speed, verifies each
  by reversing it, and writes `best-ratio.ini`, `best-speed.ini` and
  `best-balanced.ini`.

**Windows**

- The orchestrator builds and its tests pass under MSVC. preflate is built for Windows
  as a separate artefact, with an adapter that gives it the one-file-in, one-file-out
  shape a step needs.

**Known limits**

- `srep` carries no licence statement anywhere in its tree. It ships as a binary with
  its source and a NOTICE saying nothing is granted.
- `srep` can die under memory pressure; the failure is reported as a crash with a
  signal name rather than an exit code.
- preflate does not compile on POSIX — `_ftelli64` and `_fseeki64` are MSVC-only.

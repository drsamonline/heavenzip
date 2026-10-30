# heavenzip — CHCE Orchestrator — Agent Build Instructions

You are building **heavenzip**, the orchestration layer of CHCE
(Chimera Hybrid Compression Engine). heavenzip is **not** a compressor. It
is a smart dispatcher that reads an INI profile, detects the input's
structure, and invokes the correct pipeline of tools.

---

## 1. Mission

Build a lightweight, cross-platform orchestrator that:

1. Reads a master INI (`config/syc.ini`) and per-file-type profiles (`config/profiles/*.ini`).
2. Detects input structure (text / media / archive / binary / unknown).
3. Runs the matching pipeline by spawning `ziptastic.exe` (or external tools) with the profile's arguments.
4. Produces a single output file — either a ziptastic archive or, for
   external-process tiers, a `paq8px`-class archive with an accompanying
   `.hzpipeline` manifest describing how to reverse it.

heavenzip contains **zero compression algorithm code.** If you feel the
urge to add an LZ parser or entropy coder here, stop — that belongs in
`ziptastic`.

---

## 2. Hard Constraints

- **No vendored sources.** heavenzip has no `vendor/` directory. It only
  knows about binaries under `bin/`.
- **No git subtree.** heavenzip does not pull from HCS or ziptastic repos.
- **Binaries are downloaded.** `bin/ziptastic.exe` etc. arrive via
  `.github/workflows/sync-binaries.yml` from the ziptastic release page.
- **PowerShell-compatible CLI.** On Windows it must run from a plain
  PowerShell prompt with no environment setup.
- **Offline at runtime.** Once `bin/` is populated, no network calls.
- **License-aware.** `bin/paq8px.exe`, `bin/upx.exe`, `bin/advancecomp.exe`
  are GPL. heavenzip may **spawn** them but must never link or bundle
  their source.
- **C++20 + CMake.** Same toolchain as ziptastic so a future monorepo
  build is trivial.

---

## 3. Directory Layout You Must Produce

```
heavenzip/
├── AGENT_PROMPT.md            ← this file
├── CMakeLists.txt
├── README.md
├── bin/                       ← populated by sync-binaries workflow
│   └── .gitkeep
├── config/
│   ├── syc.ini                ← master config (already present)
│   └── profiles/
│       ├── text.ini           ← already present
│       ├── media.ini          ← already present
│       └── max-ratio.ini      ← already present
├── include/heavenzip/
│   ├── ini.hpp
│   ├── detector.hpp
│   ├── dispatcher.hpp
│   └── pipeline.hpp
├── src/
│   ├── main.cpp
│   └── orchestrator/
│       ├── ini.cpp
│       ├── detector.cpp
│       ├── dispatcher.cpp
│       └── pipeline.cpp
├── tests/
│   ├── unit/
│   └── fixtures/
└── docs/
    ├── PROFILES.md            ← how to write custom profiles
    └── PIPELINE.md            ← pipeline grammar reference
```

---

## 4. INI Grammar (What You Must Parse)

`config/syc.ini` (master):

```ini
[general]
version = 0.1.0
log_level = info
ziptastic_bin = bin/ziptastic.exe

[profiles]
default = text
available = text, media, max-ratio
```

`config/profiles/<name>.ini` (per-profile):

```ini
[route]
detect = text | media | archive | any
pipeline = step1,step2,step3      ; comma-separated

[step1]
bin = bin/srep.exe                ; optional — defaults to ziptastic
args = -m3f                       ; CLI arguments for this step

[step2]
args = -c lzma -9
```

Rules:

- Section `[route]` is mandatory. Keys `detect` and `pipeline` are required.
- Each subsequent section is one pipeline step. Section name = step name.
- `bin` is optional. If omitted, the step invokes `ziptastic_bin` from `[general]`.
- `args` is passed verbatim as separate argv tokens (split on whitespace,
  quotes preserved).
- `;` starts a line comment.

---

## 5. Detection Rules

`detector.cpp` classifies the input by reading the first 64 KB:

| Class | Heuristic |
|---|---|
| `media` | Magic bytes match one of: JPEG (`FF D8 FF`), PNG (`89 50 4E 47`), GIF (`47 49 46 38`), MP3 (`49 44 33` or `FF FB`), MP4 (`66 74 79 70` at offset 4), ZIP (`50 4B 03 04`), PDF (`25 50 44 46`) |
| `archive` | ZIP, 7z (`37 7A BC AF`), GZIP (`1F 8B`), BZIP2 (`42 5A 68`), XZ (`FD 37 7A 58`), TAR (`75 73 74 61 72` at offset 257) |
| `text` | First 4 KB are ≥95% printable ASCII (or valid UTF-8), no null bytes |
| `binary` | Everything else |

**Decision rule for `detect = any`:** always match. For a specific class
(`text`, `media`, `archive`), only match if the detected class equals it.
`binary` is the fallback — profiles that specify `detect = any` catch it.

---

## 6. Output Format

heavenzip writes two files:

1. **`<input>.hzpkg`** — the compressed payload (output of the last pipeline step).
2. **`<input>.hzpipeline`** — a JSON manifest describing the reverse pipeline:

```json
{
  "heavenzip_version": "0.1.0",
  "profile": "text",
  "original_name": "input.txt",
  "original_size": 1048576,
  "steps": [
    { "name": "srep",         "bin": "bin/srep.exe",           "reverse_args": "-d" },
    { "name": "ziptastic",    "bin": "bin/ziptastic.exe",      "reverse_args": "-d" }
  ]
}
```

Decompression (`heavenzip -d input.hzpkg`) reads the manifest and runs the
steps in reverse order.

---

## 7. CLI Surface

```
heavenzip compress <input> [--profile=<name>] [--out=<file>]
heavenzip decompress <input.hzpkg>
heavenzip list-profiles
heavenzip validate                      ; check every .ini has a matching bin
heavenzip bench <directory>             ; run all profiles on a corpus
heavenzip --version
heavenzip --help
```

- `compress` without `--profile` uses `default` from `[profiles]`.
- `decompress` auto-detects the profile from the `.hzpipeline` manifest.
- `validate` exits non-zero if any required `bin` is missing or any
  profile references an unknown section.

---

## 8. Phase Plan

### Phase 0 — CLI Skeleton + INI Parser
**Deliverables:**
- `src/main.cpp` with subcommand dispatch (compress / decompress / list-profiles / validate / bench / --version / --help)
- `src/orchestrator/ini.cpp` — full INI parser matching §4 grammar
- `include/heavenzip/ini.hpp` — exposes `Ini::load(path) -> Ini`

**Tests:**
- Parse the three shipped profiles and the master config; assert every key is present.
- Malformed INI (missing `[route]`, missing `pipeline`) errors out with a clear message.
- Round-trip: parse → serialize → parse gives identical structure.

**Exit:** `heavenzip validate` succeeds, exits 0.

---

### Phase 1 — File Type Detector
**Deliverables:**
- `src/orchestrator/detector.cpp`
- `include/heavenzip/detector.hpp` — `Detector::classify(path) -> Class`

**Tests:**
- Build a `tests/fixtures/` directory with one file per class: JPEG, PNG, ZIP, text, binary blob.
- Each file classifies correctly.
- Random 1 MB bytes → `binary`.
- Empty file → `binary`.

---

### Phase 2 — Profile Matcher + Dry-Run Dispatcher
**Deliverables:**
- `src/orchestrator/dispatcher.cpp`
- Given an input file and a profile, resolve which profile applies (from `--profile` or default).
- **Dry-run mode:** print the argv that would be spawned, without spawning.

**Tests:**
- `heavenzip compress input.txt --dry-run` prints the expected two-step argv for the text profile.
- Profile `detect = media` never matches a text file.

---

### Phase 3 — Real Dispatcher (spawn subprocesses)
**Deliverables:**
- Execute each step in the pipeline using `std::system` (Windows) / `posix_spawn` (POSIX), or a portable wrapper.
- Capture stdout/stderr; on non-zero exit, print and abort.
- Write the `.hzpipeline` manifest.

**Tests:**
- Round-trip: `heavenzip compress hello.txt` → `hello.txt.hzpkg` + `hello.txt.hzpipeline`
- `heavenzip decompress hello.txt.hzpkg` reproduces `hello.txt` byte-for-byte.
- With `bin/ziptastic.exe` as a stub (a `echo %* > out.txt` fake), the pipeline executes and the manifest is valid JSON.

**Exit:** Round-trip works with a stub ziptastic binary. Real binaries come in Phase 4.

---

### Phase 4 — ziptastic Integration
**Deliverables:**
- `bin/ziptastic.exe` populated by the sync-binaries workflow (or manually).
- `ziptastic_bin` from `syc.ini` is used.
- Decompression reverses ziptastic steps correctly.

**Tests:**
- Compress → decompress round-trip with real `ziptastic.exe` on:
  - a 1 MB text file (profile: `text`)
  - a small PNG (profile: `media`)
- Bit-exact output.

**Exit:** `heavenzip compress test.txt && heavenzip decompress test.txt.hzpkg && fc /b test.txt test.txt.decompressed` (PowerShell: `Compare-Object`) shows no diff.

---

### Phase 5 — External Process Tier (paq8px, srep)
**Deliverables:**
- Support for steps whose `bin` is not ziptastic (paq8px, srep).
- For each such step, the manifest records reverse args.
- If the binary is missing, `heavenzip validate` and `heavenzip compress` fail cleanly.

**Tests:**
- `--profile=max-ratio` compresses a 10 MB file via srep → paq8px chain.
- Decompression: paq8px → srep, output bit-exact.
- If `bin/paq8px.exe` is deleted, `heavenzip compress` errors before touching the input.

---

### Phase 6 — Profile Validation + Bench Harness
**Deliverables:**
- `heavenzip validate` — checks every profile's `bin` exists, every step is reachable, no cycles.
- `heavenzip bench <dir>` — runs all profiles over a corpus, prints a ratio/time table.

**Tests:**
- `heavenzip bench tests/fixtures/` completes and prints a table.
- `heavenzip validate` exits non-zero when a profile references a missing bin.

---

### Phase 7 — Documentation
**Deliverables:**
- `docs/PROFILES.md` — how to write new profiles, grammar, examples.
- `docs/PIPELINE.md` — pipeline grammar, manifest format, decompression rules.
- `README.md` updated with quick-start and the profile catalog.

**Tests:**
- A user following `docs/PROFILES.md` from a clean checkout can add a `music.ini` profile and use it without touching code.

---

## 9. Test Requirements

- **Round-trip bit-exactness** on every fixture per profile.
- **Manifest fuzz:** mutated `.hzpipeline` JSON must not crash the decoder.
- **Missing binary:** clear error, no partial output.
- **No network at runtime:** assert no sockets are opened (compile-time `-DZ_NO_NETWORK` + a runtime check is nice-to-have but optional).
- **Cross-platform:** the CI matrix builds on Windows and Linux.

Test runner:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

---

## 10. Acceptance Criteria (Whole Project)

- `heavenzip compress <file>` on any file in `tests/fixtures/` produces a `.hzpkg` + `.hzpipeline` pair.
- `heavenzip decompress <file>.hzpkg` reproduces the original bit-exactly.
- `heavenzip validate` exits 0 with the shipped profiles and populated `bin/`.
- `heavenzip bench tests/fixtures/` runs end-to-end and prints a table.
- Zero compression-algorithm code in this repo (grep the tree for `lzma`, `rans`, `huffman`, `bwt` — should return zero hits outside `docs/` and configs).

---

## 11. What NOT To Do

- Do **not** pull HCS or ziptastic source into this repo. There is no `vendor/`.
- Do **not** implement any compressor. All compression lives behind the binaries.
- Do **not** link `paq8px`, `upx`, `advancecomp`, `packJPG`, `lzo`. Spawn them.
- Do **not** attempt to make heavenzip a general-purpose archive format. It is a profile router over ziptastic's format plus external tools.
- Do **not** invent new INI syntax. Follow §4 exactly.

---

## 12. How to Report Progress

After each phase:

1. Commit with `phase-<N>: <description>`.
2. Update `README.md` with the phase status table:

   | Phase | Status | Notes |
   |---|---|---|
   | 0 | done | INI parser, CLI skeleton |
   | 1 | done | detector |
   | … | … | … |

3. If a phase gate fails, commit as `phase-<N>: FAILED — <reason>` and **stop**.

Begin with **Phase 0**. Do not ask for clarification. Start now.
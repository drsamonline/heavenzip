# CHCE — Project Overview

Chimera Hybrid Compression Engine. The family that compresses, orchestrates and reads
the `.dr` format.

This document is shared, byte for byte, by every repository in the family. It is the
architecture, the rules and the boundaries. If a repository's copy differs from this
one, the copy is wrong.

---

## 1. What CHCE is

CHCE is a compression architecture in layers, not a single program.

- **A format.** `CHCE`, version 8, extension `.dr`. Per block, nine encoders compete and
  the smallest wins; eighteen Layer 0 transforms sit above them. Every block carries its
  own SHA-256, the stream carries a manifest digest, and a v8 trailer carries a block
  offset table so a block can be reached without walking the stream.
- **An engine.** `ziptastic`, a native C++20 binary, linked — never shelling out to
  another compressor.
- **An orchestrator.** `heavenzip`, which spawns prebuilt binaries, including GPL ones,
  and never links them. Process boundaries are the safety mechanism.
- **A source mirror.** `HCS`, the reference sources the engines are built from, treated
  as read-only.
- **A workbench.** `kosh`, which splits, squeezes, archives, verifies and inspects files.
- **A text canonicaliser.** `lexora`, which canonicalises text and then compresses it with
  guaranteed byte-exact recovery.
- **A rule set.** `compfactory`, which syncs the repos and builds them.
- **A front door.** `smlz`, which packs and unpacks ordinary archives in a tab.
- **A reader.** `a2z`, which parses a `.dr` file, verifies it, and says exactly what it
  did and did not prove.

The claim the family makes is narrow and testable: format-aware transforms under a
strong backend, byte-exact round trips, and no result reported as a win unless it is
smaller. `ziptastic`'s `docs/FINDINGS.md` carries the numbers, including the rows where
it loses.

---

## 2. The family at a glance

| Repo | Role | Language | Licence | Produces |
|---|---|---|---|---|
| `HCS` | The vendored source vault | mixed (vendored) | CC0 / MIT | `SOURCES`, `LICENSES/` |
| `ziptastic` | The engine — format and codecs | C++20 | MIT | `.dr` archives, the `ziptastic` binary |
| `heavenzip` | The orchestrator — chains binaries | C++20 | MIT | `.hzpkg` + `.hzpipeline` |
| `kosh` | The file workbench | — | — | split, squeeze, archive, verify, inspect |
| `lexora` | The text canonicaliser | Python | — | canonical text, byte-exact recovery |
| `compfactory` | The rules — workspace sync and build | PowerShell / shell | MIT | a built `workspace/` |
| `smlz` | The browser front door | JavaScript | PolyForm NC 1.0.0 | ZIP and TAR in a tab |
| `a2z` | The CHCE reader, and the conversion bench | JavaScript | PolyForm NC 1.0.0 | a `.dr` report, and converted files |

`awesome-compressor` (future) would be a catalogue: CC0, freely forkable.

Where a row says `—`, that repository's own documents are the authority; this table
records the role, not the internals.

`awesome-compressor` (future) would be a catalogue: CC0, freely forkable.

---

## 3. How the pieces fit

```
HCS ──(vendored, read-only)──▶ ziptastic ──(release binaries)──▶ heavenzip
                                    │                                 │
                                    │ .dr                             │ .hzpkg
                                    ├──────────────▶ kosh  (workbench)
                                    │                                 │
                                    ▼                                 ▼
                         a2z  (browser, reads it)            any decompressor

smlz  ── ZIP · TAR · GZIP · DEFLATE ──▶  the ordinary archives, in a tab
lexora ── text canonicalisation ──────▶  byte-exact recovery before the codec runs
```

Encode: `ziptastic c assets/` → `assets.dr`. Decode: `ziptastic d assets.dr` →
byte-identical. Inspect without decoding: `a2z`, in a browser, with no upload.

Reverse a chained archive: `heavenzip decompress report.pdf.hzpkg` reads the manifest,
runs `ziptastic -d` then `preflate --reverse`, and restores `report.pdf` bit-exactly.

**Key insight:** heavenzip never opens a compressor. It just spawns processes. That is
why it can be GPL-safe even while spawning GPL tools: there is no linking, only process
boundaries.

**Second key insight:** a2z never decodes a payload. It parses the container, verifies
the manifest, and stops where the native codecs begin. A browser cannot run rANS, BWT,
PPMd or LZMA2 without a WebAssembly build of the engine, and there is none. Claiming
otherwise would be the one thing this family does not do.

**Third key insight:** there are two browser members, and they are not the same tool.
`smlz` is the front door: it opens the archives people actually meet — ZIP, TAR, GZIP,
DEFLATE — and names a `.dr` as CHCE without reading it. `a2z` is the reader: it opens the
CHCE stream itself. Neither is a weaker copy of the other, because neither implements a
codec the other has.

---

## 4. Boundaries — what each repo may and may not do

The boundaries are the safety mechanism, not a filing convention.

### `ziptastic`

- ✅ Link permissive sources from `HCS`. Own the format.
- ❌ Never shell out to an external executable. Compression must be linked.
- ❌ Never link GPL or LGPL code.

### `heavenzip`

- ✅ Spawn binaries, including GPL ones, and record how to reverse every step.
- ❌ Never implement a compression algorithm. It is a router.
- ❌ Never bundle a GPL binary's source.

### `a2z`

- ✅ Read a CHCE stream: header, flags, Layer 0 bytes, block table, trailer, manifest.
- ✅ Verify structure and the manifest digest in the browser.
- ✅ Compress in the browser with the platform's codecs (gzip, deflate, ZIP).
- ❌ Never implement a CHCE backend. No rANS, no BWT, no PPMd, no context mixing.
- ❌ Never write a `.dr` file. The encoder is `ziptastic`; a browser cannot replace it.
- ❌ Never decode a payload and call it verification.
- ❌ Never bundle or link the engine, and never make a network request at runtime.

### `smlz`

- ✅ Read and write ZIP and TAR, read TAR.GZ, GZIP and DEFLATE, verify CRC-32.
- ✅ Recognise a CHCE stream by its magic and say which member reads it.
- ❌ Never decode CHCE, never implement a codec, never fetch anything at run time.

### The overlap between `smlz` and `a2z`, stated rather than hidden

Both are browser tools and both can produce a ZIP. That is a real overlap, and it is
allowed to exist only because the two jobs are different and written down:

| | `smlz` | `a2z` |
|---|---|---|
| The job | the front door for ordinary archives | the reader of the CHCE stream, and the conversion bench |
| A `.dr` | recognises it, names the member that reads it | reads it: header, block table, manifest |
| Images, audio, PDF, minify | not its job | its job |
| If both can do it | the archive packing is `smlz`'s | the conversion is `a2z`'s |

A person holding `assets.dr` needs a2z. A person holding a ZIP of PNGs needs smlz. A
person who wants both does not get a second, worse implementation of either — the rule
below is what keeps that true.

### `HCS` and `compfactory`

- ✅ `HCS` mirrors sources and records licences. ❌ Never patch `HCS/old/`; wrap instead.
- ✅ `compfactory` syncs and builds. ❌ Never add a compression feature to it.

---

## 5. Hard Rules (Global — Apply Everywhere)

These rules apply across all eight repos. Violating any of them breaks the
architecture.

### DO

- ✅ Keep each repo single-purpose. If a file doesn't belong, don't add it.
- ✅ Follow the repo-specific `AGENT_PROMPT.md` phases in order. Do not
  skip phases.
- ✅ Pass every phase's exit tests before starting the next phase.
- ✅ Commit with clear phase-tagged messages (`phase-<N>: <description>`).
- ✅ Update `docs/BENCHMARKS.md` (ziptastic) or `README.md` phase table
  (heavenzip, compfactory, a2z) after every phase.
- ✅ If a phase gate fails, commit as `phase-<N>: FAILED — <reason>` and
  stop. Report the failure.
- ✅ Treat `vendor/hcs/` as read-only.
- ✅ Keep licenses compatible. If unsure, ask before linking.
- ✅ Keep offline at runtime. No network calls, no downloads, no models.

### DON'T

- ❌ Do not shell out to external executables from `ziptastic`. Compression
  must be linked.
- ❌ Do not link GPL/LGPL code into `ziptastic` (`paq8px`, `upx`,
  `advancecomp`, `packJPG`, `lzo`). They are external-process-only in
  `heavenzip`.
- ❌ Do not implement compression algorithms in `heavenzip`. heavenzip is a
  router, nothing more.
- ❌ Do not implement a CHCE backend in `a2z`, and do not write a `.dr`.
  a2z reads the format; `ziptastic` owns it.
- ❌ Do not let the two browser members converge. If a feature would make `smlz` read
  CHCE or `a2z` the archive front door, it belongs in neither — write down which repo
  owns it first.
- ❌ Do not commit `build/`, `workspace/`, `*.exe`, `*.o`, or any build
  artefact. Every repo has a `.gitignore` — trust it.
- ❌ Do not add network dependencies, model downloads, or GPU paths. In
  `a2z` this is literal: no CDN, no `@import`, no font fetch — every
  library is vendored, and a test fails if a request leaves the origin.
- ❌ Do not `git subtree add` in compfactory. compfactory uses plain clones.
- ❌ Do not attempt to make one repo do another repo's job. The boundaries
  are the safety mechanism.
- ❌ Do not modify upstream sources under `HCS/old/`. If a source needs a
  patch, wrap it in `ziptastic/src/codecs/`.

---

## 6. License Strategy

You are making these repos **public**. That makes license discipline visible
and non-negotiable.

### `LICENSE` files (per-repo)

| Repo | License | Rationale |
|---|---|---|
| `HCS` | CC0 / Public Domain, or MIT | HCS is a mirror. You don't claim authorship of code inside `old/`. |
| `ziptastic` | MIT or Apache-2.0 | Permissive, matches the sources you actually link. |
| `heavenzip` | MIT | Same. |
| `compfactory` | MIT | Same. |
| `smlz` | PolyForm Noncommercial 1.0.0 | The other end-user surface; same reasoning. |
| `a2z` | **PolyForm Noncommercial 1.0.0** | The same exception — see below. |
| `awesome-compressor` (future) | CC0 | A catalog should be freely forkable. |

### Why `smlz` and `a2z` are the exception

Neither links anything from the family and neither links anything GPL. `a2z`'s libraries
are MIT (`fflate`, `pdf-lib`, `Tailwind`) and OFL (the two fonts); `smlz` uses the
platform's `CompressionStream` and the family's own `export-kit`. So the compatibility
rule that forces MIT on the engines does not apply to them — there is no linking to make
compatible.

They are the family's end-user *products* rather than build components: pages a person
opens. Noncommercial licensing is the deliberate choice for that surface, and it
does not reach back into the engine, the format or the sources, which stay permissive so
anyone can build on them.

The boundary is written down so it cannot drift: **the format and the engines stay
free; the app that wraps them for end users is noncommercial.** Any change to that
needs a decision, not a default.

Add a `LICENSE` file via GitHub UI (**Add file → Create new file → type
`LICENSE` → GitHub offers a template picker**), or via CLI. Add a
`## License` paragraph to each `README.md`.

### License classes inside HCS

HCS vendors 50+ sources across several licenses. They fall into four
buckets:

1. **Permissive** — MIT, BSD, Apache-2.0, zlib, 0BSD, Public Domain.
   **Safe to link from ziptastic.** Examples: `xz`, `libbsc`, `bzip3`,
   `brotli`, `zstd`, `lz4`, `zpaq`, `preflate`, `ryg_rans`, `fse`,
   `pcodec`, `bitshuffle`, `zlib-ng`, `libdeflate`, `zopfli`, `tamp`,
   `heatshrink`, `brieflz`, `snappy`, `density`, `lizard`.

2. **GPL / LGPL — do not link** — `paq8px` (GPL-2.0), `upx` (GPL-2.0),
   `advancecomp` (GPL-3.0), `packJPG` (LGPL-3.0), `lzo` (GPL/commercial
   dual), `7z PPMd` (verify per file, sometimes LGPL). These may be
   **spawned as processes** by heavenzip, but never linked into ziptastic.

3. **Dual** — some sources offer GPL or commercial. Default to the
   permissive branch if available. If not, treat as external-process-only.

4. **Unknown / verify** — some HCS sources are marked `verify` in
   `SOURCES.md`. Before linking any of them, copy their license text into
   `HCS/LICENSES/<id>-<spdx>.txt` and confirm the integration column in
   `SOURCES.md` is set correctly.

If you ever need a source that is not already in HCS, **stop and report
before adding it**. The user must approve new sources.

---

## 7. Where Everything Lives

Quick reference for the AI agent. Use this to find what you need without
hunting.

### `ziptastic/`

| You need | Path |
|---|---|
| The 12-phase build plan | `AGENT_PROMPT.md` |
| The joint parser to write | `src/engine/parser.cpp` + `include/chimera/parser.hpp` |
| The dual match finder to write | `src/engine/match_finder.cpp` |
| The per-block mode selector | `src/engine/mode_selector.cpp` |
| The rANS coder to write | `src/entropy/rans.cpp` |
| The Huffman coders to write | `src/entropy/huff_static.cpp`, `huff_adaptive.cpp` |
| LZMA2 fallback wrapper | `src/codecs/lzma2_fallback.cpp` |
| libbsc wrapper | `src/codecs/libbsc_wrap.cpp` |
| libzpaq wrapper (max-ratio only) | `src/codecs/zpaq_wrap.cpp` |
| Preflate wrapper | `src/precompressors/preflate_wrap.cpp` |
| Parmar wrapper | `src/precompressors/parmar_wrap.cpp` |
| Bitshuffle wrapper | `src/precompressors/bitshuffle_wrap.cpp` |
| Coarse block-hash index | `src/dedup/coarse_index.cpp` |
| Bitstream spec (write as you build) | `docs/FORMAT.md` |
| The v8 offset table | `docs/FORMAT_V8.md` |
| Benchmark log (update per phase) | `docs/BENCHMARKS.md` |
| Vendored sources (read-only) | `vendor/hcs/old/` |
| Source manifest (read-only) | `vendor/hcs/SOURCES.md` |

### `heavenzip/`

| You need | Path |
|---|---|
| The 8-phase build plan | `AGENT_PROMPT.md` |
| Master INI config | `config/syc.ini` |
| Per-type profiles | `config/profiles/*.ini` |
| INI parser to write | `src/orchestrator/ini.cpp` |
| File-type detector to write | `src/orchestrator/detector.cpp` |
| Subprocess dispatcher to write | `src/orchestrator/dispatcher.cpp` |
| Pipeline manifest writer | `src/orchestrator/pipeline.cpp` |
| Binaries (downloaded) | `bin/` |
| Binaries-download workflow | `.github/workflows/sync-binaries.yml` |

### `compfactory/`

| You need | Path |
|---|---|
| The 6-phase build plan | `AGENT_PROMPT.md` |
| Workspace sync (Windows) | `scripts/sync.ps1` |
| Workspace sync (macOS/Linux) | `scripts/sync.sh` |
| HCS → ziptastic propagation | `scripts/bump-hcs.ps1` |
| Build ziptastic from workspace | `scripts/build-all.ps1` |
| Cross-repo workflow trigger | `.github/workflows/dispatch-sync.yml` |
| Workspace documentation | `docs/WORKSPACE.md` |
| Developer workflow | `docs/WORKFLOW.md` |
| All cloned repos | `workspace/` (gitignored) |

### `a2z/`

| You need | Path |
|---|---|
| The 6-phase plan | `AGENT_PROMPT.md` |
| The CHCE v8 reader | `dr-inspect.js` |
| The compression engine | `compression.js` |
| The UI | `app.js` |
| The page | `index.html` |
| Vendored libraries (read-only) | `vendor/` |
| The format, as the reader implements it | `docs/ZIPTASTIC.md` |
| Why the encoder is not there | `docs/ZIPTASTIC.md` § *Why the encoder is not here* |
| Engine choices and the store rule | `docs/COMPRESSION.md` |
| Node suite | `test-node.js` |
| Headless-browser suite | `browser-check.js` |
| Built stylesheet (committed) | `styles/app.css` ← `scripts/build-styles.sh` |

### `smlz/`

| You need | Path |
|---|---|
| The boundary, in its own words | `PROJECT_OVERVIEW.md` |
| The archive reader/writer | `archive.js` |
| The page | `index.html` |
| Vendored ZIP writer | `vendor/export-kit.js` |
| Node suite | `test-node.js` |
| Browser suite | `browser-test.js` |

### `HCS/`

| You need | Path |
|---|---|
| Sources (read-only) | `old/<id>/` |
| Machine-readable manifest | `SOURCES.md` |
| Human-readable catalog | `README.md` |
| License texts | `LICENSES/` |

---

## 8. Where to Start — The Critical Path

**Start in `ziptastic`.** That is the answer. Everything else follows.

### Why

- `HCS` is done. Only touch it to add sources.
- `compfactory` is done. Only touch it if a script breaks.
- `smlz` and `a2z` ship on their own. Neither needs a release from any other repo, and
  no other repo needs them — which is why they are not on the critical path, and why
  neither may grow a second job.
- `heavenzip` cannot work end-to-end until ziptastic produces a real
  release. Phases 0–3 can be verified with a placeholder binary, but the
  project's success hinges on ziptastic.
- `ziptastic` is the product. Every hour here compounds.

### The first-week plan

| Day | Repo | Phase | Milestone |
|---|---|---|---|
| 1 | `ziptastic` | Phase 0 | CMake + CLI skeleton + RAW round-trip |
| 2–3 | `ziptastic` | Phase 1 | LZMA2 fallback — **first "never worse than xz" milestone** |
| 4 | `ziptastic` | Phase 2 | Fine match finder (LZMA port) |
| 5 | `ziptastic` | Phase 3 | Joint parser (DP) |
| 6 | `ziptastic` | Phase 4 | rANS + context models — **first real CHCE backend** |
| 7 | `ziptastic` | Phase 6 | Coarse SREP index — **first time you beat xz on Deflate-heavy inputs** |

Once ziptastic Phase 6 passes its ratio gate on `silesia.zip`:

1. Tag `ziptastic v0.1.0` (triggers the release workflow).
2. Trigger heavenzip's `sync-binaries` workflow to pull the binaries.
3. Test heavenzip end-to-end.
4. Then consider the fifth repo (`awesome-compressor`).

### What to do first, right now

```powershell
cd C:\Users\Admin\ziptastic
notepad AGENT_PROMPT.md
```

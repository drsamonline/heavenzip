# Packaging decision: which binaries a release may carry

The shipped profiles name three binaries that are not in `bin/`: `srep`,
`preflate` and `paq8px`. `bin/` holds one file, the ziptastic release binary,
fetched by `sync-binaries`. So `text` and `max-ratio` validate with a warning
about a missing binary, and `media` would refuse at pre-flight. This document is
the decision that has been outstanding since phase 5, written down so it is not
re-litigated in a chat.

Two things are being decided, and they are not the same thing:

- **may it run** — always yes for a binary the user supplies themselves;
- **may it ship** — whether this repository's release archive may contain it.

`TOOL_MANIFEST.md` and `BUNDLING.md` disagree about the second. This file settles
it and says which document now governs.

## The rule

heavenzip is an aggregator, not a linker: it spawns separate programs and passes
files between them. Under that model a release **may** include a third-party
program in its own directory, keeping its own licence, while the orchestrator
stays MIT. `BUNDLING.md` describes this correctly and is the governing document;
`TOOL_MANIFEST.md`'s single `redistribute?` column conflates "may run" with "may
ship", and marks every GPL tool `no` on that basis. That column is the error.

Three groups still cannot ship, and they are the ones that matter here:

| group | why it cannot ship | example |
|---|---|---|
| **no licence at all** | no licence means all rights reserved; there is no permission to redistribute, only to read | `srep`, `crush`, `CCM`, `DeflOpt` |
| **commercial / closed** | the licence forbids redistribution | `nanozip`, `WinRK`, `Document Press` |
| **copyleft we cannot accompany** | a GPL binary may ship only with its licence text and a way to obtain its source, and only as a separate program | `paq8px`, `upx`, `ECM` |

> Not legal advice. The aggregation reading is the ordinary one — it is why a
> Linux distribution ships MIT, GPL and Apache software on one medium — but a
> commercial release should have it reviewed. `LICENSING_DECISION.md` covers the
> separate question of whether *our* licences should change; the answer there is
> no, and nothing in this document revisits it.

## Per-tool verdict

| tool | licence | may run | may ship | verdict |
|---|---|---|---|---|
| **ziptastic** | MIT (this fleet) | yes | yes | already shipped by `sync-binaries`; the only binary in a default release |
| **preflate** | Apache-2.0 | yes | **yes** | the cheapest to ship: permissive, needs only its `LICENSE` and a provenance note. No prebuilt release exists upstream, so shipping means building it per platform — a build-and-publish job, not a licence question |
| **paq8px** | GPL-2.0 | yes | **yes, as a separate program** | may go in `tools/paq8px/` with the GPL text and a source offer. Never inside ziptastic. A large CM compressor; ship only if a release binary is worth maintaining |
| **srep** | **all rights reserved** | yes, if the user supplies it | **no** | `old/srep-main/…/srep.cpp` says "Copyright (C) 2009-2014 Bulat Ziganshin. All rights reserved." with no licence file. Redistribution is not permitted. `HCS/LICENSES/sources/srep-HEADER.txt` is the evidence; the outreach tracked in `docs/PERMISSIONS.md` is the only path to changing this |
| **ECT** | Apache-2.0 | yes | yes | already marked `YES` in `TOOL_MANIFEST.md`, and the one row that column got right |

The practical consequence: **a default release ships ziptastic only.** A "full"
release may add `preflate` and `paq8px`, each in its own directory with its own
licence and provenance. No release may ever contain `srep`, so any profile whose
first step is srep is an *external* profile: it works on a machine where the user
put their own copy in `bin/`, and refuses cleanly everywhere else.

## What a profile does when its binary is absent

The code already does the right thing and the behaviour is contractual, not
incidental — `src/main.cpp` resolves every program before the first step runs:

```
heavenzip: step `srep` needs `bin/srep.exe`, which is not on PATH and does not exist.
Nothing was run and the input is untouched. Place the binary in bin/, or run the sync-binaries workflow.
```

That is a refusal with exit 1, before any intermediate file is rewritten. What it
must never become is a warning followed by a run that quietly skipped the step:
that would report success for a pipeline that did not do what the profile says.
`validate` warns by default and fails under `--require-bins`, which is the right
split — a fresh clone has no binaries, and a release check does.

## Why neither bundled tool exists yet, specifically

Both were attempted on 15 Nov 2026, and both fail for a reason that is not "nobody
got round to it". Recorded here so the next session starts from the facts.

**preflate does not build on POSIX at all.** `support/filestream.cpp` calls
`_ftelli64` and `_fseeki64` unconditionally — MSVC-only names with no `#ifdef`.
Compiling it with clang gives:

```
support/filestream.cpp:34:10: error: use of undeclared identifier '_ftelli64'
support/filestream.cpp:38:3: error: use of undeclared identifier '_fseeki64'
```

The fix is a two-line patch (`ftello`/`fseeko`), and the fleet's rule is that
vendored source is not patched — so this needs either an upstream patch or a
deliberate decision to carry one. It is also why `media` has never run: the
profile names `preflate` with `--in-place`, which is not preflate's CLI either
(its options are `-s` to split, `-r`/`-x` to recombine, producing **two** files
rather than the one-in-one-out a heavenzip step expects). Even a built preflate
needs an adapter before that profile can work.

**paq8px builds with g++ but not with the zig toolchain.** Its `CMakeLists.txt`
passes GCC-only flags:

```
error: unknown clang option: "-floop-strip-mine"
```

CI installs `g++` and would build it; the sandbox that wrote this could not, so
the bundle could not be verified. Shipping an unverified binary would break the
one rule this fleet keeps, so it waits for a CI job that builds and attaches it.

Net effect: `plain` is the only profile that works out of the box, which is why it
is the default and why the release bundle round-trips without it.

### The CPU baseline, which CI cannot check

The first published bundle crashed with `Illegal instruction` on `heavenzip validate`.
zig builds for the machine doing the building unless it is told otherwise, so the
binary used instructions the target CPU lacks — and the CI round trip passed, because
it ran on the same class of machine that produced it.

Both zig invocations now pass `-target x86_64-linux-gnu.2.28`, which pins the glibc
floor *and* a baseline CPU. `paq8px` is built with gcc's `-march=nocona`, already a
2004-era baseline, and `srep` is cross-compiled the same way as heavenzip.

### brotli is bundled statically

Built from the vault with `BUILD_SHARED_LIBS=OFF`. The default build links five of
brotli's own shared objects, which a self-contained bundle cannot carry; statically it
needs only libc and libm. Its CLI is `-o OUT IN`, which is why the `brotli` profile
uses `{in}`/`{out}` placeholders rather than the appended-argument convention.

### The bundle's glibc floor is 2.35

The bundle is assembled in an `ubuntu:22.04` container, and that is what sets the
floor: the engine requires `GLIBC_2.34`, so a 20.04 (2.31) build image cannot even
execute the thing it is packaging — the assembly step's own round trip failed with
`version 'GLIBC_2.34' not found`. paq8px built against 22.04 needs 2.35, so that is
the bundle's number. `heavenzip` and `srep` are cross-compiled with zig against 2.28
and are not the constraint.

### The glibc floor, learned the hard way again

The first `build-tools` run built paq8px on `ubuntu-latest` and produced a binary
that demands **`GLIBC_2.38` and `GLIBC_2.43`** — it would not start on Debian 12
(2.36), which is the same defect ziptastic 1.4.2 shipped with before its floor was
fixed. The build now runs inside an `ubuntu:20.04` container, so the floor is the
container's glibc rather than the runner's, and a step reads the binary's own
symbol versions with `objdump -T` and fails if any is newer than the build image.
A binary that builds but cannot start is worse than no binary: it passes every
check that does not try to run it.

## Two follow-ups this decision creates

1. **`TOOL_MANIFEST.md`'s `redistribute?` column** should become two columns,
   `may run` and `may ship`, and its GPL rows re-marked under the aggregation
   rule above. Left alone here so this stays a decision, not a rewrite of a table
   with forty rows of judgement in it.
2. **Binary names are Windows-shaped.** The profiles say `bin/srep.exe` and
   `bin/ziptastic.exe`, and the shipped Linux binary is
   `bin/ziptastic-1.4.2-linux-x86_64`. Nothing appends or strips the suffix, so a
   Linux user edits the profile or makes a symlink. Either the profile grammar
   should resolve a platform suffix or the examples should stop implying `.exe`
   is universal. Not decided here.

## What this does not change

No code, no profile, no `bin/` content. The release is unchanged: ziptastic only.
This file exists so the next session does not have to work out the same answer
from three documents that disagreed.

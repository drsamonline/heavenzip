# External tool manifest

heavenzip is a router: it spawns processes and never links a compressor. That makes
it licence-safe in a way ziptastic cannot be — but only for **tools the user
supplies**.

This file records, for every candidate binary collected so far, what stage it would
serve, its licence, and whether it may be **redistributed** (shipped in a release,
put in a zip, shared with someone else) as opposed to merely **run locally**.

## The rule this repo already states

`PROJECT_OVERVIEW.md` says: *"Do not commit `build/`, `workspace/`, `*.exe`, `*.o`,
or any build artefact."* So nothing below belongs in `bin/` in git. `bin/` stays
empty in the repository; the manifest tells the user what to put there.

**Redistribution is a different question from execution.** Spawning a GPL binary is
fine. *Shipping* it in an MIT repo's release is not — that is the distinction the
rest of this file turns on.

## Candidates

| tool | stage it would serve | licence | redistribute? |
|---|---|---|---|
| **ECT** (`ect.exe`, `ECT`, mac build) | DEFLATE re-encode for ZIP/PDF/PNG | **Apache-2.0** | **YES** |
| nanozip (`nz.exe`) | strong CM backend | closed source | no |
| DeflOpt | removes redundant DEFLATE blocks | none stated | no |
| ECM / UNECM | CD-image ECC strip/restore | GPL | no |
| CCM 1.20a | CM backend | none stated | no |
| crush / miniCRUSH | fast CM backend | none stated | no |
| lzari | educational LZ+arith | none stated | no |
| Document Press 6.01 | commercial text compressor | commercial | no |
| ashford | CM backend (source included) | none stated | no |
| WinRK 3.12 | commercial archiver | commercial | no |
| Radyx 0.92 | GUI frontend | GPL-3 | no |
| WABBIE | benchmark GUI | none stated | no |
| SplitSat | file splitter | none stated | no |
| NexoPacker | SFX creator (raw Deflate) | none stated | no |
| `decomp.exe` | 4 KB LZMA decoder | none stated | no |
| MakeLX | EXE header modifier | none stated | no |
| ProcProfile | Windows process profiler | GPL-3 | no |
| `uFNV.pas` | FNV hash unit (source) | none stated | n/a |

**Only ECT may be redistributed.** Everything else is user-supplied at most, and
several may not be redistributed at all.

## A problem with the current profiles

`config/profiles/max-ratio.ini` chains **`srep` + `paq8px`**, and `text.ini` chains
**`srep`**. Both are fine to *spawn*. Neither is fine to *ship*:

- **paq8px** is GPL-2+. Redistributing it inside an MIT project's release would make
  that release GPL.
- **SREP** is worse: it has **no licence file at all**, which means all rights
  reserved. It is not redistributable in *any* form, and it is the one item on this
  entire shelf that cannot even be shipped as a separate binary.

So the profiles are correct as *instructions to a user who already has the tools*,
and would become a licence violation the moment a release bundled `bin/` with them
in it. That should be stated in the README next to the profile table.

## What is worth adding to the profiles

1. **ECT as a stage for archives containing DEFLATE.** Apache-2.0, so unlike
   everything else here it can ship. It is the only tool in this collection that
   both improves ratio and can legally be redistributed.
2. **A `cd-image` profile** — ECM strips ~12% of every 2352-byte CD sector as
   computable ECC before compression. GPL, so user-supplied; but the technique is
   ECMA-130 and ziptastic can implement it natively later, at which point the
   profile stops needing an external binary at all.
3. **CCM / nanozip as CM alternatives** to paq8px in `max-ratio`, for users who
   cannot or will not run a GPL tool. Both are user-supplied.

## Why this matters for the four-repo architecture

ziptastic must link everything it uses, so its licence ceiling is "permissive
only" — that is why the plugin ABI exists. heavenzip spawns processes, so its
ceiling is much higher, and it is where every copyleft tool on the shelf actually
becomes usable. **The two repos are complementary precisely because of that
difference**, and this manifest is the boundary: what ziptastic may link, heavenzip
may spawn, and neither may ship what neither may redistribute.

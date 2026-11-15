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

Two questions, two columns. **may run** asks whether heavenzip may spawn it on a
machine where the user has it; that is a process boundary, so the answer is
always yes. **may ship** asks whether this repository's release archive may
*contain* it, and that is decided per licence under the aggregation rule in
`BUNDLING.md`: a separate program, in its own directory, with its own licence and
a way to obtain its source.

| tool | stage it would serve | licence | may run | may ship |
|---|---|---|---|---|
| **ziptastic** | the engine every profile ends with | MIT (this fleet) | yes | **yes** — already shipped by `sync-binaries` |
| **ECT** (`ect.exe`, `ECT`, mac build) | DEFLATE re-encode for ZIP/PDF/PNG | **Apache-2.0** | yes | **yes** |
| **preflate** | Deflate re-compression | **Apache-2.0** | yes | **yes** — needs a build, not a permission |
| **paq8px** | strong CM backend | **GPL-2+** | yes | **yes, with its source** — never inside ziptastic |
| ECM / UNECM | CD-image ECC strip/restore | GPL | yes | yes, with source |
| ECM3 | CD-image ECC strip/restore | AGPL-3 | yes | yes, with source |
| Radyx 0.92 | GUI frontend | GPL-3 | yes | yes, with source (if wanted) |
| ProcProfile | Windows process profiler | GPL-3 | yes | yes, with source (if wanted) |
| nanozip (`nz.exe`) | strong CM backend | closed source | yes | **no** — needs the author's permission |
| Document Press 6.01 | commercial text compressor | commercial | yes | **no** |
| WinRK 3.12 | commercial archiver | commercial | yes | **no** |
| DeflOpt | removes redundant DEFLATE blocks | none stated | yes | **no** — all rights reserved |
| CCM 1.20a | CM backend | none stated | yes | **no** — all rights reserved |
| crush / miniCRUSH | fast CM backend | none stated | yes | **no** — all rights reserved |
| lzari | educational LZ+arith | none stated | yes | **no** — all rights reserved |
| ashford | CM backend (source included) | none stated | yes | **no** — all rights reserved |
| WABBIE | benchmark GUI | none stated | yes | **no** — all rights reserved |
| SplitSat | file splitter | none stated | yes | **no** — all rights reserved |
| NexoPacker | SFX creator (raw Deflate) | none stated | yes | **no** — all rights reserved |
| `decomp.exe` | 4 KB LZMA decoder | none stated | yes | **no** — all rights reserved |
| MakeLX | EXE header modifier | none stated | yes | **no** — all rights reserved |
| `uFNV.pas` | FNV hash unit (source) | none stated | n/a — a unit, not a program | **no** — all rights reserved |
| **SREP** | huge-dictionary LZ77 preprocessor | **none — all rights reserved** | yes, if the user supplies it | **no, in any form** |

This table replaces two that disagreed. The earlier one had a single
`redistribute?` column that answered "no" for every GPL tool, and a second table
below it answered "yes, with source" for the same tools. One question was being
asked twice with different answers, which is worse than not asking it.

## The shipped profiles and what they need

`config/profiles/max-ratio.ini` chains **`srep` + `paq8px`**, `text.ini` chains
**`srep`**, and `media.ini` chains **`preflate`**. None of the three is in `bin/`.

- **srep** — all rights reserved, no licence file, not redistributable in any
  form. The profiles that use it are instructions to a user who already has it;
  a release may never bundle it. `HCS/LICENSES/sources/srep-HEADER.txt` is the
  evidence.
- **paq8px** — GPL-2+. It may ship as a separate program with its licence text
  and source, in `tools/paq8px/`. It may not be linked into ziptastic, which is
  the line `ziptastic`'s plugin ABI exists to hold.
- **preflate** — Apache-2.0, the cheapest of the three to ship: permissive, and
  only needs a build step, which heavenzip does not do for itself.

An earlier version of this file said redistributing paq8px "inside an MIT
project's release would make that release GPL". That is the rule for *linking*,
not for aggregating, and it contradicted `BUNDLING.md` on the next page.
`docs/PACKAGING.md` records the corrected decision.

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

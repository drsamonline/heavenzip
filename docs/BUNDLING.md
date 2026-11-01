# Bundling third-party tools

heavenzip is a router, and the design question this answers is: **may the
distribution include the tools it spawns, each under its own licence, while the
orchestrator stays MIT?**

Yes — for most of the shelf. This document defines the layout, the per-tool licence
rule, and the three cases where it does not work at all.

> Not legal advice. The structure below is the standard, well-trodden way to
> distribute third-party programs alongside your own. For a commercial release,
> have it reviewed.

## The model: aggregation, not linking

Bundling separate *programs* that communicate only by spawning processes and
passing files is **aggregation**, not a combined work. Each program keeps its own
licence, and the orchestrator's MIT licence covers only the orchestrator. This is
the same reason a Linux distribution can ship MIT, GPL and Apache software on one
medium.

That is a different situation from ziptastic, which *links* code and therefore
inherits the licence of everything it links. The two repos sit on opposite sides of
that line, which is exactly why both exist.

## Layout

```
heavenzip/
  LICENSE              MIT — covers src/, config/, docs/ ONLY
  NOTICE               says the above in one paragraph
  THIRD_PARTY.md       generated table: tool, version, licence, source URL
  src/                 the orchestrator
  tools/
    ect/               ECT.exe | ECT (linux) | ECT (mac)
      LICENSE          Apache-2.0
      NOTICE           upstream copyright
      PROVENANCE.md    version, upstream URL, build flags
    paq8px/
      paq8px.exe
      LICENSE          GPL-2.0
      PROVENANCE.md    upstream URL + the exact commit
      SOURCE.md        where the corresponding source is offered (see below)
    ...
```

**Each tool directory carries its own licence and its own provenance.** The
top-level `LICENSE` must say plainly that it does not cover `tools/`.

`bin/` stays as the runtime lookup path, but it becomes a *staging* directory the
installer populates from `tools/`, so the current `.gitignore` entries can remain
for developer builds.

## What each licence family requires

| licence | bundle? | obligation |
|---|---|---|
| **MIT / BSD / Apache-2.0** | yes | keep the copyright notice and licence text |
| **GPL-2 / GPL-3** | yes | **also provide the corresponding source**, or a written offer valid for three years |
| **LGPL-2.1 / LGPL-3** | yes | as GPL, plus the user must be able to replace the library — trivially satisfied here because it is a separate executable |
| **AGPL-3** | yes | as GPL; the network clause applies to *modified* versions run as a service, which a bundled unmodified binary is not |
| **commercial / closed** | **no** | requires the author's permission |
| **no licence file** | **no** | all rights reserved — the default under copyright |

## On "FreeArc shipped SREP, so we can too"

This comes up, and it is worth answering precisely because it looks like a
precedent.

**FreeArc shipped SREP because the same person wrote both.** SREP is Bulat
Ziganshin's, and so is FreeArc. He was redistributing his own work alongside his own
work. That is not a third party shipping unlicensed code, and nothing about it
transfers to anyone else.

**Attribution is not permission.** Under copyright, a work with no licence is *all
rights reserved* by default. Crediting the author tells a reader who wrote it; it
does not grant the right to copy it. "Rights reserved to the original owner, used
here with thanks" is an accurate *description* of the situation and a legally
inadequate *basis* for shipping it.

If it were otherwise, every commercial program would be redistributable with a
credit line, which is obviously not the world we live in.

## The three exceptions, stated plainly

1. **SREP — no licence file.** Not bundleable in any form *by us*. It is the single
   item on the entire shelf that cannot be redistributed even as a standalone binary.
   It can be *supported* (heavenzip spawns it if the user installs it) but never
   *shipped*. See `PERMISSIONS.md` for the one route that would change this: asking
   the author, who has every right to say yes.
2. **nanozip, WinRK, Document Press — commercial/closed.** Not bundleable without
   the author's written permission.
3. **Everything with no licence file** — CCM, crush, miniCRUSH, lzari, DeflOpt,
   ashford, WABBIE, SplitSat, NexoPacker, MakeLX, `decomp.exe`, `uFNV.pas`.
   Absence of a licence is not permission; it is the opposite.

For those, the tool stays in `THIRD_PARTY.md` as **user-supplied**, with a link to
where the user gets it. The orchestrator still supports it; the distribution just
does not contain it.

## The GPL source obligation, which is the real work

Bundling `paq8px.exe` is permitted, but GPL-2 requires that recipients can obtain
the corresponding source. Two acceptable ways:

- **ship the source** in `tools/paq8px/src/` (simplest, and honest), or
- **a written offer**, valid for three years, to supply it.

A link to a GitHub repository is *not* by itself a compliant offer under GPL-2 §3,
though it is under GPL-3 §6(d) if the binary came from that exact source. Since
paq8px is GPL-2+, shipping the source alongside is the safe choice.

The same applies to ECM (GPL) and, if ever bundled, ECM3 (AGPL-3).

## Effect on the profiles

The current `max-ratio` profile chains `srep,paq8px`. Under this design:

- **paq8px** becomes a bundled tool with source → the profile works out of the box.
- **SREP** cannot be bundled → the profile must *detect* it and fall back. A missing
  SREP should degrade to `ziptastic:lzma` with a one-line notice, not fail.

That is a real behavioural requirement the design should state, because a bundled
distribution whose default profile depends on an unbundleable tool is broken on
every machine but the author's.

## Recommended default profiles under the bundled design

| profile | pipeline | ships? |
|---|---|---|
| `text` | `ect,ziptastic:lzma` | **yes** — fully bundleable |
| `media` | `preflate,ziptastic:brotli` | yes |
| `max-ratio` | `paq8px,ziptastic:lzma` + source | yes, with source |
| `max-ratio+srep` | `srep,paq8px` | **no** — SREP user-supplied, degrades gracefully |
| `cd-image` | `ecm,ziptastic` | yes, with source |

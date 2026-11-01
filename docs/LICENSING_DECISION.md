# Licensing decision: stay MIT

Recorded so this does not get re-litigated. **The answer to "do we need to change
our licences?" is no.** MIT already does everything the design needs, and changing
it would cost more than it buys.

## Relicensing would not solve the actual problem

The blocked tools are blocked because **their authors granted no rights** — SREP,
CCM, crush, DeflOpt and the rest have no licence file, which under copyright means
all rights reserved.

Changing *our* licence does not touch that. A GPL heavenzip has exactly as much
right to redistribute Bulat Ziganshin's unlicensed SREP as an MIT one does: none.
Licensing governs what others may do with **your** code. It cannot grant you rights
in **someone else's**.

So relicensing is aimed at the wrong problem. The only things that solve it are
asking the author (see `PERMISSIONS.md`) or reimplementing the technique.

## What MIT already permits

| repo | what MIT lets us do | what is still blocked |
|---|---|---|
| **ziptastic** | link permissive code freely; load LGPL tools through the plugin ABI | linking GPL code (so we do not — plugins only) |
| **heavenzip** | aggregate GPL/Apache/AGPL binaries, each with its own licence and source | shipping no-licence and commercial binaries |
| **HCS** | mirror other people's code with their licences intact | — |
| **compfactory** | clone and build | — |

Nothing on that list is improved by relicensing. The plugin ABI in ziptastic and the
aggregation model in heavenzip were designed *precisely* to reach the maximum set of
tools without changing the licence.

## What changing it would cost

- **GPL everywhere** — downstream users would have to GPL their own work to use
  yours. That is a real reason for people to choose a different tool, and it buys
  nothing: paq8px and ECM are already usable through heavenzip's process boundary,
  and LGPL is already usable through ziptastic's plugins.
- **Any copyleft** — the "single-binary, permissively-licensed drop-in archiver"
  property disappears, which is the whole selling point of ziptastic and the reason
  its README makes that promise.
- **A custom or source-available licence** (PolyForm and friends) — stops being open
  source at all, and every downstream integrator goes away.

## When relicensing *would* be the right answer

Two cases, neither of which is now:

1. **If we wanted to link GPL code directly into ziptastic** instead of loading it as
   a plugin. Then ziptastic would have to be GPL. We chose the plugin boundary
   specifically to avoid that.
2. **If a GPL tool's behaviour were needed in-process and could not be spawned.**
   Not the case for anything on the shelf.

## The decision

**All four repos stay as they are: ziptastic MIT, heavenzip MIT, HCS as its own
mirror licence, compfactory as it is.**

The work that remains is not licensing work. It is:

1. Ask the authors of the no-licence tools for redistribution permission
   (`PERMISSIONS.md`).
2. Where the answer is no or unknown, support the tool but do not ship it.
3. Where the technique matters, implement it natively — as already done for SREP's
   dedup and as planned for ECM's ECC.

That third route is the strongest one available, and it needs no permission from
anyone: ECM's sector geometry is ECMA-130, SREP's idea is published, and a native
implementation makes the external binary unnecessary rather than merely allowed.

> Not legal advice. If a commercial release is planned, have the licence structure
> reviewed by someone qualified.

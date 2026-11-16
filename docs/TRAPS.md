# Traps this repository has already sprung

Every entry here cost real time at least once. Read this before building, publishing
or debugging anything in this repo — most of them are invisible from the source and
none of them are guessable.

Ordered by how expensive they were, not by topic.

---

## 1. An exit code of 0 is not proof

`paq8px -8` on a 1.3 MB file prints `Out of memory.`, writes a **14-byte stub**, and
returns **0**. An archive built that way is corrupt, and the manifest says `ok`.

This is why `compress` reverses what it wrote and compares the bytes before calling it
a success, and why `autotune` verifies each candidate before ranking it. Do not remove
either check to save time — they are the only thing standing between a lying tool and
a user's data.

The same shape appeared with `srep`: under memory pressure it dies with
`munmap_chunk(): invalid pointer` and leaves a **0-byte** file, again with exit 0.

## 2. CI cannot see a CPU-class bug

The first published bundle crashed on `heavenzip validate` with `Illegal instruction`.
zig builds for the machine doing the building unless told otherwise, so the binary used
instructions the target CPU lacks — and the CI round trip passed, because it ran on the
same class of machine that produced it.

**Always pass `-target x86_64-linux-gnu.2.28` to zig.** It pins the glibc floor *and*
a baseline CPU.

More generally: verify by downloading the artefact and running it on a different
machine from the one that built it. Reading a green check is not verification.

## 3. Never merge a workflow step you have not executed

A bundle step was merged after only the steps around it had been run. It failed on
main, and the fix took seven more CI cycles — each one a different one-line bug that a
single local run would have surfaced at once.

Run the thing locally first. `workflow_dispatch` only exists on the default branch, so
"merge it and then find out" is the expensive order.

## 4. The build image must be able to run what it packages

The bundle was assembled in `ubuntu:20.04` (glibc 2.31) and packages the engine, which
needs `GLIBC_2.34`. The assembly step's own round trip failed with
`version 'GLIBC_2.34' not found`. The image is now `ubuntu:22.04`.

Rule: whatever container builds the bundle must execute every binary inside it.

## 5. `/tmp` is 64 MB here

It fills, and the symptom is not "disk full" in a place you are looking at — it is
`srep` reporting `Can't write to output file (disk full?)`, or a step failing for no
visible reason. Build under `/scratch/work`. `autotune` writes beside the input for
this reason; `HEAVENZIP_WORK` overrides it.

## 6. Small shell details that fail silently in CI

| Trap | Symptom |
|---|---|
| `ldd --version \| head -1` under `set -o pipefail` | step dies, prints nothing — `head` closes the pipe, `ldd` exits 141. Use `getconf GNU_LIBC_VERSION` |
| `ubuntu:*` container's default shell is `sh` | `set: Illegal option -o pipefail`. Set `defaults: run: shell: bash` |
| a bare `ubuntu:*` container has no `git` | the *clone* step fails, so it looks like a source problem |
| no `gh` inside the container | `gh: command not found` on the publish step. Publish from the runner |
| `curl` writes 0644 | `exec bin/ziptastic: Permission denied` with every byte correct |
| `sha256sum -c` matches on the recorded name | downloading to `engine` when SHA256SUMS says `ziptastic-1.4.2-linux-x86_64` fails with "No such file or directory" |
| `sha256sum stage/x.tar.gz > x.sha256` | the checksum names a path nobody has; `cd` first and name the file |

## 7. Toolchain facts

- **heavenzip** needs **CMake ≥ 3.20 and C++20**. `ubuntu:20.04` ships 3.16 and gcc 9.
  zig's clang handles the C++20 and cross-compiles; `pip install cmake` supplies the
  CMake.
- **paq8px** passes GCC-only flags (`-floop-strip-mine`, `-fgcse-sm`); clang rejects
  them with `unknown clang option`. Build it with **g++**, not zig. Its CMakeLists also
  needs `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` under CMake 4.
- **preflate does not build on POSIX at all.** `support/filestream.cpp` calls
  `_ftelli64`/`_fseeki64` unconditionally — MSVC-only names, no `#ifdef`.
- **srep** needs all of: `-DFREEARC_UNIX -DFREEARC_INTEL_BYTE_ORDER
  -D_FILE_OFFSET_BITS=64`, the include paths under `Compression/`, an empty
  `sys/sysctl.h` on the include path (glibc dropped the header at 2.32), and **only two
  translation units** — `Compression/Common.cpp` and `Compression/SREP/srep.cpp`; the
  rest are `#include`d by them. Compiling all ten gives a wall of nonsense errors.
- **srep's memory**: `-m5f` without `-l` wants ~1.7 GB and aborts on a normal machine.
  Every profile passes `-l256m`, which uses ~17 MB.

## 8. Licence facts that decide what may ship

- **srep has no licence.** No file, no statement, anywhere in the tree — Bulat
  Ziganshin's code, an unofficial repackage. Absence of a licence is not permission.
  The bundle carries its source and build recipe and `NOTICE` says so.
- **paq8px is GPL-2.0-or-later**, so its source must travel with the binary (§3).
- **rhefo (in HCS) is SSPL-1.0** — not OSI-approved; §13 obliges anyone offering it as
  a service to publish their own service source.

## 9. Verify the reverse, not just the forward

Three separate bugs lived only in the reverse path, each invisible while compressing:

- `run_reverse` did not clear its target, and ziptastic refuses to overwrite, so
  **every pipeline longer than one step failed to decompress**;
- srep and paq8px undo with `-d`, not `d`;
- with `{in}`/`{out}` placeholders the restore target is not always the last argument,
  which is what `reverse_out_index` in the manifest exists to record.

Any change to `build_plan` or `run_reverse` needs a two-step round trip through the
CLI, not just a compress.

## 10. The sandbox forgets its toolchain

`cmake` and `ziglang` are pip-installed, and a restart takes them away. The symptom is
`No module named ziglang` in the middle of a ninja build, or `cmake: No such file or
directory` right after it worked — which reads like a broken tree and is not.

Reinstall before assuming anything:

    python3 -m pip install --user cmake ninja ziglang

Then rebuild. The build directory itself survives; only the tools vanish.

## 11. Three places that already know the answer

Before measuring or rebuilding anything, check these — in this order. Each has cost
someone a day already.

**`drsamonline/ziptastic` → `docs/FINDINGS.md` §5, "Measured and rejected — do not
rebuild these".** Seventeen ideas with the number that killed each one: LZ4
precompression +50% worse, "zip store" unpacking −1.4%, parallelising the selector no
gain, a 256 KiB trial for the LZMA2 fallback 1.4 s of a 1.6 MB run. Rebuilding one of
those is the most expensive mistake available here, because it looks like progress.

**`drsamonline/audit` → `log/YYYY-MM/`.** One file per session, six headings, evidence
in every one. If a change is not there, it is not part of the fleet's record, however
green its CI was. Read the entries for the repos you are about to touch.

**`drsamonline/audit` → `README.md`, "Read state through the API, never through raw".**
`raw.githubusercontent.com` serves a cached copy that lags minutes to hours; it has
twice made finished work look broken here. Decide whether something worked with
`GET /repos/{owner}/{repo}/contents/{path}`, not with raw.

## 12. Do not debug in CI

Sleeping five minutes per iteration to read a remote log turned a morning of one-line
fixes into hours of waiting. Reproduce locally, fix, prove it locally, and use CI as a
final confirmation. If CI is the only way to see something, run it once and read the
whole log — not once per hypothesis.

# FreeArc's compression methods, and what heavenzip does with them

FreeArc does not use an `arc.ini` any more; its method table lives in
`FA-master/0.20/CompressionMethod.lua`, in a `[Compression methods]` section. This file
maps that table onto heavenzip's profiles: what transferred, what did not, and why.

## The table, as FreeArc states it

```lua
x   = 9              ; highest compression using only internal algorithms
ax  = 9p             ; highest involving external compressors
0   = storing

#   = #rep+#exe+#xb / $obj=#b / $text=#t
#b  = #rep + #bx                      ; binary: rep, then the binary chain
#xb = delta + #binary                 ; a delta filter before LZMA
#t  = dict:p:128m:80% + lzp:160m:… + ppmd:16:384m   ; text
#xt = dict: 128m:75% + #binary        ; text, fast decompression

9binary = lzma:254m:max
9rep    = rep:2040m
```

Two ideas carry all the value:

1. **A repeat filter runs before the entropy coder.** `#rep + #bx` is `srep` (FreeArc's
   `rep`) then LZMA. The filter exposes long repeats; LZMA then codes what is left.
2. **The chain is chosen per content class**, not per file — `$text`, `$obj`, `$wav`,
   `$bmp` each get their own route.

## What transferred

| FreeArc | heavenzip | Note |
|---|---|---|
| `#rep + #bx` | `max-ratio`, `max-ratio-7z` | srep → paq8px / 7-Zip |
| `#xb = delta + #binary` | — | no delta filter (see below) |
| `#t` (dict + lzp + ppmd) | `text` (srep → ziptastic) | the filters differ, the shape does not |
| per-type routing | `detect = text / media / binary` | the same idea, expressed as routes |
| `-mx9` | `max-ratio`, `max-deflate`, `max-ratio-7z` | one per terminal stage |

## What did not, and why

**`delta` before LZMA.** xz has a delta filter (`--delta=dist=N --lzma2=…`), but it made
a base64 text file **worse**: 71,428 bytes against 62,640 without it. Delta helps
executables and PCM, not arbitrary binary, and choosing it needs FreeArc's `$obj` class
detection, which heavenzip does not have. Recorded as measured-and-rejected rather than
guessed at.

**Dictionary size.** FreeArc's `9binary` is `lzma:254m:max`. xz can express it —
`--lzma2=preset=9,dict=256MiB` — but the encoder needs several times the dictionary in
RAM and **fails with "Cannot allocate memory"** on a 2 GB machine. `dict=64MiB` (preset
9's own default) is what this box can do; 128 MiB already fails. A template that cannot
run where it is tested is not a template.

**`lzp`, `ppmd`, `dict`, `grzip`, `tor`, `delta` are NOT absent.** I said they were,
twice, because I searched `old/` for directories named after the codecs. They are inside
FreeArc's own tree, which is what `old/freearc-c-main/src/Compression/` is:

```
CLS  Delta  Dict  External  GRZip  LZMA  LZMA2  LZP  MM  PPMD  REP  Tornado  _Encryption
```

So FreeArc's whole codec set is in the vault, and `arc` builds from it — see below.
Only `tta` (the audio codec) is genuinely missing, and it is the one that does not apply
to a general-purpose archiver.

## The table itself, and where each port looks for it

The method table is now in the vault twice over, and the two copies are not equivalent:

| Source | What it is | Usable? |
|---|---|---|
| `old/FA-master/0.20/CompressionMethod.lua` | the table in Lua, as documentation | reference only |
| `old/freearc-067/app/Compression.hs` (`builtinMethodSubsts`) | the table **compiled into** the modernised 0.67 port | the copy a buildable archiver actually uses |

`config/freearc/arc.ini` is that second copy, extracted verbatim: **105 method
definitions**, including `#b = #rep+#bx`, `9rep = rep:2040m` and
`9binary = lzma:254m:max`.

**Naming and discovery differ per port, and this is where a file gets missed:**

| Port | Reads | Which names | `-cfg`? |
|---|---|---|---|
| `freearc-c` (the one this bundle ships) | `arc.ini` beside the binary | **only** `arc.ini` | **no** |
| `despawnerer/freearc` (Haskell, in the vault) | `arc*.ini` in the config places | `arc*.ini` glob | **yes** — any filename |

So a file called `FA.ini` is invisible to both the glob and the C port's fixed name, and
becomes readable only through `-cfg FA.ini` on the Haskell port. Searching a repository
for `*.ini` will not find it either if the name is something else entirely.

**The `[Compression methods]` table is not read by the C port.** `arc.ini` support in
`freearc-c-main` is `ArcCommand.h`'s `RegisterExternalCompressors`, which scans only for
`[External compressor:…]` sections and registers DLLs. It never parses
`[Compression methods]`, so `-m9` and `-mx` cannot resolve there however the file is
written — measured: the section extracted verbatim from
`FA/0.20/CompressionMethod.lua` and placed beside the binary still answers
"data compression failed (-2) method 9".

The table is therefore guidance for the chains heavenzip implements as profiles, not
configuration this binary consumes. `-m4` and `-m5` map to LZMA directly and work.

**Building FreeArc itself.** `freearc-c-main` has a Makefile; this box has no make, so
the object rules were read out of it the same way as for 7-Zip. Two obstacles, both
mechanical:

| Obstacle | Fix |
|---|---|
| `basename` collides with glibc's `noexcept` declaration | `-Dbasename=fa_basename` |
| `C_MM.cpp` and the crypto layer need include paths | `-I. -ICompression -ICompression/_Encryption/headers` |

`tools/build-freearc.py` does this reproducibly: `arc` and `unarc` both link, and a
22,235-byte PNG round-trips byte-exactly through them.

**But `arc` segfaults on `lzma`.** Measured with the script-built binary:

| method | result |
|---|---|
| `storing` | 22,416 bytes |
| `tor` | 22,416 |
| `lzp` | 22,416 |
| `grzip` | 22,273 |
| `rep` | 22,448 |
| `ppmd` | 22,472 |
| **`lzma`** | **SIGSEGV (exit 139)** |
| `-m4`, `-m5`, `-mx` | SIGSEGV — they resolve to `lzma` |

LZMA is the point of the archiver, so `arc` is **not** wired into the bundle yet. An
archiver whose best codec crashes is worse than no archiver: it would fail on exactly
the files someone reaches for it to compress. Extraction needs `unarc` beside `arc` — `arc x` shells out to
it, and without it the error is "failed to execute unarc", which reads like a missing
feature rather than a missing binary.

**`storing` as a first stage.** FreeArc's `0 = storing` is a real stage: store the file
uncompressed so a later filter sees it whole. heavenzip's `plain` profile is the engine
at its default, which is not the same thing, and a true `store` stage is not written.

## The memory ceiling, measured

| dictionary | result on this box (2 GB) |
|---|---|
| 64 MiB (preset 9 default) | 62,640 bytes for an 81 KB base64 file |
| 128 MiB | **Cannot allocate memory** |
| 256 MiB | **Cannot allocate memory** |

FreeArc's presets assume a desktop with gigabytes to spare. A bundle that ships to
laptops should keep preset 9's default and let `autotune` say when a bigger dictionary
would have paid — on a machine that has the memory.

## Content classes: FreeArc routes by filename, heavenzip by content

FreeArc's `arc.groups` (captured in `config/freearc/arc-groups.txt`) defines thirteen
groups — `$text`, `$binary`, `$exe`, `$iso`, `$obj`, `$bmp`, `$wav`, `$mod`, `$precomp`,
`$jpg`, `$jpgsolid`, `$default` — and assigns files to them **by pattern**:

```
$obj
*.dcu
*.obj
*.o
*.a
*.lib
$exe
*.com
*.exe
*.scr
$bmp
*.bmp
*.tif
*.tiff
```

Each group then selects its own chain: `$wav => tta`, `$bmp => mm+grzip:8mb:m1:l2048:h15:a`,
`$exe => dispack`, `$text => dict+lzp+ppmd`.

heavenzip refuses that approach on purpose — classification is by content, never
extension — and the class set it has is coarser:

| FreeArc group | heavenzip class | what heavenzip does |
|---|---|---|
| `$text` | `text` | srep → engine |
| `$bmp`, `$wav`, `$exe`, `$iso`, `$mod` | **`binary`** | the generic engine |
| `$precomp`, `$jpg` | `media` | the engine, which links preflate |

**Measured:** a 16 KB WAV, a 10 KB BMP and a 4 MB executable all classify as `binary`.
That is correct by the content rule — none of them is text — and it is a real limitation:
the three coders those groups exist for (`tta` for audio, `mm` for bitmap, `dispack` for
x86) are not in this bundle, so a finer class would route to the same generic coder
anyway.

**A workaround that does pay, measured.** FreeArc's `mm:2*8` is a delta of stride 2, and
xz has that filter. On a 16 KB 16-bit PCM WAV:

| profile | bytes |
|---|---|
| `xz -9` | 4,936 |
| `audio` (`--delta=dist=2 --lzma2=preset=9`) | **3,044** |

38% smaller than plain LZMA, because consecutive samples differ by a little and delta
turns a waveform into small numbers. `audio` exists for that and nothing else.

The engine could not be measured on the same file: **`ziptastic` segfaults on any valid
16-bit PCM WAV** (reported as ziptastic#22, with a 2 KB reproducer). A truncated WAV
passes because parsing rejects it and the transform never runs — which is exactly how a
corpus of malformed samples would miss this.

Adding the classes without the coders would be a label with nothing behind it. The
honest position is that heavenzip's classification is coarser **and** its codec set is
narrower, and both would have to change together.

## Where to look

- `FA-master/0.20/CompressionMethod.lua` — the `[Compression methods]` table itself.
- `FA-master/0.20/Compression.lua` — how a method string (`-mc+precomp+xtor`) is
  manipulated: add, replace, remove compressors per group.
- `FA-master/Lua-code.md` — `DecodeMethod('exe2+4xb') ==
  'dispack070+delta+4x4:lzma:96mb:normal:16:mc8'`, which is the expansion the table
  above performs.

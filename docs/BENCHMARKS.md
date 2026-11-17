# Measured, not claimed

Every number here came from `tools/sweep.py` running the shipped profiles over real
files. Until this file existed, the only figures in the repository came from a 22 KB PNG
and an 18 KB text file, which is enough to prove a harness works and not enough to say
anything about compression.

## Corpus

Four files from the **Silesia corpus** — the standard benchmark set for compressors —
each truncated to the first 2,000,000 bytes so a sweep finishes inside a CI step:

| file | what it is | 2 MB sample |
|---|---|---|
| `dickens` | English novels, ASCII text | 2,000,000 |
| `ooffice` | a Windows DLL | 2,000,000 |
| `sao` | a database dump | 2,000,000 |
| `xml` | XML | 2,000,000 |

**Provenance:** the canonical host (`sun.aei.polsl.pl`) is unreachable from this
project's sandbox and is not on its egress allowlist. The files were taken from the
GitHub mirror `MiloszKrajewski/SilesiaCorpus`, whose README states the corpus's purpose
and origin. The mirror declares no licence of its own; Silesia is a public research
corpus. The corpus is **not** vendored into this repository — fetch it yourself and point
`tools/sweep.py` at the directory.

## Result

| file | `plain` | `xz` | `freearc` | winner |
|---|---|---|---|---|
| dickens | **476,122** (0.2381) | 586,596 (0.2933) | 632,511 (0.3163) | plain |
| ooffice | **744,522** (0.3723) | 893,220 (0.4466) | 904,151 (0.4521) | plain |
| sao | 1,220,380 (0.6102) | **1,220,336** (0.6102) | 1,242,397 (0.6212) | xz, by 44 bytes |
| xml | **169,242** (0.0846) | 221,232 (0.1106) | 246,212 (0.1231) | plain |

Times: plain 5.4–9.2 s, xz 1.2–2.3 s, freearc 1.0–1.8 s per 2 MB.

**The engine wins three of four, and not narrowly** — 19% smaller than `xz -9` on
dickens, 17% on ooffice, 23% on xml. It is also 2–4× slower, which is the trade it makes:
ziptastic spends time on context modelling that `xz` does not.

`sao` is a tie to within 44 bytes; it is the one file here where LZMA's long-range
matching is enough.

## What was not run, and why

- **`paq8px`** — the strongest coder in the bundle, and far too slow for a 2 MB sample in
  a sweep (it took 16 s on 22 KB earlier; that scales to hours here).
- **`max-deflate`, `max-ratio-7z`, `fa-p`** — they detect `media`, and none of these four
  files is media, so they correctly report `n/a`. On a media corpus they do not report
  `n/a` — they **fail**, because every one of them names `bin/srep.exe` and SREP cannot be
  shipped in any form. Measured below; tracked as issue #90.

## A media corpus, first measurement

The Silesia rows above are text and binaries, so the four `media` profiles had never been
measured on media. Four real files, all of them test fixtures already in the trees rather
than generated gradients:

| file | what it is | bytes |
|---|---|---|
| `embedded.pdf` | a PDF with embedded images (`preflate-rs` samples) | 167,181 |
| `fireworks.jpg` | a photographic JPEG (`zlib-ng` test data) | 123,093 |
| `photo.jpg` | a photographic JPEG (`packJPG` test files) | 21,797 |
| `ui.png` | a UI screenshot PNG (`ziptastic/docs`) | 89,580 |

`tools/sweep.py`, one thread:

| file | `plain` | `media` | `xz` | `sevenzip` | `max-deflate` · `max-ratio-7z` · `fa-p` |
|---|---|---|---|---|---|
| embedded.pdf | **123,011** (0.7357) | 123,011 | 142,620 (0.8531) | 142,651 (0.8533) | FAILED at `srep` |
| fireworks.jpg | **100,282** (0.8147) | 100,282 | 123,168 (1.0006) | 123,225 (1.0011) | FAILED at `srep` |
| photo.jpg | **17,295** (0.7935) | 17,295 | 21,584 (0.9902) | 21,621 (0.9919) | FAILED at `srep` |
| ui.png | **56,529** (0.6310) | 56,529 | 83,308 (0.9300) | 83,309 (0.9300) | FAILED at `srep` |

Times: plain 7.07 / 0.93 / 0.36 / **33.36** s, xz 0.37 / 0.37 / 0.21 / 0.26 s, sevenzip
0.52 / 0.67 / 0.50 / 0.40 s.

Three things this settles:

1. **The engine beats `xz -9` on every media file** — by 13.7% on the PDF, 18.6% and
   19.9% on the two JPEGs, 32.1% on the PNG. On the JPEGs the win is brunsli: `plain`
   gets 0.81 where `xz` gets 1.00, because a JPEG re-encoded from its DCT coefficients is
   worth 19% and compressing the JPEG bytes is worth nothing.
2. **`media` and `plain` are byte-identical on all four.** They are the same pipeline
   (`ziptastic -9`) and differ only in `detect`, so the media route currently adds
   nothing. If a media-specific chain is meant to exist, it does not exist yet.
3. **The three chains cannot run at all.** They fail on `bin/srep.exe`, and SREP is
   "all rights reserved, not redistributable in any form" (`TOOL_MANIFEST.md`,
   `BUNDLING.md`). `BUNDLING.md` already says a missing SREP should degrade to
   `ziptastic:lzma` with a notice rather than fail; nothing implements that. Issue #90.

`ui.png` is worth its own note: 33 s for 88 KB, against 0.26 s for `xz`. The PNG path is
where the engine's time goes on image content, and 33 s per screenshot is not a trade
most callers would take without being told.

## FreeArc's own baseline, and how to read it

FreeArc 0.67 ships its benchmark results in `Addons/Benchmarking/`. Captured in
`config/freearc/benchmark-baseline.txt`. The columns come from `maketest.rb`'s
`reportResults`, and they are **not** what they look like:

```
 -m5p            7.277  0.419  1.192
```

is **ratio 7.277× (higher is better), 0.419 MB/s compressing, 1.192 MB/s decompressing**
— not a time and not a size. Read as times it would say the opposite of the truth.

| method | ratio | comp MB/s | decomp MB/s |
|---|---|---|---|
| `-m5` | 7.047 | 0.545 | 3.878 |
| `-m5p` (precomp+srep) | **7.277** | 0.419 | 1.192 |
| `-m6` | 7.052 | 0.538 | 3.654 |
| `-mlpaq` | 7.503 | 0.101 | — |

On that corpus the `p` chain buys **3.3%** over plain `-m5` and costs **30% more
compression time and 3× the decompression time**. That is the honest shape of the
trade, from the author's own numbers.

**These are not reproducible here**: the corpora are Windows trees (`C:\Base\...`) this
project does not have. They are a published baseline, not a measurement of heavenzip.

## Reproducing

```sh
mkdir corpus && cd corpus
# any files you want measured; Silesia samples work
python3 tools/sweep.py "$PWD" --profiles plain,max-deflate,max-ratio-7z,xz,freearc
```

The sweep refuses to guess: a profile whose route does not match reports `n/a`, and a
profile that fails reports `FAILED` with the step's own words.

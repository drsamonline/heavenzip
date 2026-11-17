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

## A media corpus, and the chains that were never able to run

The Silesia rows above are text and binaries, so the `media` profiles had never been
measured on media. Three files, chosen so each exercises a different part of the route:

| file | what it is | bytes |
|---|---|---|
| `flate.pdf` | a PDF whose content stream is one FlateDecode stream — **synthetic**, built for this sweep | 78,168 |
| `portrait.jpg` | a photographic JPEG (`docs/assets`) | 12,267 |
| `ui.png` | a UI screenshot PNG (`docs/shot.png`) | 92,828 |

`tools/sweep.py`, one thread:

| file | `plain` / `media` | `max-deflate` / `fa-p` | `max-ratio-7z` | `xz` | `sevenzip` |
|---|---|---|---|---|---|
| flate.pdf | **42,922** (54.9%) 2.0 s | 61,520 (78.7%) 1.2 s | 55,299 (70.8%) 1.3 s | 78,172 (100.0%) 0.3 s | 78,270 (100.1%) 0.5 s |
| portrait.jpg | 10,586 (86.3%) 0.25 s | **10,572** (86.2%) 0.22 s | 10,629 (86.7%) 0.4 s | 12,188 (99.4%) 0.2 s | 12,248 (99.9%) 0.4 s |
| ui.png | **67,217** (72.4%) **24.5 s** | 72,144 (77.7%) **1.5 s** | 72,098 (77.7%) 1.4 s | 86,744 (93.5%) 0.3 s | 86,778 (93.5%) 1.0 s |

**Every one of these chains used to report `FAILED at srep`.** They name `bin/srep.exe`,
and SREP is "all rights reserved, not redistributable in any form", so they could not run
for anyone. A step now declares `optional = true` and is dropped with a notice
(heavenzip#92), and `precomp`'s exit 2 — "nothing to recompress", output still valid — is
accepted via `ok_exit = 0,2` (heavenzip#94). So these are their first real numbers.

Four things they say:

1. **The chains lose to `plain` on every file**, and on the PDF they lose badly: 61,520
   against 42,922, a 30% regression. The engine's own precomp → numeric → block chain
   already reaches inside the DEFLATE stream and then models what it finds; handing the
   inflated bytes to `xz` throws that away.
2. **`media` and `plain` are still byte-identical** — same pipeline (`ziptastic -9`),
   different `detect`. The media route adds nothing, and on this evidence it should not
   be given a chain of its own.
3. **The PNG is a time trade, not a size trade.** `plain` is 7.3% smaller and **16×
   slower** (24.5 s against 1.5 s for 93 KB). That is the `ui.png` cost recorded earlier
   in this file, now measured against an alternative: if a caller cares about seconds per
   screenshot more than 7%, the chain is the right answer even though it never wins on
   size.
4. **The JPEG is a wash** — 14 bytes between `plain` and `max-deflate`, and both beat
   `xz` by 13%, because brunsli re-encodes from DCT coefficients while `xz` sees
   compressed bytes.

The synthetic PDF is labelled because it is the one fixture here that is not real
content. It is a valid PDF with one FlateDecode stream and no images, which is the shape
`precomp` exists for; a real PDF with embedded JPEGs would exercise the brunsli path too.

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

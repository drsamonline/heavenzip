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
- **`max-deflate`, `max-ratio-7z`, `fa-precomp-srep-zstd`** — they detect `media`, and
  none of these four files is media, so they correctly report `n/a`. A media corpus
  (PNGs, ZIPs, PDFs) is the next thing to measure, and it is where those chains are meant
  to win.

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

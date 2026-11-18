# Ordering work by similarity

2026-11-18. Design note, with measurements. **Nothing in the pipeline changes yet** — this decides
whether it should.

## The idea, and where it came from

ziptastic compresses one file at a time. It cannot see that a directory holds many near-copies of the
same content, because that fact lives between files. heavenzip sees the whole directory, so it can
decide an **order** — and with a windowed codec, order is free compression.

DwarFS (Marcus Holland-Moritz, GPL-3.0) does this for whole filesystems. Its write-up describes the
hash precisely: a rolling 32-bit hash over every 4-byte window, histogrammed into 256 buckets, and
the **four most common bucket indices packed into 32 bits**. Files sharing common 4-byte substrings
share buckets, so they land near each other.

`tools/similarity.py` implements that from the description — not copied, because DwarFS is GPL-3.0
and this repository is MIT. An algorithm is not the expression of it, and the mix function is the one
the description names.

## Measured: when it pays, and when it does not

Three corpora, three codecs. The corpora are synthetic on purpose — they isolate the variable
(how badly names interleave) from everything else.

| corpus | zlib (32 KB window) | zstd -19 | xz -9e (64 MB window) |
|---|---|---|---|
| 12 files, 90 KB, **already grouped by name** | +1.1% | **−1.1%** | **−3.8%** |
| 16 files, 149 KB, names alternate | +0.5% | +0.1% | +0.1% |
| 30 files, **1.4 MB**, names alternate | **+19.1%** | **+24.0%** | +0.6% |

Three findings, and the second is the one that matters:

1. **Ordering pays when the corpus is larger than the codec's window.** 1.4 MB of alternating
   families: +19% on zlib and +24% on zstd. 149 KB: +0.1%, because everything fits in the window at
   once and adjacency is irrelevant.
2. **Ordering can make things worse.** On the already-grouped 90 KB corpus it cost 1.1% on zstd and
   3.8% on xz. If the files are already adjacent, reordering them by hash is churn — and churn that
   separates what was together.
3. **A 64 MB window makes it nearly pointless** (+0.6% on xz). If the whole corpus fits in the
   window, the codec has already seen everything and no order is better than another.

So the feature is not "sort files by similarity". It is: **apply an ordering only when the corpus
exceeds the window and the existing order is demonstrably bad.** Both conditions are measurable
before acting, and both are cheap to check.

## What heavenzip would do with it

1. Hash every file in the input directory (fast: one pass, 4 bytes at a time).
2. Compute the current order's cost on a sample, and the grouped order's cost.
3. Use the grouped order **only if it wins**, and say so in the plan output.

That is the same accept/reject trial the engine already uses inside a file, moved up to the
directory. It also means a wrong hash cannot hurt: the ordering is measured before it is used.

**What it is not:** not deduplication. Two identical files stay two files. DwarFS dedupes; this only
decides adjacency.

## What to measure before building it

A **real directory** — not two families I generated. The synthetic corpora show the mechanism works
and bound where it can help; they cannot say whether a real tree of source code, or a real backup
set, is bimodal enough for the win to appear. That is the same lesson as the rest of this week: the
fixture shows the mechanism, only real data shows the value.

Concretely: run `tools/similarity.py <dir> --measure` on a real directory of a few hundred MB and
compare. If it does not win there, the feature stays unbuilt.

## Licence position

DwarFS: GPL-3.0. This repository: MIT. The **algorithm** is implemented independently from its
published description, and the code here shares no lines with DwarFS. That is the same position the
project already takes on SREP (`HCS/SOURCES`): a published technique can be implemented
independently, the code cannot be copied.

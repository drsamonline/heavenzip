# heavenzip


![Screenshot](docs/shot.png)
<p>
  <img alt="live: online" src="docs/assets/badge-live.svg">
  <img alt="build: phase 0" src="docs/assets/badge-build.svg">
  <img alt="tests: none yet" src="docs/assets/badge-tests.svg">
  <img alt="licence: MIT" src="docs/assets/badge-licence.svg">
  <img alt="offline: no network" src="docs/assets/badge-offline.svg">
  <img alt="deps: 0 runtime" src="docs/assets/badge-deps.svg">
</p>

CHCE orchestrator - chains prebuilt binaries

> **Status: phase 7 done — the plan's phases are complete.** CLI, INI parser,
> detector, planner, dispatcher, manifest, external-process tier, bench harness and
> the two reference documents. 8 ctest suites in CI, including a round trip through
> the real ziptastic release binary and a test that the worked example in
> `docs/PROFILES.md` actually works.
>
> Classification is by content, never extension: magic bytes for media and
> archives, then a printable-ratio test that accepts valid UTF-8, so prose in
> Devanagari is text and a ZIP renamed to `.txt` is still an archive. The read is
> bounded at 64 KiB.
>
> `compress` writes `<input>.hzpkg` and `<input>.hzpipeline`; the manifest records
> every step's argv **and** the command that reverses it. A step that fails stops
> the run, keeps its logs, and the manifest is still written with `"ok": false`. A
> missing binary is refused before the input is touched.

## Quick start

```bash
# 1. get an engine into bin/ (the sync-binaries workflow does this in CI)
gh release download -R drsamonline/ziptastic --pattern 'ziptastic-*-linux-x86_64' --dir bin/

# 2. see what the shipped profiles would do
heavenzip validate
heavenzip list-profiles

# 3. plan, then run
heavenzip compress notes.txt --dry-run
heavenzip compress notes.txt
heavenzip decompress notes.txt.hzpkg --out restored.txt

# 4. measure every profile against a corpus
heavenzip bench tests/fixtures/
```

## Profile catalog

| Profile | detect | Pipeline | What it is for |
|---|---|---|---|
| `text` | `text` | srep → ziptastic `-c lzma -9` | prose, logs, source |
| `media` | `media` | preflate → ziptastic `-c brotli -q 11` | photos, PDFs, audio |
| `max-ratio` | `any` | srep `-m5f` → paq8px `-8` | last resort, slow and small |

Writing another one is a file, not a patch: [docs/PROFILES.md](docs/PROFILES.md).
How a payload is made and undone: [docs/PIPELINE.md](docs/PIPELINE.md).

## Support

<img src="docs/assets/support-portrait.jpg" width="72" height="72" alt="Dr. Sohil Momin" align="left" style="border-radius:50%;margin-right:1rem">

Free, offline, accountless — and staying that way. If it earned its keep:

| | |
|---|---|
| UPI (India) | `drsohilboi@ybl` — payee Dr. Sohil Momin, open amount, or scan [the QR](docs/assets/upi-qr.svg) |
| GitHub Sponsors | [github.com/sponsors/drsamonline](https://github.com/sponsors/drsamonline) |
| Ko-fi | [ko-fi.com/matneo](https://ko-fi.com/matneo) |
| PayPal | [paypal.me/drsohilmomin](https://paypal.me/drsohilmomin?locale.x=en_GB&country.x=IN) |
| My books | [Amazon author store](https://www.amazon.com/stores/author/B08ZT331LY/allbooks?ccs_id=aed72b9f-71d9-41dc-adee-e9368a317acc) |
| More apps | [drsamonline.github.io](https://drsamonline.github.io/) |
| Email | [sohilmomin2000@gmail.com](mailto:sohilmomin2000@gmail.com) |

Nothing is gated behind it: no accounts, no tracking, no network requests.

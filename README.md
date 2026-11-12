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

> **Status: phase 3 done.** The CLI, the INI parser, the detector, the planner and
> the dispatcher are in: 34 unit cases across three suites, a round-trip
> integration test with stub binaries, and the phase-0 exit test — 6 ctest suites,
> all in CI. `compress` spawns the pipeline and writes both output files;
> `decompress` reads the manifest and runs it backwards; `bench` is phase 6 and
> says so.
>
> Classification is by content, never extension: magic bytes for media and
> archives, then a printable-ratio test that accepts valid UTF-8, so prose in
> Devanagari is text and a ZIP renamed to `.txt` is still an archive. The read is
> bounded at 64 KiB.
>
> `compress` writes `<input>.hzpkg` and `<input>.hzpipeline`; the manifest records
> every step's argv **and** the command that reverses it, so `decompress` never has
> to guess. A step that fails stops the run, keeps its logs, and the manifest is
> still written — with `"ok": false` and the failing step named.
>
> `bin/` holds the ziptastic release binary that `sync-binaries` fetched; the
> shipped profiles name Windows executables, so `validate` reports them as warnings
> until a sync on that platform.

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

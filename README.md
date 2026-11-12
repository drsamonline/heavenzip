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

> **Status: phase 2 done.** The CLI skeleton, the INI parser, the content detector
> and the pipeline planner are in: 34 unit cases across three suites plus the
> phase-0 exit test, all in CI. `validate`, `list-profiles`, `detect` and
> `compress --dry-run` are real; `compress` without `--dry-run`, `decompress` and
> `bench` exit 2 and name the phase that owns them, because a command that is not
> built yet should refuse rather than pretend.
>
> `compress --dry-run` resolves the profile, refuses one that does not match the
> file, and prints the argv each step would run — including the intermediate files
> phase 3 will spawn into. Nothing is executed.
>
> Classification is by content, never extension: magic bytes for media and
> archives, then a printable-ratio test that accepts valid UTF-8, so prose in
> Devanagari is text and a ZIP renamed to `.txt` is still an archive. The read is
> bounded at 64 KiB, because heavenzip may be pointed at a multi-gigabyte image.
> `bin/` holds the ziptastic release binary that `sync-binaries` fetched.

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

# Writing a profile

A profile is one `.ini` file in `config/profiles/`, named after the profile, and
listed in `config/syc.ini` under `[profiles] available`. Nothing in the code knows
what a profile is called or what it does — add a file, and it exists.

## The smallest working profile

```ini
[route]
detect = text
pipeline = ziptastic

[ziptastic]
args = c -9
```

`bin` is omitted, so the step runs `ziptastic_bin` from `[general]` — the engine
that `sync-binaries` puts in `bin/`. This is the profile to copy when you want one
command and nothing else.

## The grammar

| Section | Keys | Rules |
|---|---|---|
| `[route]` | `detect`, `pipeline` | both required; `detect` is one of `text`, `media`, `archive`, `any` (or `binary`); `pipeline` is a comma-separated list of step names, in order |
| one section per step | `bin`, `args`, `reverse_args` | `bin` optional (defaults to `ziptastic_bin`); `args` passed verbatim; `reverse_args` optional, see below |

Anything else is an error, with the file and the line:

| Mistake | Message |
|---|---|
| no `[route]` | `missing required [route] section` |
| no `detect` or `pipeline` | `[route] is missing required key ...` |
| `detect = video` | `... is not one of text, media, archive, any` |
| a step named in `pipeline` with no section | `pipeline names step 'x' but there is no [x] section` |
| a section not named in `pipeline` | `[x] is not named in pipeline` |
| the same step twice | `pipeline names step 'x' twice; a pipeline is linear and a repeated step is a cycle` |
| an unknown key in a step | `[x] has unknown key 'y' (bin, args and reverse_args are the only ones)` |

`;` and `#` start a comment. A `;` inside quotes is data, so
`args = -x ";not a comment"` survives.

## `detect`

Classification is by content, never by extension. `any` matches every file; a named
class matches only that class. See `heavenzip detect <file>` for what any file is.

| Value | Matches |
|---|---|
| `text` | no NUL in the first 4 KiB, and ≥95% printable ASCII or valid UTF-8 |
| `media` | JPEG, PNG, GIF, MP3, MP4, PDF |
| `archive` | ZIP, 7z, gzip, bzip2, xz, tar |
| `binary` | everything else |
| `any` | all of the above |

## Steps, and the reverse of a step

Each step is spawned as:

```
<bin> <args...> <input> <output>
```

The first step's input is the file you named; each later step's input is the
previous step's output; the last step writes `<input>.hzpkg`. Intermediates are
`<input>.hzpkg.<step>.tmp`, so a failed run tells you where it stopped.

Decompression replays the reverse of every step, which the manifest records as:

```
<bin> <reverse_args...> <output> <input>
```

`reverse_args` defaults to `d`, which is ziptastic's own decompress verb. A tool
whose decompress verb differs says so:

```ini
[preflate]
bin = bin/preflate.exe
args = --in-place
reverse_args = --reverse
```

### Generating a profile instead of guessing one

A profile is a guess someone wrote down. `heavenzip autotune <file>` replaces the
guess with a measurement: it runs the combinations the tools in `bin/` make
possible — engine alone at `-9` and `--max-ratio`, `srep` in front of either,
`paq8px` alone, `srep` in front of `paq8px` — and ranks them by the size of the
file each one actually produced.

Every candidate is then reversed and compared against the input, smallest first. A
candidate that cannot restore its own output is listed with the reason and is not
eligible, which matters more than it sounds: `paq8px -8` on a 1.3 MB file runs out
of memory, writes a 14-byte stub and **exits 0**, so a ranking on size alone puts
it first.

`--write <name>` saves the winner as `profiles/<name>.ini` and adds it to
`[profiles] available`, with the measured ratio in a comment and a warning to
re-measure on a different corpus. It never overwrites an existing profile.

### The engine's real options, and the profile that needs nothing else

The shipped profiles used to pass `-c lzma -9` and `-c brotli -q 11` to ziptastic.
**ziptastic has no `-c` option and no `-q`** — its options are `-9`, `--max-ratio`,
`--no-cm-stream`, `--no-precomp`, `--no-numeric`. Every shipped profile therefore
failed at the engine step with `unknown option '-c'`, and nothing noticed because
no test ran the shipped profiles against the real binary. `integration.shipped-profiles`
does now: it takes each profile's own `args` and runs them against the engine.

`plain.ini` is new and is the default. Every other profile names a tool a release
does not carry — `srep` (all rights reserved, cannot ship), `preflate`, `paq8px` —
so a fresh install could not compress anything at all. `plain` chains the bundled
engine and nothing else.

### `bin` and the platform suffix

The shipped profiles name Windows binaries — `bin/srep.exe`, `bin/preflate.exe`,
`bin/paq8px.exe` — because that is where the shelf came from, while a Linux
release of the engine is `ziptastic-1.4.2-linux-x86_64`. Nothing used to bridge
that, so a profile was correct on one platform and a refusal on the other.

The path is resolved, in this order:

1. the name exactly as written, if it exists;
2. otherwise, the same name with the platform's own convention applied —
   `.exe` **stripped** on POSIX, `.exe` **added** on Windows.

So `bin/preflate.exe` runs `bin/preflate` on Linux without editing the profile,
and `bin/srep` finds `bin/srep.exe` on Windows. The configured name is tried
first, so an explicit path that exists is never second-guessed, and the manifest
still records the name the profile used — the argv shows what actually ran.

One thing this does not do: the engine's release asset is
`ziptastic-1.4.2-linux-x86_64`, which is neither `ziptastic` nor `ziptastic.exe`.
`bin/ENGINE` records the tag and asset, and `syc.ini`'s `ziptastic_bin` must name
the file that is there — a symlink or the full name. Suffix resolution is a
convenience for the shelf, not a substitute for a correct path.

## A worked example: `music.ini`

Goal: a profile that fires on MP3 and leaves everything else alone.

```ini
[route]
detect = media
pipeline = ziptastic

[ziptastic]
args = c -9
```

1. Write `config/profiles/music.ini` with the text above.
2. Add `music` to `available = text, media, max-ratio, music` in `config/syc.ini`.
3. `heavenzip validate` — reports `ok config/profiles/music.ini`.
4. `heavenzip compress track.mp3 --profile music` — or `--profile music --dry-run`
   first, which prints the argv without running anything.

No code, no rebuild. That is the test this document has to pass.

## Checking a profile

```bash
heavenzip validate                     # parse, and warn about missing binaries
heavenzip validate --require-bins      # fail when a step's binary is absent
heavenzip list-profiles                # what each profile would run
heavenzip compress file --profile p --dry-run
heavenzip bench corpus/                # every profile that matches, with ratios
```

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

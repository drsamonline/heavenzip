# Pipelines, the manifest, and how a payload is undone

## What a run produces

| File | Contents |
|---|---|
| `<input>.hzpkg` | the payload: whatever the last step wrote |
| `<input>.hzpipeline` | JSON describing how it was made, and how to undo it |
| `<input>.hzpkg.<step>.tmp` | intermediates, one per step except the last |
| `<input>.hzpkg.<step>.stdout` / `.stderr` | each step's captured streams |

Nothing is deleted on failure. A pipeline that leaves evidence is worth more than
one that cleans up after itself.

## The run, in order

1. Resolve the profile (`--profile`, else `default` from `[profiles]`).
2. Classify the input and refuse a profile whose `detect` does not match, naming
   the rule, the actual class and the reason.
3. Build one argv per step.
4. **Pre-flight:** every step's program must be resolvable — an explicit path must
   exist, a bare name is looked up on `PATH`. A missing binary is a sentence and
   exit 1, before the input is touched.
5. Spawn each step in order, capturing stdout and stderr to files.
6. Stop at the first non-zero exit. Write the manifest anyway.

## The manifest

```json
{
  "heavenzip": "0.1.0",
  "profile": "text",
  "detect": "text",
  "found": "text",
  "input": "/abs/in.txt",
  "output": "/abs/in.txt.hzpkg",
  "ok": true,
  "failed_step": "",
  "seconds": 0.18,
  "steps": [
    {
      "step": "srep",
      "argv": ["bin/srep.exe", "-m3f", "/abs/in.txt", "/abs/in.txt.hzpkg.srep.tmp"],
      "reverse": ["bin/srep.exe", "d", "/abs/in.txt.hzpkg.srep.tmp", "/abs/in.txt"],
      "exit_code": 0,
      "seconds": 0.09
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `ok` / `failed_step` | whether the run completed, and where it stopped |
| `argv` | exactly what was spawned, in order |
| `reverse` | exactly what undoes that step — recorded, never inferred |

`reverse` is written per step because "the reverse of a step" is not derivable from
`argv`: it depends on the tool's own verb, which is why profiles can state
`reverse_args`.

## Decompression

`heavenzip decompress <input.hzpkg> [--out <file>]`

1. Finds the manifest by stripping `.hzpkg` from the payload name —
   `in.txt` → `in.txt.hzpkg` + `in.txt.hzpipeline`.
2. Reads `steps`, takes each step's `reverse`, and runs them in reverse order.
3. The last reverse step writes to `--out` (default `<payload>.out`), never back
   over the original input.
4. Any non-zero exit stops the run and names the step.

A payload without its manifest cannot be undone by heavenzip: the manifest is the
only record of how it was made. `decompress` says so rather than guessing.

## Refusals, and why each one exists

| Situation | Behaviour |
|---|---|
| profile does not match the file | exit 1, names the rule and the actual class |
| a step's binary is missing | exit 1 before running anything; input untouched |
| a step exits non-zero | run stops, step named, its stderr shown, manifest written with `ok: false` |
| manifest missing on decompress | exit 1, explains that the manifest is the record |
| `bench` with a missing binary | row marked `skipped (bin missing)`; `--require-bins` turns that into a failure |

## Bench

`heavenzip bench <dir>` runs every profile against every file in the directory
whose class matches that profile's `detect`, and prints one row per pair:

```
profile      file                       class    in        out       ratio   time
engine       a.txt                      text     5520      159       2.9%    0.06s
```

It measures wall time per step as the sum of the steps, not of the process, and it
never leaves payloads behind — each run's output is removed after the row is
printed.

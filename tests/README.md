# Host unit tests

These tests run on any machine with a C compiler — **no Mac, no emulator, no
Retro68 toolchain.** They exercise ArtfulType's *pure* markdown logic (the
`mdcore` layer) directly, in milliseconds.

```sh
cd tests
make check
```

## Why a host test build

Most of ArtfulType's subtle logic — turning markdown delimiters into style
spans and back — is arithmetic on character buffers. On the Mac it happens to
read and write live `TEHandle` records, which is why it could previously only
be verified by hand on real hardware. The `mdcore` layer lifts that logic out
from behind the Toolbox so it operates on plain `const char *` buffers and
explicit model structs, and these tests pin its behaviour down:

- **Golden tests** — a specific input produces exactly the expected stripped
  text + spans.
- **Round-trip tests** — `emit(strip(s))` recovers `s`, the identity the whole
  Writer/Markdown mode switch (and undo) silently relies on.
- **Detector case tables** — the live "type `**bold**`, get bold" branches,
  which are almost impossible to hit reliably by hand.

Tests are written to **fail if the logic is broken** (verify by mutation), so
they catch regressions rather than merely exercising code.

## Files

| File | Role |
|---|---|
| `test_util.h` | Zero-dependency C89 assertion macros (`CHECK`, `CHECK_EQ`, `CHECK_STR`). |
| `host_mac_types.h` | Tiny shims (`Boolean`, `Str255`) so tests speak the Mac side's vocabulary. `mdcore` itself needs no Toolbox types. |
| `test_smoke.c` | Proves the harness compiles and runs. |
| `test_mdcore.c` | Golden + round-trip tests for `mdcore`'s strip/emit engine. |
| `test_mddetect.c` | Case tables for the live inline-markdown detector. |
| `Makefile` | `make check` builds and runs every `test_*` with native `cc -std=c89`. |

The harness pattern is borrowed from the author's BomberTalk project.

# ArtfulType — working notes for Claude

A distraction-free Markdown editor for classic 68k Macs (System 6/7), built
to run from a BlueSCSI on a compact Mac. Cross-compiled with **Retro68** using
its **multiversal** interfaces (not Apple's universal ones — some API and
constant names differ; everything lands in one generated `Multiverse.h`).

Deeper, path-scoped notes live in `.claude/rules/` and load only when relevant:
[`classic-mac-toolbox`](.claude/rules/classic-mac-toolbox.md) (Toolbox pitfalls —
read before touching the Toolbox) loads when you open `app/` sources, and
[`disk-images`](.claude/rules/disk-images.md) (the hermetic image builder,
blessing, deploy scripts) loads when you touch the build/deploy files.

## Build, test, lint

- **68k application**: CMake + Retro68's `add_application` (see `app/CMakeLists.txt`).
  Configure with `-DCMAKE_TOOLCHAIN_FILE=<retro68>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake`.
  Outputs `ArtfulType.bin` (MacBinary, what `deploy.sh` ships), plus `.APPL`/`.dsk`.
- **Host unit tests**: `make -C tests check`. The pure core (`app/mdcore.c`) is
  Toolbox-free C89 and compiles on any native `cc` with `-DAT_HOST_TEST`; tests
  run in milliseconds. Add cases there, not on the Mac side, whenever logic can
  be expressed without the Toolbox.
- **CI**: `.github/workflows/ci.yml` runs the host tests, a real 68k build in the
  `ghcr.io/autc04/retro68` container, and cppcheck (gated `--error-exitcode=1`).
- **Warnings are errors in spirit**: the 68k build is warning-clean today; keep it
  so. `Multiverse.h` itself emits warnings — those are third-party and out of
  scope. cppcheck false positives are suppressed *inline with a rationale*
  (see `DoLinkHidden` in `app/markdown.c`), never via a blanket ignore.

## Architecture in one breath

Two TextEdit records back every document:

- `gTE` holds the canonical **Markdown source**.
- `gHiddenTE` holds the **styled Writer view** (real bold/italic/Monaco/heading
  runs, no visible delimiters).
- `gActiveTE` aliases whichever is live; `gHideMarkdown` selects the mode. In
  Writer mode `gActiveTE == gHiddenTE`.

Style encoding in `gHiddenTE`'s TextEdit style runs: **bold**/**italic** = `tsFace`,
**code** = font is Monaco, **heading** = bold + a larger `tsSize`, **link** =
underline face + a 1-based link ID stashed in the otherwise-unused `tsColor.red`
(1..64; 0 = no link). The ID rides the style run, so links follow their text
through edits automatically. URLs live in `gLinkURLs[]`, keyed by that ID; the
table is rebuilt in `BuildHiddenView` and compacted by `CompactLinkTable` when
IDs are exhausted (`MAX_LINKS` = 64).

The pure engine `mdcore` (`app/mdcore.{c,h}`) does strip / emit / live-detect on
plain buffers; `markdown.c` is the thin Mac adapter that locks handles, calls
mdcore, and maps spans onto real `TextStyle` runs.

# ArtfulType — working notes for Claude

A distraction-free Markdown editor for classic 68k Macs (System 6/7), built
to run from a BlueSCSI on a compact Mac. Cross-compiled with **Retro68** using
its **multiversal** interfaces (not Apple's universal ones — some API and
constant names differ; everything lands in one generated `Multiverse.h`).

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

## Known gotchas (classic Mac — read before touching the Toolbox)

- **`char` is signed — read key codes as `unsigned char`.** `event.message &
  charCodeMask` for an option-accented character is ≥ 0x80 and sign-extends to
  negative, misclassifying as a control key. See the `keyDown` handler in `main.c`.

- **The Writer-mode menu bar is hand-inverted.** There's no Menu Manager API for a
  black menu bar on System 6/7, so `UpdateMenuBarLook` draws the normal bar then
  XOR-inverts it on the **Window Manager port**. Anything that lets the Menu
  Manager repaint the bar — `HiliteMenu(0)` after a menu pick, an alert, a
  StandardFile call — clobbers the inversion, so call `UpdateMenuBarLook()` again
  afterward. That's why it's sprinkled after dialogs.

- **Restore the port.** Disposing a dialog/window doesn't restore the caller's
  `thePort`; the event loop defensively `SetPort(gWindow)` each pass, and dialog
  helpers do it explicitly. Leaving `thePort` at freed memory is a latent crash.

- **Handles move; lock before you dereference across an allocation.** TextEdit's
  `hText` and any `NewHandle` block can relocate. `HLock` around `BlockMove` /
  `*(T*)*h` and `HUnlock` after (see `undo.c`, `zoom.c`). A raw master-pointer held
  across a Memory Manager call is a dangling-pointer bug.

- **Static bitmaps fed to `CopyBits` must be 4-byte aligned.** A plain
  `unsigned char[]` may land on an odd address; a **real 68000 raises an Address
  Error** on the misaligned word/long read (confirmed on a Mac Plus, not
  theoretical). The splash bitmap uses `__attribute__((aligned(4)))` (`splash.c`).

- **`TEGetHeight` is only reliable summed cumulatively from line 0.** Querying a
  single line's height — or reading `(**te).lineHeight` — comes back stale right
  after Enter makes a new empty line. Always `TEGetHeight(n, 0, te)`. It's O(n),
  so the results are cached and invalidated via `InvalidateHeightCache` on the
  rare paths that change a line's height (style, zoom, mode switch, heading
  conversion). See `scrolling.c`.

- **TextEdit's hard limit is 32 KB and `TEInsert` won't enforce it.** The caller
  must. Documents are bounded to `kMaxTELength` (20000, leaving headroom for the
  delimiters re-added in Markdown mode); `DocCanGrowBy` gates every insert/paste.

- **Preferences go in the Preferences folder, never the app's own resource fork.**
  ArtfulType runs from read-only media (a write-locked 800K floppy, a BlueSCSI
  image), where writing back into the running app silently fails. `zoom.c` uses
  `FindFolder` + a small resource file, opens read-only to *load* (works on a
  locked volume) and read/write to *save*, and saves/restores `CurResFile()`
  around every access so the app's own fork stays current for other `GetResource`
  callers.

- **`SIZE` resource (-1) makes the app a MultiFinder citizen.** It sets the memory
  partition and `acceptSuspendResumeEvents`; the `osEvt` handler then dims the
  scrollbar / drops the caret on suspend like a deactivate. It is deliberately
  marked **notHighLevelEventAware**: document opening uses the classic
  `CountAppFiles`/`GetAppFiles` mechanism, not Apple Events — claiming AE
  awareness without handlers would break "double-click a .md in the Finder."

- **Desk-accessory hosting is the app's job on System 6.** DAs opened from the Apple
  menu (`AppendResMenu 'DRVR'` + `OpenDeskAcc`) share the app's layer there, so:
  route `inSysWindow` clicks to `SystemClick`, route the Edit menu / Cmd-keys to
  `SystemEdit` when a DA is frontmost (map items to `undoCmd`/`cutCmd`/… — this
  app's Edit menu is non-standard), and guard `updateEvt`, content clicks and
  keystrokes on `FrontWindow() == gWindow` so the app never draws into or types
  through a DA's window. All of this is a no-op on the System 7 targets where DAs
  live in their own layer. See `main.c`.

- **Modal text dialogs need a filter proc.** `ModalDialog(NULL, …)` gives no
  Return-confirms / Escape-cancels. The link dialog installs a `ModalFilterUPP`
  and `SetDialogDefaultItem` for the OK outline (`markdown.c`). `pascal` callbacks
  (filter procs, control actions, user items) must keep the `pascal` keyword and
  be wrapped in the matching `New…UPP` / disposed with `Dispose…UPP`.

- **Watch cursor for full-document passes.** Any O(n) sweep over the whole document
  (`BuildHiddenView`, `CompactLinkTable`, full styling) shows the watch cursor
  (`SetCursor(*GetCursor(watchCursor))`) and restores `InitCursor()` on every exit
  path — it's slow on a real 68000.

## Peripheral tooling (deploy / packaging)

- `deploy.sh` — builds and copies `ArtfulType.bin` + the guide onto the 20 MB and
  800 K test images (via `hmount`/`hcopy` from hfsutils); wraps the floppy in a
  DiskCopy 4.2 header (`make_diskcopy_image.py`).
- `build-bluescsi-image.sh` — converts the 20 MB volume to a BlueSCSI device image
  (partition map + driver) with `djjr`.
- `package-release.sh` — runs both, then stages release-named images in `Disks/`.

These operate on disk images under `vmac/` (git-ignored) and expect `hfsutils`,
`djjr`, and `python3` on the build host.

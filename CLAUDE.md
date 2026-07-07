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
  callers. `FindFolder` and the FSSpec calls are System 7 only, so `OpenPrefsFile`
  bails via `HasSystem7()` on System 6 — the zoom simply doesn't persist there.

- **System 7-only traps crash the System 6 targets — gate them on `HasSystem7()`.**
  The Mac SE runs System 6.0.8, and on a 68000 an unimplemented A-line trap is not
  a no-op: it drops into the debugger. `FindFolder`, the FSSpec resource calls
  (`FSMakeFSSpec` / `FSpCreateResFile` / `FSpOpenResFile`), and
  `SetDialogDefaultItem` / `SetDialogCancelItem` are all System 7 additions and were
  a real startup crash on a real Mac SE until gated. `HasSystem7()` (`main.c`, via
  `SysEnvirons` — itself safe back to System 4.1) is the gate. The host tests and
  the 68k build are both System-version-blind, so **only real System 6 hardware
  catches this** — prefer a System-6-safe call (`SFGetFile`/`SFPutFile`, not
  `StandardGetFile`) whenever one exists.

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

- **The right side of the menu bar belongs to the system.** On System 7 the Menu
  Manager auto-inserts up to three system menus at the right: the Application
  (switcher) menu, the Help (`?`) menu, and — only when more than one script
  system is installed — a Keyboard menu. The **Application menu is *always
  displayed* and cannot be removed** (Inside Macintosh VI, "With the System
  Menus"). The Help/Keyboard icons *can* be dropped: after the bar is first drawn
  (the `DrawMenuBar` in `MakeMenu` is what makes the MBDF's *calc* routine insert
  them), `DeleteMenu(kHMHelpMenuID)` + a second `DrawMenuBar` removes the Help
  menu, and it stays gone because the ever-present Application menu keeps *calc*
  from re-adding the full set (it only re-adds when *no* system menus remain).
  Gate on `HasSystem7()` — System 6 has no Help menu to remove. The **System 7.5+
  menu-bar clock is *not* a menu**: it's redrawn on a timer by the Date & Time
  control panel (the folded-in SuperClock) and has no Toolbox off-switch, so it
  can only be turned off there — an app cannot hide just the clock without hiding
  the whole bar. See `MakeMenu` in `main.c`.

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

- `build-boot-images.sh` — the primary, **hermetic** disk builder: produces every
  bootable image (800K floppy, 20 MB volume, `HD1_*.hda`) from scratch on Linux
  with no Mac. `release.yml` runs it, so `git tag vX.Y.Z && git push --tags`
  builds and publishes the whole release. Needs `hfsutils`, `djjr`, `python3`.
- `deploy.sh` — older path: copies `ArtfulType.bin` + the guide onto *pre-built*
  20 MB and 800 K base images under `vmac/` (a fast Mini vMac test loop); wraps
  the floppy in a DiskCopy 4.2 header (`make_diskcopy_image.py`).
- `build-bluescsi-image.sh` — converts a 20 MB `vmac/` volume to a BlueSCSI device
  image with `djjr`. `package-release.sh` runs deploy + this, staging into `Disks/`.

- **Blessing an HFS volume in software (no Mac).** `hformat` makes a mountable
  volume but *not a bootable one*: it writes neither the boot blocks nor the
  blessed-folder ID a ROM needs to find the System file. `tools/bless_hfs.py`
  supplies both — it copies the 1024-byte boot blocks (`'LK'`/0x4C4B) verbatim
  from a known-bootable System volume, and writes the System Folder's directory
  ID into the MDB's `drFndrInfo[0]` (MDB at logical block 2 = byte 1024; the
  field is at MDB offset 92, so byte 1116). The blessed dir ID is made
  deterministic by creating the System Folder *first* on a fresh volume, where
  the next catalog node ID is always 16 (asserted from `drNxtCNID`, MDB offset
  30). `hcd :` does **not** return to the volume root in hfsutils, so root-level
  files are copied while cwd is still root, before descending into the folder.
  The committed System 6.0.8 base lives in `disk-base/` (git-ignored `*.dsk`
  except that one file). djjr reports a raw volume as `(bootable)` off the boot
  signature; a *device* image (`.hda`) instead prints its partition map, so
  verify it via the wrapped `HFS Volume` line, not a `bootable` grep. The 800K
  floppy and the 20 MB volume are built the *same* way (`make_blessed_volume`),
  both named `ArtfulType`; only the container is fresh — System, Finder and the
  boot blocks come verbatim from the base. The app's `hasBundle` Finder flag is
  set (and `hasBeenInited` cleared) in its MacBinary header before `hcopy`, with
  the header CRC fixed, or the Finder paints the generic application icon instead
  of ArtfulType's `ICN#`. The 800K floppy is **boot-tested on a real Mac SE**
  (System 6.0.8, booting and launching ArtfulType); the 20 MB `.hda` is
  structurally verified and validated by mounting under an emulator, but not
  System-6 boot-tested there (a Quadra-class `qemu-system-m68k` can't run
  System 6).

---
paths:
  - "app/**/*.{c,h,r}"
---

# Classic Mac (68k, System 6/7) — read before touching the Toolbox

These pitfalls bite when editing the Mac side (`app/*.c`, `*.h`, `main.r`). The
pure core (`app/mdcore.{c,h}`) is Toolbox-free and not subject to them.

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
  partition and `acceptSuspendResumeEvents` (plus `doesActivateOnFGSwitch`, so the
  app owns its own activate/deactivate across a foreground switch). The document
  window fills the whole screen (`plainDBox` over `qd.screenBits.bounds`), so on
  **suspend the `osEvt` handler `HideWindow`s it** (after deactivating) and on
  **resume `ShowWindow`s + reactivates** — otherwise a backgrounded ArtfulType
  would keep covering the Finder desktop and every other app. `Show/HideWindow`
  are original traps and this `osEvt` only fires under MultiFinder, so it's
  System-6-safe. It is deliberately marked **notHighLevelEventAware**: document
  opening uses the classic `CountAppFiles`/`GetAppFiles` mechanism, not Apple
  Events — claiming AE awareness without handlers would break "double-click a .md
  in the Finder."

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

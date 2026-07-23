# Plan — Windowed mode + Preferences

Implements [ADR 0002](../adr/0002-windowed-mode-and-preferences.md). Sequenced so
the low-risk, high-value work lands first and the engine-touching font setting is
last. Each phase builds warning-clean, keeps host tests green, and is committable
on its own.

## Phase 0 — Prep: factor `LayoutWindow` out of `MakeWindow`

No behaviour change; sets up everything after it.

- Extract the `viewRect`/`destRect` + scrollbar-rect math from `MakeWindow`
  (`main.c:128-144`) into `void LayoutWindow(void)` that reads the current
  `gWindow->portRect`, sets both TEs' `viewRect`/`destRect`, `TECalText`s,
  `MoveControl`/`SizeControl`s `gScrollBar`, calls `InvalidateHeightCache()` and
  `AdjustScrollbar()`, then `InvalRect(&gWindow->portRect)`.
- `MakeWindow` calls `LayoutWindow` after creating the window, TEs, and scrollbar.
- **Verify:** app looks identical; host tests + 68k build clean.

## Phase 1 — Generalise preferences storage (System 6 + 7)

Highest value, no UI. Turn `zoom.c` into the preferences module (or add
`prefs.c`; keep zoom's helpers where they are and have them call the shared
loader/saver).

- **Prefs record.** Replace the `'ZLvl'` short with one versioned resource, e.g.
  `'Pref' (128)`:
  ```c
  typedef struct {
      short version;      /* = kPrefsVersion */
      short windowMode;   /* 0 = full screen, 1 = windowed */
      short viewMode;     /* 0 = Markdown, 1 = Writer */
      short fontChoice;   /* index into the validated font table (Phase 4) */
      short zoomIndex;    /* existing 0..kNumZoomLevels-1 */
  } AtPrefs;
  ```
  Clamp every field on load; unknown `version` → ignore and keep defaults. (No
  migration needed — alpha software. Optionally read a stray old `'ZLvl'` once.)
- **`OpenPrefsFile` version fork** (see ADR 0002 §3):
  - System 7 branch = today's `FindFolder` + `FSpCreate/FSpOpenResFile`.
  - System 6 branch: `vRef = LMGetBootDrive()`; save the caller's `GetVol`;
    `SetVol(NULL, vRef)`; `OpenResFile(kPrefsFileName)`, and if `-1` and creating,
    `CreateResFile(kPrefsFileName)` then `OpenResFile`; restore `SetVol` and (as
    today) `CurResFile` on the way out.
  - Confirm `LMGetBootDrive()` is declared (`<LowMem.h>`); fall back to
    `*(short *)0x0210` only if the header lacks it. Add `<LowMem.h>` to `app.h`.
- **`LoadPrefs`/`SavePrefs`** read/write the whole record; keep the existing
  `HLock`/bounds-check discipline from `LoadZoomPref`. Zoom's save becomes a
  `SavePrefs()` call.
- **Globals:** add `gWindowMode`, keep `gHideMarkdown`, `gZoomIndex`; `gFontChoice`
  added in Phase 4 (default Times until then).
- **main():** `LoadPrefs()` replaces `LoadZoomPref()` before `MakeMenu`/`MakeWindow`.
- **Host test:** factor the clamp/validate logic (`AtPrefs` in/out of a byte
  buffer, field clamping) into a pure helper in `mdcore` or a small testable unit
  and add cases under `tests/` — this is the only new pure logic.
- **Verify on real System 6** that zoom now persists across launches (it did not
  before). This path is invisible to CI.

## Phase 2 — Windowed-mode toggle (View menu)

- **Globals/menu:** add `gWindowMode`; add `iFullScreen` to the View menu
  (`"…Default Size/0;(-;Full Screen"`), define the item index in `app.h`,
  checkmark it from `gWindowMode` in `MakeMenu`.
- **`RebuildWindow(Boolean windowed)`** (ADR 0002 §1):
  1. If Writer mode, `SyncHiddenToCanonical()`; stash `selStart/selEnd` and the
     scroll position; remember `gFileName`/`gHaveFile` for the title.
  2. `DisposeControl(gScrollBar)` then `DisposeWindow(gWindow)` (or rely on
     `DisposeWindow` to take the control).
  3. `NewWindow` with `plainDBox` (full screen, `bounds.top += MENU_BAR_HEIGHT`,
     `goAwayFlag` false) or `documentProc` (windowed, `goAwayFlag` false, title =
     doc name); `SetPort`.
  4. Recreate `gTE`/`gHiddenTE` (`TEStyleNew`) and `gScrollBar` (`NewControl`);
     set the base font/size (Phase 4 uses `gFontChoice`, else Times/`FONT_SIZE`).
  5. Restore `gTE` text; if Writer mode `BuildHiddenView()`; set `gActiveTE`,
     `TEActivate`, restore selection + scroll; `LayoutWindow()`.
- **`DoMenuCommand`** `mView` gains `case iFullScreen:` →
  `RebuildWindow(gWindowMode == windowed ? false : true)`, flip `gWindowMode`,
  re-checkmark.
- **Event loop** (`main.c` `mouseDown`): add `FindWindow` parts for windowed mode —
  `inDrag` → `DragWindow(w, event.where, &dragBounds)` (bounds =
  `(**GetGrayRgn()).rgnBBox`); `inGrow` → `GrowWindow(w, event.where, &limitRect)`
  (min ~ margins+a few lines, max = screen) then `SizeWindow` + `LayoutWindow()`.
  No `inGoAway`/`inZoom` (omitted per ADR).
- **`DoUpdate`:** `if (gWindowMode == windowed) DrawGrowIcon(w);` and in
  `LayoutWindow` shorten the scrollbar by the grow-icon height (15px) in windowed
  mode so it doesn't overlap.
- **Title upkeep:** `SetWTitle` on New/Open/Save when windowed.
- **Verify:** toggle both ways with an unsaved doc, a saved doc, in Writer and
  Markdown mode; drag/resize; suspend/resume (osEvt `HideWindow` still fine).

## Phase 3 — Preferences dialog (mode + view + zoom)

Font popup added in Phase 4; wire the other three first.

- **Resources (`main.r`):** `DLOG`/`DITL (137)` — `dBoxProc`, a title StaticText,
  three rows of `StaticText` label + `userItem` (the pop-up hit area), and
  OK/Cancel buttons; plus three `MENU` resources for the pop-up contents. Define
  `kPrefsDialog 137` and item ids in `app.h`.
- **Popups via `PopUpMenuSelect`** (ADR 0002 §2): a `userItem` draw proc frames
  each box and draws the current choice; a click in it calls `PopUpMenuSelect` and
  updates the working copy. **Do not** use the pop-up control CDEF (System 7-only).
- **`DoPreferences()`** (new, in the prefs module): copy current defaults into a
  working `AtPrefs`, run the modal dialog (with a `ModalFilterUPP` for
  Return/Escape like the link dialog), on OK apply + `SavePrefs()`. Applying:
  window-mode change → `RebuildWindow`; view change → `SetViewMode`; zoom change →
  `ApplyZoomIndex`; re-checkmark the menus.
- **Menu:** Edit gains `"…Select All/A;(-;Preferences…"`; define `iPreferences`;
  in `DoMenuCommand` `mEdit` handle it (open ours regardless of DA focus; it is
  not a `SystemEdit` command). No Cmd-key (period-correct).
- **Verify:** each setting persists across relaunch on System 7 and System 6;
  Cancel discards; defaults apply at next launch (window opens in the saved mode,
  saved view, saved zoom).

## Phase 4 — Font preference (engine-touching; last)

- Add `gFontChoice` + a validated font table: `{ "Times", "Geneva", "New York" }`
  (all present on stock System 6), each resolved with `GetFNum`; drop any whose id
  comes back 0/unresolved; index 0 (Times) is the fallback.
- Apply the base font in `MakeWindow`/`RebuildWindow` (`TextFont(chosenFNum)`), and
  add the Font pop-up to the Preferences dialog.
- **Audit `markdown.c`** (`ApplySpanStyles`/heading/code paths): confirm non-code
  runs inherit the port's base font (so changing it "just works") and that code
  runs still force Monaco and headings still use base+delta sizes. Adjust if any
  run writes an explicit Times `tsFont`.
- Changing the font rebuilds the styled view (`BuildHiddenView`) so existing text
  re-renders. **Verify** headings/code/links still render correctly under each font
  and after a zoom change.

## Docs to update (Phase 1 for prefs, Phase 2 for windows)

- `README.md`: drop "zoom persistence is System 7 only"; add windowed mode +
  Preferences to features; note prefs now persist on System 6.
- `.claude/rules/classic-mac-toolbox.md`: rewrite the "Preferences go in the
  Preferences folder… System 6 the zoom simply doesn't persist" bullet to describe
  the two-branch `FindFolder` / `BootDrive` scheme; add the pop-up CDEF vs
  `PopUpMenuSelect` System-6 gotcha.
- `CLAUDE.md`: adjust the one-line prefs summary if it claims System 7-only.
- Bump the version string in `main.r` (`'ArtT' (0)`).

## Risk register

- **`RebuildWindow` TE re-association** — mitigated by reusing the canonical-text
  rebuild rather than re-pointing `TERec.inPort`. Main correctness risk; test all
  mode/view combinations.
- **System 6-only paths** (`BootDrive` prefs, `PopUpMenuSelect` dialog) — not
  covered by CI; require real-hardware/emulator (Mini vMac System 6) verification.
- **Font × style engine** — isolated to Phase 4; ships independently so the rest
  is unaffected if it needs more work.

# ADR 0002 — Optional windowed mode and a real Preferences dialog

- **Status:** Accepted
- **Date:** 2026-07-23
- **Scope:** `MakeWindow`/`EventLoop`/`DoMenuCommand` in `app/main.c`, `app/zoom.c`
  (generalised to a preferences module), a new Preferences dialog, and the
  `main.r` menus/resources.
- **Supersedes nothing; complements [ADR 0001](0001-menu-bar-and-system-menus.md).**

## Context

ArtfulType is a full-screen `plainDBox` window over `qd.screenBits.bounds`
(`MakeWindow`, `main.c`). John Gruber's critique of the upstream app was pointed:
*"I for one would never look twice at a Mac app that didn't use windows.
Full-screen mode just wasn't a thing back then, except for games… not a
Mac-assed 1984 Mac app."*

ADR 0001 already answered half of "be a proper Mac app" by keeping a standard
menu bar (Apple menu, desk accessories, the switcher). Two things it did **not**
address remain:

1. **Windows.** The document is a borderless full-screen box — it cannot be
   moved or resized, and reads as a game/kiosk rather than a Mac application.
2. **Preferences.** A first-class classic Mac app has a Preferences dialog whose
   settings persist in the System Folder. ArtfulType persists exactly one hidden
   setting (zoom), has no Preferences UI, and — because `OpenPrefsFile` uses only
   the System 7 `FindFolder` path — persists **nothing at all on System 6**.

We want to honour the critique without abandoning the distraction-free premise
that is the app's reason to exist.

## Decision

### 1. Full-screen stays the default; windowed is an option

Keep `plainDBox`/full-screen as the shipped default. Add a **View-menu toggle**
("Full Screen", checkmarked) that switches the document window to a **resizable,
draggable** window and back.

- **Windowed proc = `documentProc` (0):** title bar (draggable) + grow box
  (resizable). It opens maximised to the available screen, so "windowed" changes
  the *chrome*, not the initial size — matching the design intent that we still
  use all the space.
- **No go-away box** (`goAwayFlag = false`) and **no zoom box** (`documentProc`,
  not `zoomDocProc`). This is a single-window app: there is nothing to close *to*,
  and a window zoom box would be a second, competing "fill the screen"
  affordance next to the Full-Screen toggle. Both are omitted deliberately, not
  by oversight.
- The window **title shows the document name** ("Untitled" until saved) in
  windowed mode, updated on New/Open/Save. In full-screen there is no title bar.

Switching mode cannot change a live window's `procID`, so the toggle **rebuilds
the window**: it reuses the existing canonical-text path (`SyncHiddenToCanonical`
→ recreate window + TextEdit records + scrollbar → restore `gTE` text →
`BuildHiddenView` → restore selection). That path is already exercised by
open/undo, so the rebuild leans on tested machinery rather than trying to
re-point live `TERec.inPort` fields into a new port.

Resizing (grow) and the rebuild share one **`LayoutWindow`** routine that
recomputes the TextEdit `viewRect`/`destRect` from the current `portRect`,
re-wraps (`TECalText`), repositions the scrollbar, invalidates the height cache
(`InvalidateHeightCache`), and recomputes the scrollbar range. In windowed mode
the scrollbar stops short of the grow icon and `DoUpdate` calls `DrawGrowIcon`.

### 2. A real Preferences dialog, persisted on **both** System 6 and 7

Add **Edit ▸ Preferences…** (the System 7 HIG home for it) opening a dialog with
four settings, each a pop-up menu:

| Setting     | Options                                   | Default      |
|-------------|-------------------------------------------|--------------|
| Open in     | Full Screen / Windowed                    | Full Screen  |
| View        | Writer / Markdown                         | Writer       |
| Font        | Times / Geneva / New York / … (validated) | Times        |
| Zoom        | the existing five levels                  | Default (0)  |

All four are *startup defaults*; the View and Full-Screen menu items still toggle
live within a session. The dialog's pop-ups are built with **`PopUpMenuSelect` on
`userItem`s**, *not* the pop-up control CDEF (`popupMenuProc`, procID 1008) —
that CDEF is a System 7 addition and would crash System 6, whereas
`PopUpMenuSelect` is present on our SE-class ROM / System 4.1+ target. This keeps
the whole dialog working on System 6, consistent with the app's "runs on the
Mac SE" commitment.

### 3. Preferences storage: System-Folder location by OS version

`OpenPrefsFile` gains a version fork, and both branches use only calls safe on
their target:

- **System 7 (`HasSystem7()`):** `FindFolder(kOnSystemDisk,
  kPreferencesFolderType, …)` → `FSpCreateResFile`/`FSpOpenResFile` in the
  **Preferences folder**. This is the existing zoom path. Inside Macintosh VI is
  explicit that on 7.0 you must *not* drop files at the top of the System Folder
  and must use `FindFolder`.
- **System 6 (else):** locate the blessed **System Folder** through the
  low-memory global **`BootDrive`** (`$0210`), read via the `LMGetBootDrive()`
  accessor. Inside Macintosh IV, File Manager, verbatim: *"it should always
  create it in the directory containing the system folder. The working directory
  reference number for this directory is stored in the global variable BootDrive;
  you can pass it in ioVRefNum."* We `SetVol` to that WDRefNum and
  `CreateResFile`/`OpenResFile` the prefs file **loose in the System Folder**
  (System 6 has no Preferences subfolder — that is a 7.0 invention).
  `LMGetBootDrive`/`SetVol`/`GetVol`/`CreateResFile`/`OpenResFile` are all
  original traps, safe on the 68000.

The prefs file becomes one **versioned record** (window mode, view mode, font,
zoom) rather than the single `'ZLvl'` short. Every loaded field is range-clamped
to its valid set; a missing or unreadable prefs file leaves the built-in
defaults untouched (unchanged behaviour). Reads open read-only so they still work
on a write-locked volume; only the save path asks for write access.

## Alternatives considered

- **Windowed mode that is draggable but not resizable (`noGrowDocProc`).**
  Rejected. A Mac window you can move but not resize is exactly the kind of
  half-measure the critique is about; the grow box is what makes it read as a
  real window, and `LayoutWindow` is needed for the mode rebuild anyway, so
  resize is nearly free once that exists.
- **A zoom box (`zoomDocProc`) instead of / in addition to the Full-Screen
  toggle.** Rejected for v1 — two "maximise" affordances for one window is
  confusing. Could be revisited if the Full-Screen menu item is later dropped in
  favour of the zoom box.
- **Pop-up menus via the standard pop-up control CDEF.** Rejected: the CDEF is
  System 7-only and would crash the Mac SE. `PopUpMenuSelect` on user items is
  the System-6-safe classic technique.
- **Keeping preferences System 7-only** (today's behaviour). Rejected now that
  the `BootDrive` mechanism is documented — persisting on System 6 is both doable
  and *more* period-authentic (it is how 1980s apps found the System Folder).
- **Radio-button groups instead of pop-ups.** Rejected — four settings' worth of
  radio clusters is a large dialog; pop-ups are tighter and equally period-correct.

## Consequences

- The app is a proper Mac citizen on demand: a movable, resizable window with a
  titled document, while the distraction-free full-screen mode remains the
  default and the app's identity.
- Preferences now persist on **System 6 and System 7** — the "zoom doesn't
  persist on System 6" limitation is removed. `README`, CLAUDE.md, and the
  `classic-mac-toolbox` rule that assert "System 7 only" must be corrected.
- New per-OS code paths that only real hardware fully exercises: the System 6
  `BootDrive` path and the System 6 `PopUpMenuSelect` dialog are invisible to the
  host tests and the version-blind 68k CI build, so they need checking on a real
  System 6 machine (as the FindFolder crash once did — see ADR 0001 lineage and
  the `classic-mac-toolbox` rule).
- The window rebuild reuses the canonical-text round-trip, so a mode switch is an
  O(n) rebuild (like open/undo) — acceptable for a rare, explicit user action,
  and it keeps us off fragile `TERec.inPort` surgery.
- **Font** is the one setting that touches the style engine (heading-size math,
  the code=Monaco convention) and depends on which fonts a given boot disk ships;
  it is sequenced last and its option list is validated with `GetFNum` so an
  absent font falls back to Times.

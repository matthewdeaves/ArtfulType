# darkcruix2/ArtfulType — fork review

Research date: 2026-07-23. Repo: <https://github.com/darkcruix2/ArtfulType>
(cloned to `/tmp/darkcruix2-artfultype`, tip `326a35a`, tag `v0.1.2-alpha`).

## 1. What this fork is

A heavily-reworked fork of ActionRetro/ArtfulType. It branched from **upstream
commit `7cc0b04a` ("Add video overview to README")** and then diverged hard:
~7,000 insertions across `main.c`, `markdown.c`, `scrolling.c`, etc. Note our
own fork sits on a *later* upstream base (`6a2f127`), so darkcruix2 does **not**
have some upstream fixes we do, and vice-versa — this is a parallel fork, not a
descendant of ours.

Two big structural choices distinguish it from both upstream and us:

1. **It replaced TextEdit with WASTE** (the WorldScript-Aware Styled Text
   Engine — `app/waste/WASTE.c`, `.h`). Every editor call is `WEKey`/`WEInsert`/
   `WEGetText`/`WESetSelect` instead of TextEdit. WASTE's motive is escaping
   **TextEdit's hard 32 KB limit** (the fork advertises 256 KB files in Pro).
   Our fork is still TextEdit-based (`gTE`/`gHiddenTE`), so none of their editor
   code lifts directly — only the *ideas* and the pure parse logic do.
2. **It ships two builds from one source via `#ifdef ARTFUL_PRO`** (19 sites in
   `main.c`): a "standard" build and an **"ArtfulType Pro"** build. Pro requires
   **a 16 MHz 68030 (SE/30), 16 MB RAM, and System 7.1+** and adds
   multi-document/multi-window, a Window menu, a status bar, an in-window icon
   toolbar, and 256 KB files.

It has **no `mdcore`-style pure core** — all markdown parsing lives inside
`markdown.c` tangled with WASTE calls. There are no issues (disabled), no PRs,
and no host tests. Docs it wrote about itself live in `docs/` and `doc/`.

**License: GPLv3** (code) + all-rights-reserved creative assets — **same as
ours**, so we may lift code verbatim, not just reimplement ideas (keep
attribution/history; creative assets are off-limits).

### UI critique — is it Mac-assed?

The **standard** build (screenshot1) is clean and genuinely Mac-assed: bare
window, serif body text, a normal `File / Edit / Style / View` menu bar, nothing
floating. That is the good ArtfulType look.

The **"Pro"** build (screenshot2) is the "nasty" one, and the user is right. It
crams a **non-Mac in-window icon toolbar** across the top of the *document*
window — a Save glyph, `B`, `I`, an `M↓` view-toggle, a refresh glyph — plus
two square **"Top" and "End" push buttons** sitting inside the content area.
1984–1991 Mac apps did not put button toolbars or navigation push-buttons in the
document window; that is a mid-90s/Windows idiom and it fights the HIG (the
scroll bar and Cmd-keys already do "go to top/end"). The bottom **status bar**
(`[Writer]  Chars: N  Line: N  Col: N`) is the least offensive Pro addition and
is defensible if kept subtle and optional. Multi-document with a real **Window
menu** is properly Mac-assed — that part follows the HIG correctly.

## 2. Candidate features (ranked)

| # | Feature | What it does | Useful? | Mac-assed? | System 6 safe? | Effort | Recommendation |
|---|---------|--------------|---------|------------|----------------|--------|----------------|
| 1 | **@today / @time expansion** | Typing `@today`/`@time` auto-replaces with `YYYY-MM-DD` / `HH:MM` | High | Yes (invisible; `GetDateTime`) | **Yes** | S | **Lift** |
| 2 | **UTF-8 → MacRoman import cleanup** | On Open: strips BOM, CRLF/LF→CR, maps smart quotes/dashes/ellipsis/bullet to MacRoman | High | Yes (invisible) | **Yes** (byte munging) | S | **Lift** |
| 3 | **Find (Cmd-F)** | Standard dialog, scans buffer for a string, selects match | High | Yes (dBoxProc dialog) | **Yes** (plain `memcmp` scan) | M | **Adapt** |
| 4 | **Find & Replace / Replace All** | Two-field dialog, single + global replace | High | Yes (standard dialog) | **Yes** | M | **Adapt** |
| 5 | **Line-start/end + doc-top/bottom + PageUp/Down nav** | Home/End, Cmd-←/→/↑/↓, Page keys | High | Yes | **Yes** (keyCode handling) | S | **Lift** |
| 6 | **Bullet / numbered / nested lists** | `- `, `1. `, indented sublists render with real bullets (•/○/-) | High | Yes | **Yes** (pure parse) | L | **Adapt** |
| 7 | **Blockquote rendering** | `> ` (nestable) shown indented + italic, delimiter hidden | Med-High | Yes | **Yes** (pure parse) | M | **Adapt** |
| 8 | **Horizontal rule** | `---` renders as a drawn line | Med | Yes | **Yes** | S | **Adapt** |
| 9 | **Fenced code block** | ` ``` ` multi-line Monaco block | Med | Yes | **Yes** | M | **Adapt** |
| 10 | **Highlight `==text==`** | Marks a run as highlighted | Med | Partly (needs gray bg; they overload the `outline` face bit) | **Yes** | M | **Adapt** |
| 11 | **Word/char count** | They show `Chars:` in the status bar; add real word count | Med | Yes | **Yes** (pure count over buffer) | S | **Build in mdcore** |
| 12 | **Optional status bar** | Mode + char + line/col at window bottom, View-menu toggle | Med | Borderline (OK if subtle + optional) | **Yes** | M | **Adapt (optional)** |
| 13 | **Checkbox list `- [ ]` / `- [x]`** | Task-list rendering | Med | Yes | **Yes** | M | **Adapt** |
| 14 | **Markdown-view line-number gutter** | Numbers down the left of raw view | Low-Med | Borderline | **Yes** | M | **Skip / later** |
| 15 | **Multi-document + Window menu** | Several docs/windows, per-doc undo & state | High | Yes (Window menu is correct HIG) | **No, needs work** (their impl = WASTE 256 KB + 16 MB + Sys 7.1) | L | **Adapt (long-term)** |
| 16 | **WASTE text engine (>32 KB / 256 KB files)** | Removes TextEdit's 32 KB ceiling | High | N/A (engine) | Needs verification on real Sys 6 | L | **Investigate** |
| 17 | Serif / Sans-serif body toggle | View-menu font switch | — | Yes | Yes | — | Already planned (Preferences → Font) |
| 18 | Resizable windows | Draggable/growable window | — | Yes | Yes | — | Already planned |
| 19 | In-window icon toolbar + Top/End buttons | Button strip in the document window | Low | **No — un-Mac** | Yes | — | **Do NOT copy** |

## 3. How the "Lift"/"Adapt" items fit our architecture

- **@today/@time (Lift, #1):** trivial. In our key handler, after a content key,
  check the 5–6 bytes behind the caret in `gActiveTE`'s text; on match, select
  and replace with a `GetDateTime`/`SecondsToDate` string. Pure-Toolbox, no Sys 7.
  The match/format step can live in `mdcore` as a `char*`-in/`char*`-out helper
  and be host-tested.

- **UTF-8 import cleanup (Lift, #2):** our `file.c` load path already produces a
  Markdown buffer; drop their conversion loop in right after `FSRead`, before it
  reaches `gTE`. It's a self-contained byte transform — ideal `mdcore` function
  (`MdNormalizeImport(buf,len)`), fully host-testable, no Toolbox.

- **Find / Replace (Adapt, #3–4):** add DITL/DLOG `dBoxProc` dialogs to `main.r`
  and a pure `MdFind(haystack,len,needle,from)` in `mdcore` (their
  `FindTextInHandle` is a plain scan). The Mac adapter locks `gActiveTE`'s handle,
  calls the core, then `TESetSelect`/scrolls to the hit; Replace All loops on the
  canonical Markdown buffer and rebuilds the view. All System-6-safe. Decide
  case-sensitivity (theirs is exact-match).

- **Navigation keys (Lift, #5):** pure keyCode handling in the event loop
  (`0x73` Home, `0x77` End, `0x74/0x79` PageUp/Down, Cmd+arrows). Maps onto our
  `TESetSelect` + scroll helpers; no Sys 7. Small, high polish-per-line.

- **Lists / blockquote / HR / code block / checkbox / highlight (Adapt, #6–10,
  13):** these are all *block-level* markdown our `mdcore` strip/emit path
  doesn't yet model. This is where the real work (and value) is. Implement the
  parse/emit in `mdcore` (host-tested), then extend the Mac adapter's
  span→`TextStyle` mapping. **Highlight fits our existing convention cleanly:**
  we already stash strike in `tsColor.green` and link IDs in `tsColor.red`, so a
  highlight flag is another `tsColor` channel bit, drawn as a gray-pattern
  background overdraw (same pattern as our `DrawStruckRuns`). Their trick of
  overloading the `outline` `tsFace` bit is a shortcut we needn't copy since we
  have the color channels. HR is the smallest; lists/blockquote nest and are the
  largest.

- **Word count (Build, #11):** a pure counter over the Markdown buffer in
  `mdcore`; surface via a menu item or the optional status bar. Host-testable.

- **Status bar (Adapt, #12):** worth having but keep it **off by default**,
  toggled from the View menu (as they do), drawn subtly in Geneva 9 at the window
  bottom. Recompute line/col only on idle to avoid their per-keystroke full
  buffer scan. System-6-safe.

- **Multi-document (Adapt, #15):** the *right* Mac model — a `DocumentRecord`
  with per-window state and a Window menu — but their implementation is bound to
  WASTE + a 16 MB / 68030 / Sys 7.1 envelope, so it is **not System-6-safe as
  shipped**. A Sys-6-safe version would need: a document struct replacing our
  globals; per-document TextEdit pairs (mind the 32 KB × N memory on a 1–4 MB
  SE); a Window menu built with plain `NewMenu`/`AppendMenu` (no Sys 7 traps
  needed — Window menus predate Sys 7); and careful `DisposeWindow` cleanup.
  Big, but each piece is Sys-6-doable. Long-term.

- **WASTE (Investigate, #16):** TextEdit's 32 KB limit is our real ceiling for
  long documents; WASTE removes it. But swapping engines is a rewrite of every
  editor call and our style-run encoding, and we'd have to confirm the bundled
  WASTE build runs on a real 68000/System 6.0.8 (WASTE historically targeted
  System 7). Treat as a research spike, not a quick lift.

## 4. Do NOT copy (un-Mac / nasty)

- **The in-window icon toolbar** (Save/B/I/M↓/refresh glyph strip across the top
  of the document window). No 1984–1991 Mac app did this; it belongs to menus.
- **The "Top" and "End" push buttons** planted in the content area. The scroll
  bar and Cmd-↑/↓ already do this the Mac way; buttons in the text region break
  the HIG and eat writing space.
- **Any always-on chrome by default.** If we adopt the status bar, it must be
  optional and quiet — the whole point of ArtfulType is a distraction-free page.
- **The tangled `markdown.c`-does-everything structure.** Don't import their
  parsing wholesale into our adapter; re-express block parsing in `mdcore` so it
  stays pure and host-tested (our architecture's whole premise).

## 5. Licensing

darkcruix2/ArtfulType is **GPLv3 for code** (`LICENSE`) with creative assets
reserved (`ASSETS_LICENSE`) — **identical terms to our fork**. We may copy code
verbatim (preserving history/attribution) or reimplement ideas; either is
GPL-compatible. Do **not** copy their icons/screenshots/branding artwork.

# Plan — Content features lifted from the darkcruix2 fork

> **Implementation status (updated 2026-07-23, cycle 2).** Phase 1 (import
> cleanup, `@today`/`@time`, nav keys), Phase 2 (Find / Replace All), and the
> Word Count cross-cutting item are **implemented, reviewed, and shipping** — the
> pure parts (`MdNormalizeImport`, `MdFind`, `MdWordCount`) are in `mdcore` with
> host tests (`tests/test_text.c`).
>
> **Phase 3a (design spike) is done** and recorded in
> [ADR 0003 — "Design-spike outcome"](../adr/0003-content-features-from-darkcruix2.md).
> Its conclusion: the tier splits by *round-trip risk* (does the feature hide
> line-structure text?), not by block-vs-inline.
>
> **Phase 3, item 2 — `==highlight==` — is now implemented, reviewed, and
> shipping.** It's inline and round-trip-safe, so it rides the proven span → run →
> `MdStyleFields` pipeline (blue channel; `DrawHighlightRuns` `patOr` overdraw;
> **Style ▸ Highlight**). The style-codec host test now covers all **64**
> attribute combinations. **Remaining Phase 3 items are still deferred** because
> they hide line structure (blockquote, fenced code, lists) or need a
> non-colliding rule renderer (HR): they carry real round-trip risk and are best
> validated one at a time on real System 6 hardware. Next unit of work, smallest
> first: blockquote → fenced code → HR → lists/checkbox.
>
> **Architecture follow-on (candidate C, 2026-07-23 review):** the `main.c`
> `keyDown` arm is lengthening (doc-cap gating, `ScrollByKey`, `TEKey`,
> `DetectInlineMarkdown`, `ExpandDateKeyword`, and — when lists land — Return
> continuation). A future `HandleContentKey()` front door would give the
> keystroke story one home. **Flagged, not scheduled**; revisit when list
> Return-handling makes it a further concern.

Implements [ADR 0003](../adr/0003-content-features-from-darkcruix2.md). Three
tiers, quick-wins first, block-level Markdown last. Independent of the
windowing/Preferences work ([plan 0001](0001-windowed-mode-and-preferences.md)) —
either can ship first. Every phase: warning-clean 68k build, host tests green,
committable on its own, with darkcruix2 credited where ideas/snippets are used.

Architecture reminder: pure logic goes in `mdcore` (`app/mdcore.{c,h}`,
host-tested under `tests/`); `markdown.c`/`main.c`/`file.c` are the thin Mac
adapter. Nothing here may use a System-7-only trap without a System-6 path — but
in practice all of this is pure logic + original traps, so it is System-6-safe.

## Phase 1 — Quick wins (all Small, System-6-safe)

### 1a. UTF-8 → MacRoman import cleanup
- **Pure:** `long MdNormalizeImport(char *buf, long len)` in `mdcore` — strips a
  UTF-8 BOM, normalizes CRLF/LF → CR, and maps common UTF-8 sequences (smart
  quotes ‘’“”, en/em dashes, ellipsis, bullet) to their MacRoman bytes, in place,
  returning the new length. Fully host-testable; add `tests/` cases with known
  UTF-8 input → expected MacRoman bytes.
- **Adapter:** call it in `file.c` `ReadFile` on the `*textH` buffer **after
  `FSRead`, before `TEInsert`** (`file.c:120-127`). Re-check the length against
  `kMaxTELength` after normalization (it only shrinks, so it stays safe).

### 1b. `@today` / `@time` expansion
- **Pure:** a formatter helper in `mdcore` that, given a date/time (passed in as
  numbers — mdcore stays Toolbox-free), returns the `YYYY-MM-DD` / `HH:MM` string;
  plus a matcher that reports whether the bytes ending at the caret are `@today`
  or `@time`. Host-tested.
- **Adapter:** in the `main.c` `keyDown` content-key path (after `TEKey`, near
  `DetectInlineMarkdown`, `main.c:338-343`), read the few bytes behind the caret;
  on a match, `GetDateTime`/`SecondsToDate`, `TESetSelect` over the keyword, and
  `TEDelete`+`TEInsert` the formatted string. Respect `DocCanGrowBy`.

### 1c. Classic navigation keys
- **Adapter only:** extend the `main.c` `keyDown` handler to act on keyCodes
  (`(event.message & keyCodeMask) >> 8`): Home `0x73`, End `0x77`, PageUp `0x74`,
  PageDown `0x79`, and Cmd-←/→/↑/↓ for line/doc ends. Map to `TESetSelect` +
  our existing `ScrollCaretIntoView`/scroll helpers; extend selection when Shift
  is down. No new pure code. Verify against the existing signed-char handling note
  in the toolbox rule (use the keyCode, not the charCode, for these).

**Phase 1 verify:** open a UTF-8 file saved on a modern Mac (curly quotes/dashes
come through clean); `@today`/`@time` expand; navigation keys move/scroll as
expected in both Writer and Markdown mode.

## Phase 2 — Find / Find & Replace (Medium, System-6-safe)

- **Pure:** `long MdFind(const char *hay, long hayLen, const char *needle,
  long needleLen, long from, int caseSensitive)` in `mdcore` → offset of the next
  match at/after `from`, or -1. Add a case-fold helper for MacRoman. Host-tested
  (ASCII + accented, wrap-around handled by the caller).
- **Resources (`main.r`):** `DLOG`/`DITL` for **Find** (one field + Find/Cancel)
  and **Find & Replace** (two fields + Find/Replace/Replace All/Cancel), both
  `dBoxProc`, with a `ModalFilterUPP` for Return/Escape (as the link dialog does).
  Define ids in `app.h`.
- **Adapter:** Find operates on `gActiveTE`'s locked `hText`; on a hit
  `TESetSelect` + `ScrollCaretIntoView`. **Replace All** operates on the canonical
  `gTE` Markdown buffer (sync first if in Writer mode), loops `MdFind`+splice,
  then `RefreshActiveView` — show the watch cursor for the sweep (toolbox rule).
- **Menus:** add **Find… (Cmd-F)**, **Find Again (Cmd-G)**, **Replace… (Cmd-R)**
  to the Edit menu (below Select All, after a separator); define item ids; wire in
  `DoMenuCommand`'s `mEdit`. These are our commands, not `SystemEdit` ones. Keep a
  small `gLastSearch`/`gLastReplace` for Find Again.
- **Decisions:** default case-insensitive with no "match case" box for v1 (keep
  the dialog small); wrap-around search with a beep at the end.

**Phase 2 verify:** find/replace across a multi-page doc in both modes; Replace
All count correct; Cmd-period/Escape cancels; no match beeps.

## Phase 3 — Block-level Markdown (Large; design spike first)

The high-value, high-effort tier. See ADR 0003's "Key technical constraint":
render **within the text/glyph model** (TextEdit has no paragraph indentation).

### 3a. Design spike (do before coding)
Decide the concrete Writer-view rendering + round-trip for each block type under
TextEdit's limits, and how `mdcore` represents block structure (extend `MdStrip`
with a line/block pass, or add a parallel `MdStripBlocks`). Confirm MacRoman glyph
availability (bullet `$A5` yes; em-dash `$D1` yes; ballot box **no** → checkbox
compromise). Write the chosen model into ADR 0003 or a short spike note.

### 3b. Implement, smallest-first (each: pure `mdcore` + host tests, then adapter)
1. **Horizontal rule** (`---`/`***`/`___` on their own line) — ✅ **DONE
   (2026-07-23).** Detected by content (pure `MdIsHorizontalRule`, host-tested)
   and drawn by `DrawHrRuns` overdraw — no text mutation, no style channel, so
   the round-trip is safe by construction. Reveal-on-active-line keeps the
   markers editable; prints via the print path too.
2. **Highlight `==text==`** — ✅ **DONE (2026-07-23).** `MD_KIND_HIGHLIGHT` +
   the `tsColor.blue` flag in `MdStyleFields`/`MdRunToFields`; adapter
   `DrawHighlightRuns` paints a light-gray `patOr` stipple mirroring
   `DrawStruckRuns` (but background, glyphs preserved). Host round-trip test now
   covers 64 combinations; live-typing (`MdDetectInline` for `=`) and a
   **Style ▸ Highlight** menu item included.
3. **Fenced code block** (` ``` `) → multi-line Monaco run, fences hidden.
4. **Blockquote `> `** (nestable) → space-indent + italic run, `> ` hidden;
   optional left-bar overdraw.
5. **Lists** — bullet (`- `/`* ` → `•`), numbered (`1. `), **nested** via leading
   spaces, and **checkbox** (`- [ ]`/`- [x]`, rendered per the glyph compromise).
   Largest; do last.

Each item extends `MdStrip` (hide delimiter, record span/attribute) and
`MdEmitInline`/emit (restore canonical Markdown), then the adapter's
span→`TextStyle` mapping. Keep `MdSpansToRuns`' combined-write invariant intact so
block + inline styles coexist.

**Phase 3 verify:** every block type round-trips losslessly Writer↔Markdown
(extend the host round-trip tests); nested lists/quotes indent correctly; struck
/ bold / linked text inside a list item still combines; print output (page-break
planner) still correct with the new line heights.

## Cross-cutting — Word count

- **Pure:** `long MdWordCount(const char *buf, long len)` (and char count) in
  `mdcore`, host-tested.
- **Adapter:** a **Word Count…** menu item (Edit menu) showing a small alert with
  words/characters — the classic Mac word-processor form. No always-on status bar
  in v1 (ADR 0003). Runs on the canonical `gTE` buffer.

## Docs & attribution (as each tier ships)

- `README.md`: add the shipped features (Find/Replace, lists/blockquote/HR/code
  block/highlight, import cleanup, word count) to the feature list.
- Credit **darkcruix2/ArtfulType** for the ideas/snippets in the relevant commit
  messages (and a `NOTICE`/attribution line if code is lifted). Do not copy their
  assets.
- Keep the 68k build warning-clean and cppcheck green (inline-suppress with a
  rationale if needed, per CLAUDE.md).

## Risk register

- **Block rendering within TextEdit's no-indent model** (Phase 3) — the main risk;
  the design spike (3a) de-risks it before code. Fallback: ship the styling-only
  blocks (HR, highlight, code block) first and treat indent-dependent ones
  (lists, blockquote) as a follow-on if indentation proves too costly.
- **MacRoman glyph gaps** — checkbox has no native glyph; accept a legible
  compromise rather than a font hack.
- **Full-buffer scans on a 68000** (Find/Replace All, word count) — show the watch
  cursor; these are already O(n) operations the app performs elsewhere.
- **Deferred, tracked elsewhere:** WASTE engine swap (32 KB ceiling) and
  System-6-safe multi-document + Window menu — both L-effort, out of this plan.

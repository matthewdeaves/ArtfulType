# ADR 0003 — Adopt the useful, Mac-assed, System-6-safe features from the darkcruix2 fork

- **Status:** Accepted
- **Date:** 2026-07-23
- **Scope:** `app/mdcore.{c,h}` (new pure block-level + text helpers),
  `app/markdown.c` (adapter), `app/file.c` (import), `app/main.c` (menus, key
  handling), `main.r` (dialogs/menus). Sequenced in
  [plan 0002](../plans/0002-content-features.md).
- **Input:** [darkcruix2 fork review](../research/darkcruix2-fork-review.md).

## Context

The parallel fork **darkcruix2/ArtfulType** (GPLv3 — license-compatible with ours,
so code may be lifted with attribution, not only reimplemented) carries editing
and Markdown features our fork lacks. But it comes wrapped in three things we do
**not** want:

1. **A "Pro" tier bound to System 7.1 / 68030 / 16 MB.** Its headline features
   (256 KB files, multi-document) ride on **WASTE** replacing TextEdit and on a
   System 7.1 envelope. Our fork's defining constraint is that it runs on
   **System 6.0.8 on a stock 68000** (Mac SE). Anything System-7.1-only is out
   unless it has a System-6 path.
2. **A non-Mac "Pro" UI.** An in-window **icon toolbar** (Save/B/I/M↓/refresh) and
   **"Top"/"End" push buttons** inside the document window — a mid-90s/Windows
   idiom that violates the classic HIG (menus and the scroll bar already do this).
3. **A tangled implementation.** Its Markdown parsing lives inside the editor
   code, interwoven with WASTE calls, with no pure core and no tests.

Meanwhile ArtfulType's whole architecture is a **pure `mdcore`** (Toolbox-free,
host-tested) plus a thin Mac adapter. We want the *features*, expressed our way.

## Decision

Adopt a curated subset, **reimplemented in `mdcore`** (pure, host-tested) with a
thin adapter — never by importing their WASTE-coupled editor code — in three
tiers (see plan 0002):

1. **Quick wins (System-6-safe, small):** UTF-8→MacRoman **import cleanup** on
   Open; **`@today`/`@time`** keyword expansion; classic **navigation keys**
   (Home/End, PageUp/Down, Cmd-arrows).
2. **Find / Find & Replace:** standard `dBoxProc` dialogs plus a pure buffer scan
   (`MdFind`) in `mdcore`. No System 7 traps.
3. **Block-level Markdown we don't model yet:** horizontal rule, `==highlight==`,
   fenced code block, blockquote, and lists (bullet/numbered/nested, checkbox) —
   parsed/emitted in `mdcore`, rendered by the adapter.

Plus a pure **word-count** helper surfaced via a menu item.

**Rejected outright** (recorded so no one "helpfully" adds them):

- The in-window **icon toolbar** and **"Top"/"End" push buttons** — un-Mac; keep
  everything in menus and the scroll bar.
- **Any always-on chrome by default.** If a status bar is ever added it must be
  off by default and quiet (a distraction-free page is the point).
- **Importing their editor/parse code wholesale** — it would drag WASTE coupling
  and defeat the pure-core architecture.

**Deferred (not rejected):**

- **WASTE adoption** (to beat TextEdit's 32 KB ceiling) — an engine rewrite of
  every editor call and our style-run encoding; must first be *proven* on a real
  68000/System 6.0.8, since WASTE historically targeted System 7. A research
  spike, tracked separately.
- **Multi-document + Window menu** — the correct Mac model, but their version is
  WASTE/256 KB/16 MB/Sys 7.1. A System-6-safe version (a `DocumentRecord`
  replacing our globals, per-document TextEdit pairs, a plain
  `NewMenu`/`AppendMenu` Window menu — Window menus predate System 7) is an
  L-effort item for later.

## Key technical constraint this ADR commits to

**Classic styled TextEdit has no paragraph margins or indentation.** So the
block-level features render **within the text/glyph model or via adapter
overdraw**, not by assuming a richer engine:

- **Lists:** the leading `- `/`* ` becomes a real bullet character (`•`, MacRoman
  `$A5`) in the Writer view; nesting is leading spaces; the delimiter round-trips
  back to `- ` in Markdown mode. Numbered lists keep `1. `.
- **Horizontal rule:** drawn as a run of a rule-like MacRoman character (em-dash
  `$D1` / underscore) filling the line — no custom paragraph object needed.
- **Blockquote:** leading-space indent plus an italic run; the `> ` is hidden in
  Writer view and restored on emit. (A drawn left bar, if wanted, is adapter
  overdraw like `DrawStruckRuns`.)
- **Fenced code block:** a multi-line Monaco run; the ` ``` ` fences hidden in
  Writer view.
- **Highlight `==text==`:** fits our existing style-field convention — we already
  pack link IDs into `tsColor.red` and the strike flag into `tsColor.green`, so
  highlight is another `tsColor`-channel flag, drawn as a gray-pattern background
  overdraw (same mechanism as `DrawStruckRuns`). We need **not** copy their trick
  of overloading the `outline` `tsFace` bit.
- **Checkbox lists:** MacRoman has no ballot-box glyph, so render `- [ ]`/`- [x]`
  as `[ ]`/`[x]` kept legible (or a bullet + trailing marker), round-tripping to
  the canonical form. Noted as a rendering compromise.

This is precisely why these features do **not** require WASTE — they live in the
Markdown text and its style runs, which TextEdit already handles.

## Alternatives considered

- **Adopt WASTE now to unlock the block features and big files together.**
  Rejected for this ADR — couples useful, low-risk content features to a risky
  engine swap and a System-6 unknown. Decoupled: features now, WASTE as its own
  spike.
- **Lift their code verbatim.** License permits it, but it is WASTE-coupled and
  non-pure; reimplementing in `mdcore` keeps the host-tested core intact. We still
  credit darkcruix2 in commit messages / a NOTICE for the ideas and any snippet.
- **Add a status bar / word count as always-on chrome (as Pro does).** Rejected as
  default; word count via a menu item is the Mac-assed form.

## Consequences

- `mdcore` grows a block-level Markdown model (strip/emit + a run/line
  representation for blocks) and a few pure text helpers (`MdNormalizeImport`,
  `MdFind`, date/keyword expansion, word count) — all host-tested, keeping the
  "logic lives in the pure core" invariant.
- Every adopted feature is System-6-safe (pure logic + original Toolbox traps);
  nothing here reintroduces a System-7-only dependency. Find/Replace uses
  `dBoxProc` dialogs (safe), navigation uses raw keyCodes (safe).
- Block-level rendering within TextEdit's no-indent reality is the main design
  risk and gets a design spike before implementation (plan 0002, Phase 3).
- Attribution: darkcruix2/ArtfulType is credited where ideas/snippets are used;
  their creative assets (icons/screenshots/branding) are **not** copied.

## Design-spike outcome (Phase 3a) and staged delivery — 2026-07-23

The block-level tier was spiked before coding, as this ADR required. Findings:

**The tier splits by round-trip risk, not by "block vs inline".** The dividing
line is whether a feature **hides line-structure text** in the Writer view:

- **Inline / styling-only (round-trip-safe, no line hidden):** `==highlight==`.
  It never removes line structure, so the Writer buffer's text equals the
  canonical text and the round-trip is inherently lossless — it rides the exact
  span → run → `MdStyleFields` pipeline that bold/strike already use. **Shipped
  this cycle.**
- **Line-hiding blocks (real round-trip risk):** blockquote (`> ` hidden per
  line), fenced code (whole ` ``` ` lines hidden), and lists (marker hidden,
  indentation synthesised). Each changes the Writer text vs. canonical and must
  re-insert the hidden structure on emit — the delicate part, best validated one
  block type at a time on real System 6 hardware. **Deferred, still tracked.**
- **Horizontal rule** turned out to sit *between*: rendering it cleanly wants the
  `---` glyphs hidden, but the only hide mechanism (a white `tsColor`) collides
  with the red/green/blue encoding channels. Left with the line-hiding group
  until a non-colliding rule-drawing approach is chosen (adapter overdraw of a
  line across a specially-flagged run is the likely route).

**Channel map locked (candidate B of the 2026-07-23 architecture review):** the
three repurposed `tsColor` channels are now fully allocated —
`red` = link ID (1..64), `green` = strike flag, `blue` = **highlight** flag —
all independent. `MdRunToFields`/`MdFieldsToRun` remain the single encoder; the
host round-trip test grew from 32 to **64** combinations to cover the new bit.
Any future block attribute that isn't a face must therefore be **line-level**
(like headings, applied by the adapter as a separate pass), not another colour
channel — the colour space is exhausted.

**What shipped:** `==highlight==` end-to-end — `MdStrip`/`MdEmitInline`/
`MdSpansToRuns`/`MdDetectInline` in the pure core (host-tested), `SetHighlight-
Range`/`ToggleHighlight` and a `patOr` light-gray `DrawHighlightRuns` overdraw in
the adapter (leaving black glyphs intact, sidestepping the strike overdraw's
known colour issue #9), a **Style ▸ Highlight** menu item, and `==` wrapping in
Markdown mode. The read-modify-write range setters and `CompactLinkTable` were
hardened to break runs on **all three** colour channels so writing one never
clobbers another.

**Still to do (a follow-on unit of work each, smallest first):** blockquote,
fenced code block, horizontal rule, then bullet/numbered/nested/checkbox lists —
each as pure `mdcore` + host tests + adapter, validated in the emulator before
the next. Tracked in [plan 0002](../plans/0002-content-features.md).

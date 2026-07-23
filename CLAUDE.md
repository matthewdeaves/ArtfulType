# ArtfulType — working notes for Claude

A distraction-free Markdown editor for classic 68k Macs (System 6/7), built
to run from a BlueSCSI on a compact Mac. Cross-compiled with **Retro68** using
**Apple's MPW/Universal interfaces** (the real Apple headers, extracted from the
Retro68 fork's `resources/MPW_Interfaces.zip` by its `setup.sh`). Because those
headers gate the classic Toolbox spellings the code uses (`inThumb`,
`inUpButton`, …) behind `OLDROUTINENAMES`, `app/CMakeLists.txt` sets
`OLDROUTINENAMES=1`. Include the specific manager headers (`<Controls.h>`,
`<Scrap.h>`, …), not a single umbrella. (History: the project originally built
against Retro68's open-source *multiversal* interfaces and `Multiverse.h`.)

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
  `ghcr.io/matthewdeaves/retro68` container (the fork image that ships the
  MPW/Universal interfaces — the stock `autc04/retro68` ships multiversal and
  won't compile this source), and cppcheck (gated `--error-exitcode=1`).
- **Warnings are errors in spirit**: the 68k build is warning-clean today; keep it
  so. cppcheck false positives are suppressed *inline with a rationale*
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
(1..64; 0 = no link), **strikethrough** = a flag in `tsColor.green` (1 = struck;
QuickDraw has no strike face, so `DrawStruckRuns` overdraws the line after
TextEdit lays the text down), **highlight** (`==mark==`) = a flag in `tsColor.blue`
(1 = marked; no face for it either, so `DrawHighlightRuns` overpaints a light-gray
`patOr` stipple — leaving the black glyphs intact — after the text is laid down).
The three colour channels are independent, so a highlighted struck link keeps all
of red/green/blue. `GetLinkID`/`SetLinkID`, `GetStrikeFlag`/`SetStrikeFlag` and
`GetHighlightFlag`/`SetHighlightFlag` own those conventions; the read-modify-write
range setters (`SetStrikeRange`/`SetHighlightRange`) and `CompactLinkTable` break
runs on all three channels so writing one never clobbers another. IDs/flags ride
the style run, so styling follows its text through edits automatically. URLs live
in `gLinkURLs[]`, keyed by the ID; the table is rebuilt in `BuildHiddenView` and
compacted by `CompactLinkTable` when IDs are exhausted (`MAX_LINKS` = 64).

**Inline styles nest.** `MdStrip` parses nested delimiters recursively, so
`~~**x**~~`, `***x***`, `[**x**](u)` and friends round-trip through the
Writer↔Markdown mode switch. Nested spans overlap (share stripped-text
coordinates); the pure `MdSpansToRuns` flattens them into one combined-attribute
run per character range, and `ApplySpanStyles` applies a single combined
`TextStyle` per run — that one combined write is what lets bold+strike+link
coexist without a later span clobbering an earlier face.

**Line-level block features** are the exception to the style-run model: they carry
no style flag at all. The adapter detects each block by *content* at draw time (via
a pure `mdcore` predicate) and overpaints — the marker text is never modified, so
the Markdown round-trips untouched and no colour channel is spent (all three are
full). This is the sanctioned pattern now that the colour space is exhausted, and
every line-level block uses it: **horizontal rule** (`---`/`***`/`___` → `DrawHrRuns`,
pure `MdIsHorizontalRule`), **blockquote** (`> ` → `DrawBlockquoteRuns` margin bars,
`MdBlockquoteDepth`, nestable), **fenced code block** (```` ``` ````/`~~~` →
`DrawCodeBlockRuns` gray stipple across the fenced region, `MdIsCodeFence`), and
**lists** (`- `/`* `/`+ ` bullets → a drawn `•`, and `- [ ]`/`- [x]` task boxes →
`DrawListRuns`, `MdParseListItem`; numbered `1. ` stays literal). All six Writer
overpaints (these plus strike and highlight) are painted by one
`DrawWriterOverlays` pass that walks the display lines **once**, classifies each
line (fence/rule/quote/list, tracked across wrapped lines) and dispatches small
per-line helpers (`ShadeCodeLine`, `PaintHighlightLine`, `PaintBlockquoteBars`,
`PaintListMarker`, `StrikeLineRuns`, `PaintRule`) back-to-front — one walk, not
six. For blocks whose markers would otherwise be hidden, the caret's own line is
left literal so it stays editable (the `revealActive` flag). Because the
Writer buffer text always equals the canonical text for these lines, the round-trip
is lossless by construction.

The pure engine `mdcore` (`app/mdcore.{c,h}`) does strip / emit / span→run
flatten / live-detect on plain buffers; `markdown.c` is the thin Mac adapter
that locks handles, calls mdcore, and maps runs onto real `TextStyle` runs.

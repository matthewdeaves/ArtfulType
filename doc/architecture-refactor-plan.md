# Architecture refactor plan

Findings from an Ousterhout-lens ("A Philosophy of Software Design": deep vs.
shallow modules, information leakage, seams, deletion test) review of the whole
project, 2026-07-07. `mdcore` is already a genuinely deep, well-tested module;
almost all friction is concentrated in **`app/markdown.c`** (the largest file),
where the encoding "how a Markdown style is represented as a classic
`TextStyle`" has leaked out of mdcore's clean seam and been re-implemented inline
at ~10 sites.

All items are Toolbox-side (`app/*.c`), so they are **not** covered by the host
tests (which exercise the pure `mdcore` only). Verification is: host tests still
green (no mdcore regression), cppcheck clean, and — ideally — a 68k container
build. The payoff is maintainability / AI-navigability, not new behaviour; every
change must be behaviour-preserving and keep the 68k build warning-clean.

## Status

- [x] **Item 1** — centralize the kind ⟷ `TextStyle` mapping
- [x] **Item 2** — encapsulate the link-ID-in-`tsColor.red` trick
- [x] **Item 3** — unify the two "apply spans to a TE" loops
- [x] **Item 4** — de-duplicate undo/redo push + cut/copy scrap encoding
- [~] **Item 5** — single source of truth for active-view state — **considered, declined** (see below)
- [x] **Note** — Times/Monaco `GetFNum` caching

Items 1–3 landed in `app/markdown.c` this session (all behaviour-preserving).
New `static` helpers at the top of the file: `SetLinkID`/`GetLinkID`,
`HeadingSizeForLevel`/`HeadingLevelForSize`, `StyleForKind`, `ApplySpanStyles`.
Callers routed through them: `BuildHiddenView`, `InsertMarkdownAsStyled`,
`DetectInlineMarkdown`, `SyncHiddenToCanonical`, `ToggleHeadingHidden`,
`DoLinkHidden`, `CompactLinkTable`, `BuildStyleRuns`.

**Verified locally:** host tests green (192 checks — mdcore untouched, no
regression). **Not verifiable locally** (no Retro68 toolchain or cppcheck on this
machine): the real 68k container build and the cppcheck gate — both run in CI.
Manual check done: no orphaned locals, no new `case MD_KIND_*` ladders, the
heading formula now has exactly one home (`HeadingSizeForLevel`).

Item 4 and the note landed in a second pass (2026-07-07). New `static` helpers:
`CaptureSnapshot`/`EncodeSelectionForScrap`/`PutHandleToScrap` in `app/undo.c`,
`TimesFont`/`MonacoFont` in `app/markdown.c` (10 `GetFNum` call sites routed
through them; `main.c`'s one-time startup call left as-is — cross-TU, no benefit).
Item 5 was analysed and **declined** with a recorded rationale (below).

**On test coverage for this work:** every seam created here is Toolbox-bound —
Memory Manager (`NewHandle`/`BlockMove`), Scrap Manager (`ZeroScrap`/`PutScrap`),
Font Manager (`GetFNum`), TextEdit (`TEDelete`, `gActiveTE`). The host harness
compiles **only** `mdcore.c` (`-DAT_HOST_TEST`, no Toolbox mocks — `host_mac_types.h`
is types, not callable fakes), so none of these are host-reachable, and the pure
logic they call into (`MdStrip`/`MdEmitInline`/`MdDetectInline`) is already covered
by the 192 existing checks. The one pure invariant from Items 1–3 (heading
size ⟷ level) is guarded structurally rather than by a test: `HeadingLevelForSize`
is *implemented by calling* `HeadingSizeForLevel`, so the two cannot drift out of
being exact inverses — a stronger guarantee than a round-trip assertion. No new
host test was warranted; adding one would require either faking the Toolbox or
moving point-size (a rendering concern) into the pure text engine.

---

## Item 1 — kind ⟷ `TextStyle` mapping (Strong) ✔ implementing now

**Files:** `app/markdown.c`

**Problem (information leakage).** The header promises the mapping "lives only in
that adapter," but the forward (kind → style) switch is copied three times
(`BuildHiddenView`, `InsertMarkdownAsStyled`, `DetectInlineMarkdown`) and the
heading-size formula `CurrentFontSize() + (4 - level) * 4` appears four times
(BuildHiddenView, `ToggleHeadingHidden` ×2, DetectInlineMarkdown) plus its
inverse detection once (`SyncHiddenToCanonical`). Round-trip correctness depends
on the forward formula and its inverse staying exact inverses, but nothing links
them.

**Solution.** Three `static` helpers at the top of `markdown.c`:
- `short HeadingSizeForLevel(short level)` — the one formula.
- `short HeadingLevelForSize(short size)` — its guaranteed inverse (0 = not a
  heading size).
- `void StyleForKind(short kind, short level, short linkID, TextStyle *ts,
  short *mode)` — fills `ts` and the `doFace/doFont/...` mode mask for a kind.

Route all forward switches and formula sites through them. `DetectInlineMarkdown`
keeps its `EnsureLinkRoom()` + `AddLinkURL()` side effects around the call.

**Benefit (leverage).** One edit site for the visual encoding; the forward/inverse
invariant becomes assertable (a host test could check
`HeadingLevelForSize(HeadingSizeForLevel(n)) == n`).

## Item 2 — link-ID-in-`tsColor.red` encapsulation (Strong, cheap) ✔ implementing now

**Files:** `app/markdown.c`

**Problem.** The "stash a 1-based link ID in the unused `tsColor.red`, keep
green/blue 0" convention is open-coded at ~10 read/write sites. Easy to write
`.red` and forget to zero `.green/.blue`.

**Solution.** `static void SetLinkID(TextStyle *, short)` and
`static short GetLinkID(const TextStyle *)`. The green/blue-stay-0 invariant then
lives in exactly one place. (The plain black-reset sites — base/normal text —
stay explicit; they mean "black", not "no link".)

## Item 3 — unify the two apply-spans loops (Worth exploring) ✔ implementing now

**Files:** `app/markdown.c` — `BuildHiddenView` and `InsertMarkdownAsStyled`

**Problem.** Both normalize a range to the base (plain Times) style, then loop
`MdSpan`s applying the kind switch, differing only by a coordinate `offset` and a
link `remap[]`.

**Solution.** `static void ApplySpanStyles(TEHandle te, short from, short to,
short offset, const MdSpan *spans, short count, const short *remap)` — normalizes
`te[from,to)` to base, then paints each span via `StyleForKind` (Item 1), shifting
by `offset` and remapping link IDs when `remap` is non-NULL.
- `BuildHiddenView`: `ApplySpanStyles(gHiddenTE, 0, 32767, 0, spans, n, NULL)`.
- `InsertMarkdownAsStyled`: `ApplySpanStyles(te, insertStart, insertStart+outLen,
  insertStart, spans, n, remap)`.

Depends on Items 1 & 2. Slightly widens an internal interface — acceptable, but
was flagged for a design-it-twice look; the offset+remap parameterization is the
minimal shape that covers both callers.

---

## Item 4 — undo/redo + cut/copy duplication (Worth exploring) ✔ done

**Files:** `app/undo.c`

`PushUndoSnapshot` and `PushRedoSnapshot` were identical except for which
stack/count they touched (verbatim ring-buffer eviction included); `DoCut`/`DoCopy`
shared ~15 lines of scrap encoding. Extracted:
- `Boolean CaptureSnapshot(UndoSnapshot stack[], short *count)` — the sync +
  allocate + evict + fill, returning `false` (touching nothing) on `NewHandle`
  failure. The two push routines are now thin wrappers; the documented asymmetry
  stays at the call sites (`PushUndoSnapshot` clears redo + `UpdateEditMenuState`,
  and correctly skips both when the capture fails; `PushRedoSnapshot` does neither).
- `Handle EncodeSelectionForScrap(short, short)` and `void PutHandleToScrap(Handle)`
  — the shared cut/copy body. `DoCut` keeps its snapshot-then-delete tail; `DoCopy`
  is just encode + put. Behaviour-preserving; ordering (encode → snapshot → put →
  delete in cut) unchanged.

## Item 5 — active-view single source of truth (Speculative) ✘ declined

**Files:** `app/main.c` (init), `app/file.c` (`SetViewMode`)

**Considered and declined (2026-07-07).** The invariant
`gActiveTE == (gHideMarkdown ? gHiddenTE : gTE)` is real, but it is written in only
**two** places (`MakeWindow` init at `main.c:136`; `SetViewMode` at `file.c:40/43`),
both already correct and co-located with the mode switch. A derived `ActiveTE()`
accessor would make the invalid state unrepresentable, but it reads `gHideMarkdown`,
and `SetViewMode` deliberately `TEDeactivate`s the **old** active TE, `TEActivate`s
the **new** one, and assigns `gHideMarkdown` *last* (`file.c:36–47`). An accessor
would force moving that assignment between the deactivate/activate pair — injecting
ordering fragility into the most delicate state transition in the app for a very
small "unrepresentable state" gain across two already-correct sites. Not worth it.
(The `TEIdle(gActiveTE)`-per-loop hot-path cost of an accessor is negligible by
comparison and was not the deciding factor.)

## Note — Times/Monaco `GetFNum` caching ✔ done

**Files:** `app/markdown.c`

Added `static short TimesFont(void)` / `MonacoFont(void)` — resolve each name once
into a `-1`-sentinel function-static cache (font numbers are fixed for the app's
life; both fonts have positive IDs). All 10 `GetFNum` sites in `markdown.c` now go
through them. `main.c:124`'s single startup call is left alone: it's one-time and
in another translation unit, so caching it buys nothing and would only widen the
accessors' linkage.

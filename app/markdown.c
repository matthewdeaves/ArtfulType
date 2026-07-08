#include "app.h"

/*
    The visual encoding of a markdown style -- how a "kind" (bold, code,
    heading, link, ...) becomes a concrete classic TextStyle -- lives here and
    nowhere else. Everything below (BuildHiddenView, InsertMarkdownAsStyled, the
    live DetectInlineMarkdown converter, the round-trip in SyncHiddenToCanonical)
    goes through these helpers, so the encoding has exactly one home to edit.
*/

/* Times and Monaco are the only two fonts the Writer view uses, and a font's
   number is fixed for the life of the app -- so resolve each name once and
   cache it. GetFNum otherwise walks the font list on every call, and these run
   on every style pass. (-1 == not yet resolved; both fonts have positive IDs.) */
static short TimesFont(void)
{
    static short cached = -1;
    if (cached < 0)
        GetFNum("\pTimes", &cached);
    return cached;
}

static short MonacoFont(void)
{
    static short cached = -1;
    if (cached < 0)
        GetFNum("\pMonaco", &cached);
    return cached;
}

/* A link's URL rides in its run's tsColor.red as a 1-based ID (0 == no link);
   see the gLinkURLs note in app.h. green/blue must stay 0 (the ID is meant to
   be visually black). These two helpers are the only places that read or write
   that convention, so that invariant lives in one spot. */
static void SetLinkID(TextStyle *ts, short id)
{
    ts->tsColor.red = id;
    ts->tsColor.green = 0;
    ts->tsColor.blue = 0;
}

static short GetLinkID(const TextStyle *ts)
{
    return ts->tsColor.red;
}

/* A level-N heading (N == 1..3) renders as bold at this size -- larger N is
   smaller. HeadingLevelForSize is the exact inverse (0 == not a heading size),
   so the Writer<->Markdown round-trip stays lossless. Keep the two paired. */
static short HeadingSizeForLevel(short level)
{
    return CurrentFontSize() + (4 - level) * 4;
}

static short HeadingLevelForSize(short size)
{
    short level;

    for (level = 1; level <= 3; level++)
        if (size == HeadingSizeForLevel(level))
            return level;
    return 0;
}

/* Fills `ts` (and `mode`, the doFace/doFont/... mask naming which fields apply)
   with the style for a markdown span kind. linkID is used only for a LINK. This
   is the single forward encoding; the several TESetStyle sites share it. */
static void StyleForKind(short kind, short level, short linkID,
                         TextStyle *ts, short *mode)
{
    switch (kind) {
    case MD_KIND_BOLD:
        ts->tsFace = bold;
        *mode = doFace;
        break;
    case MD_KIND_ITALIC:
        ts->tsFace = italic;
        *mode = doFace;
        break;
    case MD_KIND_CODE:
        ts->tsFont = MonacoFont();
        *mode = doFont;
        break;
    case MD_KIND_LINK:
        ts->tsFace = underline;
        SetLinkID(ts, linkID);
        *mode = doFace + doColor;
        break;
    case MD_KIND_HEADING:
        ts->tsFace = bold;
        ts->tsSize = HeadingSizeForLevel(level);
        *mode = doFace + doSize;
        break;
    default:
        *mode = 0;
        break;
    }
}

/*
    Normalizes te[from,to) to the base (plain Times) style, then paints each
    span's style onto te, shifting span coordinates by `offset` and remapping
    link IDs through `remap` (NULL == use the span's own linkID). Shared by
    BuildHiddenView (whole document, no offset, no remap) and
    InsertMarkdownAsStyled (the pasted range, offset and remapped).
*/
static void ApplySpanStyles(TEHandle te, short from, short to, short offset,
                            const MdSpan *spans, short count, const short *remap)
{
    TextStyle base;
    short fontNum;
    short k;

    fontNum = TimesFont();
    base.tsFont = fontNum;
    base.tsFace = normal;
    base.tsSize = CurrentFontSize();
    base.tsColor.red = base.tsColor.green = base.tsColor.blue = 0;
    TESetSelect(from, to, te);
    TESetStyle(doFont + doFace + doSize + doColor, &base, true, te);

    for (k = 0; k < count; k++) {
        TextStyle st;
        short mode;
        short linkID = remap ? remap[spans[k].linkID] : spans[k].linkID;

        StyleForKind(spans[k].kind, spans[k].level, linkID, &st, &mode);
        if (mode != 0) {
            TESetSelect((short) (offset + spans[k].start),
                        (short) (offset + spans[k].end), te);
            TESetStyle(mode, &st, true, te);
        }
    }
}

short AddLinkURL(const unsigned char *url)
{
    if (gLinkCount >= MAX_LINKS)
        return 0;
    gLinkCount++;
    BlockMove((Ptr) url, (Ptr) gLinkURLs[gLinkCount], url[0] + 1);
    return gLinkCount;
}

/*
    Reclaims link IDs orphaned when their underlined text was deleted or
    re-styled. gLinkURLs / gLinkCount are otherwise reset only by a full
    BuildHiddenView (a reparse of gTE), so a long Writer-mode session of
    adding and removing links would march gLinkCount monotonically to
    MAX_LINKS and then start silently dropping new links.

    This rebuilds the table from the IDs still referenced by a run in
    gHiddenTE -- renumbering them 1..N and rewriting each surviving link
    run's ID -- so only live links occupy the table. The ID lives in a
    run's tsColor.red (a value 1..64, visually indistinguishable from
    black), so recolouring never changes the display.

    Call only when gHiddenTE is coherent (never mid-insert). Preserves the
    caret/selection. Not reentrant: uses file-scope scratch, like the other
    adapters here.
*/
static Str255 gCompactURLs[MAX_LINKS + 1];

static short CompactLinkTable(void)
{
    short remap[MAX_LINKS + 1];   /* old ID -> new ID; 0 == unreferenced */
    short newCount = 0;
    long len = (**gHiddenTE).teLength;
    short savedStart = (**gHiddenTE).selStart;
    short savedEnd = (**gHiddenTE).selEnd;
    long i;
    short k;

    /* Both passes call TEGetStyle per character over the whole document,
       which on real 68000 hardware is slow enough on a long document to be
       worth a watch cursor -- same reasoning as BuildHiddenView. This only
       runs at the genuine 64-link cap, so the cost is rare. */
    SetCursor(*GetCursor(watchCursor));

    for (k = 0; k <= gLinkCount; k++)
        remap[k] = 0;

    /* Pass 1: give each still-referenced ID a fresh 1..N number, in order
       of first appearance, copying its URL aside. */
    for (i = 0; i < len; i++) {
        TextStyle st;
        short lh, fa, id;

        TEGetStyle((short) i, &st, &lh, &fa, gHiddenTE);
        id = GetLinkID(&st);
        if ((st.tsFace & underline) && id >= 1 && id <= gLinkCount &&
            remap[id] == 0) {
            newCount++;
            remap[id] = newCount;
            BlockMove(gLinkURLs[id], gCompactURLs[newCount],
                      gLinkURLs[id][0] + 1);
        }
    }

    if (newCount == gLinkCount) {
        InitCursor();
        return gLinkCount;   /* nothing orphaned -- no rewrite needed */
    }

    /* Pass 2: walk maximal (underline, id) runs and rewrite the ID of any
       link run whose number actually changed. redraw = false: the colour
       is an imperceptible link ID, so nothing on screen moves. */
    i = 0;
    while (i < len) {
        TextStyle st;
        short lh, fa, id;
        long runEnd;
        int underlined;

        TEGetStyle((short) i, &st, &lh, &fa, gHiddenTE);
        id = GetLinkID(&st);
        underlined = (st.tsFace & underline) != 0;

        runEnd = i + 1;
        while (runEnd < len) {
            TextStyle st2;
            short lh2, fa2;

            TEGetStyle((short) runEnd, &st2, &lh2, &fa2, gHiddenTE);
            if (((st2.tsFace & underline) != 0) != underlined ||
                GetLinkID(&st2) != id)
                break;
            runEnd++;
        }

        if (underlined && id >= 1 && id <= gLinkCount && remap[id] != id) {
            TextStyle ns;

            SetLinkID(&ns, remap[id]);
            TESetSelect((short) i, (short) runEnd, gHiddenTE);
            TESetStyle(doColor, &ns, false, gHiddenTE);
        }
        i = runEnd;
    }

    /* Publish the compacted table and restore the user's selection. */
    for (k = 1; k <= newCount; k++)
        BlockMove(gCompactURLs[k], gLinkURLs[k], gCompactURLs[k][0] + 1);
    gLinkCount = newCount;

    TESetSelect(savedStart, savedEnd, gHiddenTE);
    InitCursor();
    return newCount;
}

/* Make room for at least one more link before adding it, reclaiming
   orphaned IDs from gHiddenTE only when the table is actually full so the
   common case pays nothing. Call only when gHiddenTE is coherent. */
static void EnsureLinkRoom(void)
{
    if (gLinkCount >= MAX_LINKS)
        CompactLinkTable();
}

/*
    Markdown mode shows raw syntax with no visual styling at all -- just
    plain uniform text at the current zoom size. Selection is preserved
    since this gets called after Style-menu edits that already placed
    the caret somewhere meaningful.
*/
void ClearStyles(void)
{
    TextStyle ts;
    short fontNum;
    short savedStart = (**gTE).selStart;
    short savedEnd = (**gTE).selEnd;

    fontNum = TimesFont();
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;

    TESetSelect(0, 32767, gTE);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gTE);

    TESetSelect(savedStart, savedEnd, gTE);
}

/* Scratch buffers for the pure strip pass (mdcore's MdStrip), shared by the
   non-reentrant BuildHiddenView / InsertMarkdownAsStyled /
   ClearMarkdownInSelection adapters -- kept at file scope so they stay off
   the small classic-Mac stack. */
static MdSpan gStripSpans[MD_MAX_SPANS];
static MdLinkTable gStripLinks;

/* Scratch for the emit direction (mdcore's MdEmitInline): the coalesced
   style runs of the range being encoded, and a snapshot of the global link
   table in mdcore's own layout. Shared by the non-reentrant
   SyncHiddenToCanonical / EncodeSelectionAsMarkdown adapters. */
static MdRun gEmitRuns[MD_MAX_SPANS];
static MdLinkTable gEmitLinks;

/*
    Builds gHiddenTE from gTE's canonical markdown text, stripping the
    delimiter characters themselves (**, *, `, [](), leading #s) and
    recording where the surviving text landed so styling can be applied
    afterward, in the stripped buffer's own coordinates.
*/
/*
    gTE and gHiddenTE are both bound to gWindow (a TE record draws into
    whatever GrafPort was current at TEStyleNew time, for its whole
    lifetime, regardless of which one is "active" later) -- so editing
    the *inactive* record still paints onto the window. Moving its
    viewRect off-screen for the duration of a rebuild makes those calls
    draw nothing, since drawing is clipped to viewRect every time.
*/
#define OFFSCREEN_COORD (-32000)

void SuppressDrawing(TEHandle te, Rect *saved)
{
    *saved = (**te).viewRect;
    SetRect(&(**te).viewRect, OFFSCREEN_COORD, OFFSCREEN_COORD,
            OFFSCREEN_COORD + 100, OFFSCREEN_COORD + 100);
}

void RestoreDrawing(TEHandle te, Rect *saved)
{
    (**te).viewRect = *saved;
}

void BuildHiddenView(void)
{
    Handle srcH;
    long len;
    Handle outH;
    long outLen;
    short spanCount;
    short k;
    Rect savedViewRect;
    MdStripOpts opts;

    /* Parsing the whole document and applying one TESetStyle call per
       styled span is, on real 68000 hardware, slow enough on a long,
       heavily-styled document to look like the app has hung. A watch
       cursor doesn't make it faster, but it stops it from looking
       broken -- the actual fix for the underlying slowness (lazy/
       incremental styling) is a much bigger, riskier change. */
    SetCursor(*GetCursor(watchCursor));

    srcH = (**gTE).hText;
    len = (**gTE).teLength;
    outH = NewHandle(len + 1);
    if (outH == NULL) {
        InitCursor();
        return;
    }

    HLock(srcH);
    HLock(outH);
    opts.headingMode = MD_HEADINGS_SPAN;
    opts.startsAtLineStart = 1;
    outLen = MdStrip(*srcH, len, &opts, *outH, len + 1,
                     gStripSpans, MD_MAX_SPANS, &spanCount, &gStripLinks);
    HUnlock(srcH);

    /* Publish the freshly-parsed link table to the Mac-side globals. The
       table is rebuilt from scratch on every BuildHiddenView, so link IDs
       never accumulate across rebuilds. */
    gLinkCount = gStripLinks.count;
    for (k = 1; k <= gStripLinks.count; k++)
        BlockMove(gStripLinks.url[k], gLinkURLs[k], gStripLinks.url[k][0] + 1);

    SuppressDrawing(gHiddenTE, &savedViewRect);

    TESetSelect(0, 32767, gHiddenTE);
    TEDelete(gHiddenTE);
    /* outH must stay locked across TEInsert: TEInsert allocates from the
       heap and can relocate blocks, and *outH is the source it copies
       from -- unlocking first would leave that pointer dangling
       (Inside Macintosh I, "lock the block before dereferencing"). */
    TEInsert(*outH, outLen, gHiddenTE);
    HUnlock(outH);
    DisposeHandle(outH);

    /* Base plain style plus each parsed span, mapped through the one shared
       kind->TextStyle encoder. Whole document, no coordinate offset, no link
       remap (the spans already carry the freshly-published global IDs). */
    ApplySpanStyles(gHiddenTE, 0, 32767, 0, gStripSpans, spanCount, NULL);

    TESetSelect(0, 0, gHiddenTE);

    RestoreDrawing(gHiddenTE, &savedViewRect);

    InitCursor();
}

/*
    Reverse direction: walks gHiddenTE's text + style runs and re-derives
    markdown delimiters, rebuilding gTE's canonical text from scratch.
    Headings are detected per-line (bold + a heading-sized run at the
    line's start); everything else is inline bold/italic/Monaco-as-code.
    Link underlines round-trip as "[text](url)" -- the url comes from
    gLinkURLs, keyed by the run's tsColor.red (see AddLinkURL above).
*/
/* Coalesces the styled characters of te[from..to) into maximal same-style
   runs (in coordinates relative to `from`), the input MdEmitInline needs.
   Breaks runs on the bold/italic/code/link booleans only -- a run keeps the
   linkID of its first character, matching the old emit loop, which captured
   a link's URL when it opened and ignored any mid-underline id change.
   Returns the run count. */
static short BuildStyleRuns(TEHandle te, long from, long to, short monacoFont,
                            MdRun *runs, short cap)
{
    short n = 0;
    long i;

    for (i = from; i < to; i++) {
        TextStyle st;
        short lh, fa;
        int b, it, cd, lk;
        short id;

        TEGetStyle((short) i, &st, &lh, &fa, te);
        b = (st.tsFace & bold) != 0;
        it = (st.tsFace & italic) != 0;
        cd = (st.tsFont == monacoFont);
        lk = (st.tsFace & underline) != 0;
        id = GetLinkID(&st);

        if (n > 0 && runs[n - 1].bold == b && runs[n - 1].italic == it &&
            runs[n - 1].code == cd && runs[n - 1].link == lk) {
            runs[n - 1].end = (i - from) + 1;
        } else if (n < cap) {
            runs[n].start = i - from;
            runs[n].end = (i - from) + 1;
            runs[n].bold = b;
            runs[n].italic = it;
            runs[n].code = cd;
            runs[n].link = lk;
            runs[n].linkID = id;
            n++;
        } else {
            /* Run table full (a line/range with >512 style changes -- not
               reachable in practice): fold the rest into the last run so no
               text is dropped. */
            runs[n - 1].end = (i - from) + 1;
        }
    }
    return n;
}

/* Copies the live global link table into mdcore's MdLinkTable layout so the
   pure emitter can look up URLs without touching Mac globals. */
static void SnapshotLinkTable(void)
{
    short li;

    gEmitLinks.count = gLinkCount;
    for (li = 1; li <= gLinkCount; li++)
        BlockMove(gLinkURLs[li], gEmitLinks.url[li], gLinkURLs[li][0] + 1);
}

void SyncHiddenToCanonical(void)
{
    Handle srcH;
    long len;
    Handle outH;
    long outCap;
    long outLen;
    long lineStart;
    short monacoFont;
    Rect savedViewRect;
    long urlSpace;
    short li;

    /* Same reasoning as the watch cursor in BuildHiddenView -- this is
       the reverse direction, called on save, mode switch, and (more
       frequently) at the start of every typing run via PushUndoSnapshot/
       PushRedoSnapshot, so a long, heavily-styled document can make it
       pause noticeably mid-typing too. */
    SetCursor(*GetCursor(watchCursor));

    srcH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;
    urlSpace = 0;
    for (li = 1; li <= gLinkCount; li++)
        urlSpace += gLinkURLs[li][0];
    outCap = len * 2 + 64 + urlSpace;
    outH = NewHandle(outCap);
    if (outH == NULL) {
        InitCursor();
        return;
    }
    outLen = 0;

    monacoFont = MonacoFont();
    SnapshotLinkTable();

    HLock(srcH);
    HLock(outH);

    lineStart = 0;
    while (lineStart <= len) {
        long lineEnd = lineStart;
        short headingLevel = 0;
        Boolean isHeading = false;

        while (lineEnd < len && (*srcH)[lineEnd] != '\r')
            lineEnd++;

        if (lineEnd > lineStart) {
            TextStyle firstStyle;
            short dummyLH, dummyFA;

            TEGetStyle((short) lineStart, &firstStyle, &dummyLH, &dummyFA, gHiddenTE);
            if (firstStyle.tsFace & bold) {
                headingLevel = HeadingLevelForSize(firstStyle.tsSize);
                isHeading = (headingLevel > 0);
            }
        }

        if (isHeading) {
            short k;

            for (k = 0; k < headingLevel; k++)
                (*outH)[outLen++] = '#';
            (*outH)[outLen++] = ' ';
            BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
            outLen += (lineEnd - lineStart);
        } else {
            short runCount = BuildStyleRuns(gHiddenTE, lineStart, lineEnd,
                                            monacoFont, gEmitRuns, MD_MAX_SPANS);
            outLen += MdEmitInline(*srcH + lineStart, lineEnd - lineStart,
                                   gEmitRuns, runCount, &gEmitLinks,
                                   *outH + outLen, outCap - outLen);
        }

        if (lineEnd < len)
            (*outH)[outLen++] = '\r';
        lineStart = lineEnd + 1;
    }

    HUnlock(srcH);

    SuppressDrawing(gTE, &savedViewRect);

    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    /* Keep outH locked across TEInsert -- see BuildHiddenView. */
    TEInsert(*outH, outLen, gTE);
    HUnlock(outH);
    DisposeHandle(outH);

    ClearStyles();

    RestoreDrawing(gTE, &savedViewRect);

    InitCursor();
}

/*
    Cut/Copy/Paste go through the Scrap Manager directly (ZeroScrap/
    PutScrap/GetScrap) rather than the usual TECut/TECopy/TEPaste +
    TEToScrap/TEFromScrap pattern -- TEToScrap/TEFromScrap are declared
    in this toolchain's headers but have no actual implementation
    linked anywhere (confirmed: linker error, not a typo), so they're
    unusable here.

    Styling still survives a copy within Writer mode, just not via the
    clipboard's own (unavailable) style support: copying a Writer-mode
    selection encodes its styled runs as markdown text (the same
    inline bold/italic/code/link delimiters SyncHiddenToCanonical
    already produces for the whole document, just scoped to a range
    instead of per-line -- so headings specifically aren't
    re-derived, since they're a line-level construct that doesn't
    make sense for an arbitrary sub-range), and pasting back into
    Writer mode parses that text for the same delimiters and applies
    the corresponding styles (mirroring BuildHiddenView's inline
    parsing, again without heading handling). Plain text round-trips
    unchanged either way, including to/from other apps -- a paste
    that happens to contain a literal "*" or "`" from some other
    source will get (mis)interpreted as markdown, an accepted
    trade-off for getting styled copy/paste working at all. Markdown
    mode's copy/paste is untouched -- the selection is already raw
    markdown text, no encoding/decoding needed.
*/
Handle EncodeSelectionAsMarkdown(short start, short end, TEHandle te)
{
    Handle srcH;
    Handle outH;
    long outCap;
    long outLen;
    long urlSpace;
    short li;
    short monacoFont;
    short runCount;

    srcH = (**te).hText;
    urlSpace = 0;
    for (li = 1; li <= gLinkCount; li++)
        urlSpace += gLinkURLs[li][0];
    outCap = (long) (end - start) * 2 + 64 + urlSpace;
    outH = NewHandle(outCap);
    if (outH == NULL)
        return NULL;

    monacoFont = MonacoFont();
    SnapshotLinkTable();
    runCount = BuildStyleRuns(te, start, end, monacoFont, gEmitRuns, MD_MAX_SPANS);

    HLock(srcH);
    HLock(outH);
    outLen = MdEmitInline(*srcH + start, (long) (end - start),
                          gEmitRuns, runCount, &gEmitLinks, *outH, outCap);
    HUnlock(srcH);
    HUnlock(outH);
    SetHandleSize(outH, outLen);

    return outH;
}

void InsertMarkdownAsStyled(Handle srcH, long srcLen, TEHandle te)
{
    Handle outH;
    long outLen;
    short spanCount;
    short remap[MD_MAX_LINKS + 1];
    short insertStart;
    short k;
    MdStripOpts opts;
    Boolean droppedLink = false;

    /* Only ever called in Writer mode, where te == gHiddenTE. Reclaim any
       orphaned link IDs before appending the pasted ones, so pasting into
       a link-heavy document doesn't needlessly exhaust the table. */
    EnsureLinkRoom();

    outH = NewHandle(srcLen + 1);
    if (outH == NULL)
        return;

    HLock(srcH);
    HLock(outH);
    /* Inline only -- a pasted "# " stays literal, matching the old code. */
    opts.headingMode = MD_HEADINGS_OFF;
    opts.startsAtLineStart = 0;
    outLen = MdStrip(*srcH, srcLen, &opts, *outH, srcLen + 1,
                     gStripSpans, MD_MAX_SPANS, &spanCount, &gStripLinks);
    HUnlock(srcH);

    /* Append the pasted links to the running global table (Writer-mode
       links accumulate as usual), remembering the real global ID each
       local link maps to. remap[0] stays 0 so an overflowed link renders
       unstyled, exactly as before. */
    remap[0] = 0;
    for (k = 1; k <= gStripLinks.count; k++) {
        remap[k] = AddLinkURL(gStripLinks.url[k]);
        if (remap[k] == 0)
            droppedLink = true;
    }

    insertStart = (**te).selStart;
    /* Keep outH locked across TEInsert -- see BuildHiddenView. */
    TEInsert(*outH, outLen, te);
    HUnlock(outH);
    DisposeHandle(outH);

    /* TEInsert's new text inherits whatever style was at the insertion point
       -- ApplySpanStyles normalizes the whole pasted range to plain first,
       then paints the parsed spans, shifting them into place by insertStart
       and remapping each local link ID to its global one. No heading spans
       occur here (paste is inline-only, headingMode OFF). */
    ApplySpanStyles(te, insertStart, (short) (insertStart + outLen), insertStart,
                    gStripSpans, spanCount, remap);

    TESetSelect((short) (insertStart + outLen), (short) (insertStart + outLen), te);

    /* The text pasted fine; only some of its links couldn't be tracked. Say
       so rather than leave a few underlines silently pointing nowhere. */
    if (droppedLink)
        ShowError("\pSome pasted links weren't kept: this document is at ArtfulType's 64-link limit.");
}

void WrapSelection(char *prefix, char *suffix)
{
    short selStart, selEnd;
    long selLen, totalLen, textLen;
    short prefixLen, suffixLen;
    Handle textH;
    Handle newH;
    Boolean outerWrapped, innerWrapped;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    selLen = selEnd - selStart;
    textH = (**gTE).hText;
    textLen = (**gTE).teLength;

    gDirty = true;

    prefixLen = strlen(prefix);
    suffixLen = strlen(suffix);

    HLock(textH);
    outerWrapped =
        (selStart >= prefixLen) &&
        (selEnd + suffixLen <= textLen) &&
        (memcmp(*textH + selStart - prefixLen, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd, suffix, suffixLen) == 0);
    innerWrapped = !outerWrapped &&
        (selLen >= prefixLen + suffixLen) &&
        (memcmp(*textH + selStart, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd - suffixLen, suffix, suffixLen) == 0);
    HUnlock(textH);

    if (outerWrapped) {
        /* markers sit just outside the selection -- strip them (toggle off) */
        newH = NewHandle(selLen);
        if (newH == NULL)
            return;
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart, *newH, selLen);
        HUnlock(textH);

        TESetSelect(selStart - prefixLen, selEnd + suffixLen, gTE);
        TEDelete(gTE);
        TEInsert(*newH, selLen, gTE);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart - prefixLen, selStart - prefixLen + selLen, gTE);
        return;
    }

    if (innerWrapped) {
        /* markers are part of the selection itself -- strip them (toggle off) */
        long innerLen = selLen - prefixLen - suffixLen;

        newH = NewHandle(innerLen);
        if (newH == NULL)
            return;
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart + prefixLen, *newH, innerLen);
        HUnlock(textH);

        TEDelete(gTE);
        TEInsert(*newH, innerLen, gTE);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart, selStart + innerLen, gTE);
        return;
    }

    totalLen = prefixLen + selLen + suffixLen;
    newH = NewHandle(totalLen);
    if (newH == NULL)
        return;
    HLock(newH);
    HLock(textH);
    BlockMove(prefix, *newH, prefixLen);
    BlockMove(*textH + selStart, *newH + prefixLen, selLen);
    BlockMove(suffix, *newH + prefixLen + selLen, suffixLen);
    HUnlock(textH);

    TEDelete(gTE);
    TEInsert(*newH, totalLen, gTE);
    HUnlock(newH);
    DisposeHandle(newH);

    TESetSelect(selStart + prefixLen, selStart + prefixLen + selLen, gTE);
}

void ApplyHeading(short level)
{
    short selStart;
    short lineStart;
    long textLen;
    Handle textH;
    char prefix[8];
    short i;
    Boolean alreadyHeading;

    gDirty = true;

    selStart = (**gTE).selStart;
    textH = (**gTE).hText;
    textLen = (**gTE).teLength;

    lineStart = selStart;
    HLock(textH);
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    HUnlock(textH);

    for (i = 0; i < level; i++)
        prefix[i] = '#';
    prefix[level] = ' ';

    HLock(textH);
    alreadyHeading =
        (lineStart + level + 1 <= textLen) &&
        (memcmp(*textH + lineStart, prefix, level + 1) == 0);
    HUnlock(textH);

    if (alreadyHeading) {
        TESetSelect(lineStart, lineStart + level + 1, gTE);
        TEDelete(gTE);
        return;
    }

    TESetSelect(lineStart, lineStart, gTE);
    TEInsert(prefix, level + 1, gTE);
}

void DoLink(void)
{
    short selStart, selEnd;
    long selLen, totalLen;
    Handle textH;
    Handle newH;
    static char mid[] = "]()";
    short midLen = 3;
    short cursorPos;

    gDirty = true;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    selLen = selEnd - selStart;
    textH = (**gTE).hText;

    totalLen = 1 + selLen + midLen;
    newH = NewHandle(totalLen);
    if (newH == NULL)
        return;
    HLock(newH);
    HLock(textH);
    (*newH)[0] = '[';
    BlockMove(*textH + selStart, *newH + 1, selLen);
    BlockMove(mid, *newH + 1 + selLen, midLen);
    HUnlock(textH);

    TEDelete(gTE);
    TEInsert(*newH, totalLen, gTE);
    HUnlock(newH);
    DisposeHandle(newH);

    cursorPos = selStart + selLen + 3;
    TESetSelect(cursorPos, cursorPos, gTE);
}

/*
    Style commands while in Hide Markdown mode apply real TextStyle
    directly to gHiddenTE instead of inserting delimiter text -- there's
    no visible syntax to insert. Toggle state is read back from the
    style at the selection start.
*/
static Boolean SelectionHasFace(Style face)
{
    TextStyle ts;
    short lh, fa;

    TEGetStyle((**gHiddenTE).selStart, &ts, &lh, &fa, gHiddenTE);
    return (ts.tsFace & face) != 0;
}

void ToggleFace(Style face)
{
    TextStyle ts;

    ts.tsFace = SelectionHasFace(face) ? normal : face;
    TESetStyle(doFace, &ts, true, gHiddenTE);
}

/* Briefly hilites a push button so a keyboard-triggered OK/Cancel gives
   the same visual click feedback the mouse would. */
static void FlashDialogButton(DialogPtr dlg, short item)
{
    DialogItemType type;
    Handle itemH;
    Rect box;
    long ticks;

    GetDialogItem(dlg, item, &type, &itemH, &box);
    HiliteControl((ControlHandle) itemH, inButton);
    Delay(8, &ticks);
    HiliteControl((ControlHandle) itemH, 0);
}

/* Modal filter for the link dialog: Return/Enter confirms, Escape or
   Cmd-. cancels -- the standard keyboard shortcuts a text-entry dialog is
   expected to honour. All other events fall through to ModalDialog (which,
   with the default item set, also maintains the OK button's outline). */
static pascal Boolean LinkDialogFilter(DialogPtr dlg, EventRecord *evt, short *item)
{
    unsigned char ch;

    if (evt->what != keyDown && evt->what != autoKey)
        return false;

    ch = evt->message & charCodeMask;
    if (ch == kReturnKey || ch == kEnterKey) {
        FlashDialogButton(dlg, iLinkOK);
        *item = iLinkOK;
        return true;
    }
    if (ch == kEscapeKey || ((evt->modifiers & cmdKey) && ch == '.')) {
        FlashDialogButton(dlg, iLinkCancel);
        *item = iLinkCancel;
        return true;
    }
    return false;
}

/* Prompts for a URL; returns true and fills in `url` if OK was clicked. */
static Boolean ShowLinkURLDialog(unsigned char *url)
{
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;
    Boolean result;
    ModalFilterUPP filter;

    dlg = GetNewDialog(kLinkDialog, NULL, (WindowPtr) -1L);
    if (dlg == NULL)
        return false;

    /* SetDialogDefaultItem/SetDialogCancelItem are System 7 traps and are
       unimplemented on System 6. LinkDialogFilter handles Return/Escape
       regardless, so on System 6 we only forgo the default button's heavy
       outline, not the keyboard behavior. */
    if (HasSystem7()) {
        SetDialogDefaultItem(dlg, iLinkOK);
        SetDialogCancelItem(dlg, iLinkCancel);
    }
    SelectDialogItemText(dlg, iLinkField, 0, 32767);

    filter = NewModalFilterUPP(LinkDialogFilter);
    do {
        ModalDialog(filter, &item);
    } while (item != iLinkOK && item != iLinkCancel);
    DisposeModalFilterUPP(filter);

    result = (item == iLinkOK);
    if (result) {
        GetDialogItem(dlg, iLinkField, &type, &itemH, &box);
        GetDialogItemText(itemH, url);
    }

    DisposeDialog(dlg);
    SetPort(gWindow);
    return result;
}

/*
    "Link" in Writer mode: prompts for a URL, then applies underline +
    a link ID (see AddLinkURL) to the current selection.
*/
void DoLinkHidden(void)
{
    Str255 url;

    if ((**gHiddenTE).selStart == (**gHiddenTE).selEnd)
        return;

    /* ShowLinkURLDialog fills `url` (via GetDialogItemText) before it
       returns true, and `url` is read only inside this if-body -- i.e.
       only on the path where it was written. cppcheck can't see across
       the call, so it flags a false positive on the next line. */
    /* cppcheck-suppress uninitvar ; url is written by ShowLinkURLDialog */
    if (ShowLinkURLDialog(url)) {
        TextStyle ts;
        short id;

        EnsureLinkRoom();
        id = AddLinkURL(url);
        if (id == 0) {
            /* Genuinely out of link slots even after reclaiming dead ones:
               this document really does have 64 live links. Tell the user
               rather than apply an underline with no URL behind it. */
            ShowError("\pThis document already has the most links ArtfulType can track (64).");
            return;
        }
        ts.tsFace = underline;
        SetLinkID(&ts, id);
        TESetStyle(doFace + doColor, &ts, true, gHiddenTE);
    }
}

void ToggleCode(void)
{
    TextStyle ts;
    short lh, fa;
    short monacoFont, timesFont;

    monacoFont = MonacoFont();
    timesFont = TimesFont();

    TEGetStyle((**gHiddenTE).selStart, &ts, &lh, &fa, gHiddenTE);
    ts.tsFont = (ts.tsFont == monacoFont) ? timesFont : monacoFont;
    TESetStyle(doFont, &ts, true, gHiddenTE);
}

void ToggleHeadingHidden(short level)
{
    short selStart;
    long lineStart, lineEnd;
    Handle textH;
    long len;
    TextStyle ts;
    short lh, fa;
    Boolean isThisLevel;

    selStart = (**gHiddenTE).selStart;
    textH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = lineStart;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    TEGetStyle((short) lineStart, &ts, &lh, &fa, gHiddenTE);
    isThisLevel = (ts.tsFace & bold) && (ts.tsSize == HeadingSizeForLevel(level));

    TESetSelect((short) lineStart, (short) lineEnd, gHiddenTE);
    if (isThisLevel) {
        ts.tsFace = normal;
        ts.tsSize = CurrentFontSize();
    } else {
        ts.tsFace = bold;
        ts.tsSize = HeadingSizeForLevel(level);
    }
    TESetStyle(doFace + doSize, &ts, true, gHiddenTE);
}

/*
    Sets the style at a zero-length selection (the insertion point) --
    Style TextEdit uses this as the style for whatever gets typed next,
    which is exactly what's needed after closing a live-converted span
    so typing doesn't keep inheriting bold/italic/code indefinitely.
*/
static void SetTypingStyleNormal(short pos)
{
    TextStyle ts;
    short fontNum;

    fontNum = TimesFont();
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    TESetSelect(pos, pos, gHiddenTE);
    TESetStyle(doFont + doFace + doSize, &ts, true, gHiddenTE);
}

/*
    Live "type the markdown, get the formatting" for Writer mode: called
    after every keystroke. Looks backward from the caret for a delimiter
    pair that the just-typed character completed, and if found, strips
    both delimiters and applies the corresponding style in place.
    Strikethrough has no native classic Mac text style, so it stays
    menu-only; everything else, including links, converts live.
*/
void DetectInlineMarkdown(char justTyped)
{
    Handle textH;
    long len;
    long caret;
    MdInlineEdit e;
    TextStyle ts;

    if (justTyped == '\r') {
        SetTypingStyleNormal((**gHiddenTE).selEnd);
        return;
    }

    textH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;
    caret = (**gHiddenTE).selEnd;

    /* All pattern matching is pure: read the locked buffer once, decide the
       edit, then unlock before touching the TE (TEDelete/TESetStyle may
       move memory, so nothing below dereferences textH). */
    HLock(textH);
    e = MdDetectInline(*textH, len, caret, justTyped);
    HUnlock(textH);

    if (e.kind == MD_INLINE_NONE)
        return;

    /* del1 is the higher-positioned range, so deleting it first leaves
       del2's and the style range's coordinates valid. */
    if (e.del1End > e.del1Start) {
        TESetSelect((short) e.del1Start, (short) e.del1End, gHiddenTE);
        TEDelete(gHiddenTE);
    }
    if (e.del2End > e.del2Start) {
        TESetSelect((short) e.del2Start, (short) e.del2End, gHiddenTE);
        TEDelete(gHiddenTE);
    }

    TESetSelect((short) e.styleStart, (short) e.styleEnd, gHiddenTE);
    {
        short mode;
        short linkID = 0;

        if (e.kind == MD_KIND_LINK) {
            /* CompactLinkTable preserves the selection we just set above, so
               it is safe to reclaim room here before claiming an ID. */
            EnsureLinkRoom();
            linkID = AddLinkURL(e.linkURL);
        }
        StyleForKind(e.kind, e.level, linkID, &ts, &mode);
        if (mode != 0)
            TESetStyle(mode, &ts, true, gHiddenTE);
    }

    if (e.resetNormal)
        SetTypingStyleNormal((short) e.newCaret);

    InvalidateHeightCache();
}

/* "None" in Writer mode: just clear the applied style on the selection. */
void ClearSelectionStyleHidden(void)
{
    TextStyle ts;
    short fontNum;

    if ((**gHiddenTE).selStart == (**gHiddenTE).selEnd)
        return;

    fontNum = TimesFont();
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gHiddenTE);
}

/*
    "None" in Markdown mode: strips any matched markdown delimiter pairs
    that fall entirely within the selection. Delimiters that extend
    outside the selection are left alone -- to clear those,
    extend the selection to include them, or toggle the specific Style
    menu item that applied them.
*/
void ClearMarkdownInSelection(void)
{
    Handle textH;
    short selStart, selEnd;
    Handle outH;
    long outLen;
    short spanCount;
    MdStripOpts opts;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    if (selStart == selEnd)
        return;

    textH = (**gTE).hText;
    outH = NewHandle(selEnd - selStart + 1);
    if (outH == NULL)
        return;

    HLock(textH);
    HLock(outH);
    /* Clear formatting: strip only the "# " prefix on a heading line (its
       body still gets inline-stripped), and use no link table. STRIP mode
       reproduces the old loop, which skipped "# " then fell through to the
       inline stripping. startsAtLineStart mirrors the old "i == 0 ||
       text[i-1] == '\r'" test for the selection's first character. */
    opts.headingMode = MD_HEADINGS_STRIP;
    opts.startsAtLineStart = (selStart == 0) || ((*textH)[selStart - 1] == '\r');
    outLen = MdStrip(*textH + selStart, (long) (selEnd - selStart), &opts,
                     *outH, (long) (selEnd - selStart + 1),
                     gStripSpans, MD_MAX_SPANS, &spanCount, (MdLinkTable *) 0);
    HUnlock(textH);

    TESetSelect(selStart, selEnd, gTE);
    TEDelete(gTE);
    /* Keep outH locked across TEInsert -- see BuildHiddenView. (The old
       code unlocked outH here before the insert too: the same latent
       dangling-pointer bug as the other three sites, fixed by moving to
       the shared adapter.) */
    TEInsert(*outH, outLen, gTE);
    HUnlock(outH);
    DisposeHandle(outH);

    TESetSelect(selStart, (short) (selStart + outLen), gTE);
}

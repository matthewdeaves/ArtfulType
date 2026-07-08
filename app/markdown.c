#include "app.h"

/*
    This file is the Mac adapter for the markdown style engine: it locks the
    TextEdit handles, calls the pure mdcore codec, and maps the result onto real
    classic TextStyle runs. Two paths encode the Writer view -- ApplySpanStyles
    paints mdcore's flattened runs through the pure MdRunToFields packing, and
    the live DetectInlineMarkdown converter styles one just-completed span
    through StyleForKind. Both express the same three channel conventions, and
    each convention has one canonical helper pair to edit it: a link ID in
    tsColor.red (Set/GetLinkID), the strike flag in tsColor.green (Set/GetStrike-
    Flag), and a heading as bold + a stepped tsSize (SetHeadingStyle/GetHeading-
    Level). The menu toggles (ToggleFace/ToggleCode/...) set a single field
    directly, and a handful of "reset to plain" sites zero all channels at once.
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
   be visually black). SetLinkID/GetLinkID are the canonical readers and writers
   of this convention; the only other writers are the full "reset to plain" sites
   (ClearStyles, SetTypingStyleNormal, ClearSelectionStyleHidden, and the base
   pass in ApplySpanStyles) that zero all three channels together, and Compact-
   LinkTable, which rewrites red ALONE on purpose to preserve a struck link's
   green channel (see its note). */
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

/* Strikethrough has no native classic text face, so a struck run is flagged
   in its run's tsColor.green (1 == struck) -- the exact same trick links use
   in tsColor.red, and just as imperceptible (green == 1 out of 65535 reads as
   black). red (link ID) and green (strike) are independent channels, so a
   struck link keeps both. TextEdit can't draw the line itself; DrawStruckRuns
   paints it after TextEdit lays the text down. */
static short GetStrikeFlag(const TextStyle *ts)
{
    return ts->tsColor.green ? 1 : 0;
}

/* The write half of the green-channel convention (mirrors SetLinkID for red):
   sets the strike flag to 0/1 without touching red (the link ID) or blue, so
   a struck link keeps both channels. The canonical writer of the "green carries
   strike, independent of red" invariant; the only other writers are the full
   reset-to-plain sites that zero all three channels at once. */
static void SetStrikeFlag(TextStyle *ts, short on)
{
    ts->tsColor.green = on ? 1 : 0;
}

/*
    Fast-path guard for DrawStruckRuns. That routine sweeps every visible
    character (TEGetStyle per char) after every keystroke and update, which is
    real, avoidable latency on an 8 MHz 68000 for the overwhelmingly common
    document that has no strikethrough at all. This flag is set true wherever a
    struck run can come into being, and recomputed from scratch on every
    BuildHiddenView (which clears it, then ApplySpanStyles re-sets it iff the
    rebuilt view actually contains strike). It is only ever set false there, so
    it can never hide a real struck run (at worst it over-reports after all
    strike is removed, reverting to today's always-sweep until the next
    rebuild). DrawStruckRuns early-outs when it is false. */
static Boolean gDocHasStrike = false;

/*
    Sets (on != 0) or clears the strike flag over te[from,to), preserving each
    run's link ID -- strike and link share tsColor but own separate channels,
    and TESetStyle writes the whole colour, so the flag can only be flipped by
    reading each run's colour first. Walks maximal (strike,link) runs like
    CompactLinkTable so one TESetStyle covers each. redraw is false: the flag
    is invisible, and the visible line is (re)painted by DrawStruckRuns / the
    update cycle, never by this.
*/
static void SetStrikeRange(TEHandle te, long from, long to, short on)
{
    long i = from;
    short want = on ? 1 : 0;

    while (i < to) {
        TextStyle st;
        short lh, fa;
        long runEnd;

        TEGetStyle((short) i, &st, &lh, &fa, te);
        runEnd = i + 1;
        while (runEnd < to) {
            TextStyle st2;
            short lh2, fa2;

            TEGetStyle((short) runEnd, &st2, &lh2, &fa2, te);
            if (GetStrikeFlag(&st2) != GetStrikeFlag(&st) ||
                st2.tsColor.red != st.tsColor.red)
                break;
            runEnd++;
        }
        if (GetStrikeFlag(&st) != want) {
            SetStrikeFlag(&st, want);
            if (want)
                gDocHasStrike = true;
            TESetSelect((short) i, (short) runEnd, te);
            TESetStyle(doColor, &st, false, te);
        }
        i = runEnd;
    }
}

/* The 1:1 bridge between mdcore's Toolbox-free MD_FACE_* bits and the classic
   Style constants. The bit layouts happen to coincide, but bridging explicitly
   (rather than casting) keeps the pure codec's abstract bits from depending on
   that coincidence -- the packing/round-trip invariant lives in mdcore; this
   just renames the bits at the Toolbox boundary. */
static Style ToolboxFaceFromMd(short mdFace)
{
    Style f = normal;
    if (mdFace & MD_FACE_BOLD)      f |= bold;
    if (mdFace & MD_FACE_ITALIC)    f |= italic;
    if (mdFace & MD_FACE_UNDERLINE) f |= underline;
    return f;
}

static short MdFaceFromToolbox(Style face)
{
    short f = 0;
    if (face & bold)      f |= MD_FACE_BOLD;
    if (face & italic)    f |= MD_FACE_ITALIC;
    if (face & underline) f |= MD_FACE_UNDERLINE;
    return f;
}

/* Body-relative heading sizes: the lossless level<->size mapping lives in the
   pure, host-tested MdHeadingSizeForLevel/MdHeadingLevelForSize (mdcore); these
   two just supply the document's current zoom size as the base. Keep paired. */
static short HeadingSizeForLevel(short level)
{
    return MdHeadingSizeForLevel(CurrentFontSize(), level);
}

static short HeadingLevelForSize(short size)
{
    return MdHeadingLevelForSize(CurrentFontSize(), size);
}

/* The style-level counterpart of the link/strike channel helpers: a heading is
   encoded in a run's TextStyle as bold + a heading-sized tsSize. GetHeadingLevel
   reports the level (1..3, or 0 when the run isn't a heading) and SetHeadingStyle
   writes the pair, so the "bold + heading size" convention has one home, just as
   Set/GetLinkID and Set/GetStrikeFlag do for their channels. */
static short GetHeadingLevel(const TextStyle *ts)
{
    if (!(ts->tsFace & bold))
        return 0;
    return HeadingLevelForSize(ts->tsSize);
}

static void SetHeadingStyle(TextStyle *ts, short level)
{
    ts->tsFace = bold;
    ts->tsSize = HeadingSizeForLevel(level);
}

/* Fills `ts` (and `mode`, the doFace/doFont/... mask naming which fields apply)
   with the style for a markdown span kind. linkID is used only for a LINK. Used
   by the live DetectInlineMarkdown converter to style one just-completed span;
   the run-painting path (ApplySpanStyles) encodes through the pure MdRunToFields
   codec instead, and the menu toggles set their one field directly. */
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
        SetHeadingStyle(ts, level);
        *mode = doFace + doSize;
        break;
    default:
        /* MD_KIND_STRIKE lands here: it can't be a plain TESetStyle because
           it shares tsColor with links and must preserve the link ID, so
           callers apply it with SetStrikeRange instead. mode 0 == "nothing
           to set here", which the span loops already skip. */
        *mode = 0;
        break;
    }
}

/* Scratch buffers for the pure strip pass (mdcore's MdStrip), shared by the
   non-reentrant BuildHiddenView / InsertMarkdownAsStyled /
   ClearMarkdownInSelection adapters -- kept at file scope so they stay off
   the small classic-Mac stack. */
static MdSpan gStripSpans[MD_MAX_SPANS];
static MdLinkTable gStripLinks;

/* Scratch for the run-based directions: the coalesced style runs of the range
   being encoded (emit) or applied (build), and a snapshot of the global link
   table in mdcore's own layout. Shared by the non-reentrant
   SyncHiddenToCanonical / EncodeSelectionAsMarkdown (emit) and ApplySpanStyles
   (build) adapters -- none of which run concurrently, so the one buffer is
   safe to alias between them. Sized to MD_MAX_RUNS (not MD_MAX_SPANS): the
   build side flattens up to MD_MAX_SPANS disjoint spans, which can be 2N+1
   runs, so a smaller buffer would fold the document's tail into one style. */
static MdRun gEmitRuns[MD_MAX_RUNS];
static MdLinkTable gEmitLinks;

/*
    Paints the styled runs MdStrip found onto te[offset, offset+textLen).
    `spans` are in stripped-text coordinates [0, textLen) and MAY OVERLAP
    (nested inline styles -- see mdcore.h), so they're first flattened by the
    pure MdSpansToRuns into non-overlapping, multi-attribute runs; each run
    then gets ONE combined TextStyle. That single combined write is what makes
    nesting render: a bold+strike run carries bold in tsFace and strike in
    tsColor.green together, where the old span-at-a-time approach would let a
    later span's face clobber an earlier one. link IDs are remapped through
    `remap` (NULL == use the run's own ID). Headings are line-level (never
    nested inside inline styles) and applied as a final pass over their spans.
    Shared by BuildHiddenView (whole document, offset 0, no remap) and
    InsertMarkdownAsStyled (the pasted range, offset and remapped).

    Reuses gEmitRuns as run scratch: the emit direction that owns it never runs
    concurrently with a build/paste, and keeping one 512-run buffer off the
    tiny classic-Mac stack matters more than the aliasing.
*/
static void ApplySpanStyles(TEHandle te, short offset, long textLen,
                            const MdSpan *spans, short count, const short *remap)
{
    TextStyle base;
    short timesFont, monacoFont;
    short nRuns, r, k;

    timesFont = TimesFont();
    monacoFont = MonacoFont();

    base.tsFont = timesFont;
    base.tsFace = normal;
    base.tsSize = CurrentFontSize();
    base.tsColor.red = base.tsColor.green = base.tsColor.blue = 0;
    TESetSelect(offset, (short) (offset + textLen), te);
    TESetStyle(doFont + doFace + doSize + doColor, &base, true, te);

    nRuns = MdSpansToRuns(textLen, spans, count, gEmitRuns, MD_MAX_RUNS);
    for (r = 0; r < nRuns; r++) {
        const MdRun *run = &gEmitRuns[r];
        MdStyleFields sf;
        TextStyle st;

        if (!run->bold && !run->italic && !run->code && !run->link && !run->strike)
            continue;                 /* plain run: base already covers it */

        /* One pure pack of every attribute into the style-run fields, then move
           them onto a real TextStyle. The link ID is remapped (local->global)
           here, the one adapter-side concern the pure codec can't own. */
        sf = MdRunToFields(run);
        st.tsFont = sf.code ? monacoFont : timesFont;
        st.tsFace = ToolboxFaceFromMd(sf.face);
        st.tsSize = CurrentFontSize();
        /* Write both colour channels through their owners: SetLinkID sets red
           (and clears green/blue), then SetStrikeFlag sets green -- byte-for-byte
           the old raw red=id / green=strike / blue=0, but via the convention's
           helpers rather than around them. Order matters (SetLinkID zeros green
           first). The ID is remapped local->global here, the one thing the pure
           codec can't own. */
        SetLinkID(&st, remap ? remap[sf.linkID] : sf.linkID);   /* 0 == no link */
        SetStrikeFlag(&st, (short) (sf.strike ? 1 : 0));
        if (sf.strike)
            gDocHasStrike = true;
        TESetSelect((short) (offset + run->start), (short) (offset + run->end), te);
        TESetStyle(doFont + doFace + doSize + doColor, &st, true, te);
    }

    /* Headings last: bold at a larger size over the line body. Their chars
       carry no inline attribute, so they were left at base above; set the
       heading face/size on top without disturbing the run styling around them. */
    for (k = 0; k < count; k++) {
        if (spans[k].kind == MD_KIND_HEADING) {
            TextStyle hs;
            SetHeadingStyle(&hs, spans[k].level);
            TESetSelect((short) (offset + spans[k].start),
                        (short) (offset + spans[k].end), te);
            TESetStyle(doFace + doSize, &hs, true, te);
        }
    }
}

/* File-local: every caller (paste, live-detect, the link dialog) lives in this
   adapter, so it needs no external linkage. */
static short AddLinkURL(const unsigned char *url)
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
                GetLinkID(&st2) != id ||
                GetStrikeFlag(&st2) != GetStrikeFlag(&st))
                break;
            runEnd++;
        }

        if (underlined && id >= 1 && id <= gLinkCount && remap[id] != id) {
            /* Rewrite only the link ID (red); keep this run's strike flag
               (green) -- green is uniform across the run thanks to the break
               condition above, so st's green stands for the whole run. */
            TextStyle ns = st;

            ns.tsColor.red = remap[id];
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

void RestoreDrawing(TEHandle te, const Rect *saved)
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

    /* Recompute the strike fast-path flag from scratch: ApplySpanStyles sets
       it true iff the rebuilt view actually contains a struck run. */
    gDocHasStrike = false;

    /* Base plain style plus the flattened, combined style runs. Whole
       document (offset 0), no link remap (the spans already carry the
       freshly-published global IDs). */
    ApplySpanStyles(gHiddenTE, 0, outLen, gStripSpans, spanCount, NULL);

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
   Breaks runs on the bold/italic/code/link/strike booleans only -- a run keeps
   the linkID of its first character, matching the old emit loop, which captured
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
        MdStyleFields sf;
        MdRun cur;

        /* Read the run's style fields off the TextStyle and unpack them with
           the same pure codec ApplySpanStyles packs with -- the exact inverse,
           so the Writer<->Markdown round-trip is provably lossless (see the
           MdRunToFields/MdFieldsToRun round-trip test). */
        TEGetStyle((short) i, &st, &lh, &fa, te);
        sf.face = MdFaceFromToolbox(st.tsFace);
        sf.code = (st.tsFont == monacoFont);
        sf.linkID = GetLinkID(&st);
        sf.strike = GetStrikeFlag(&st);
        MdFieldsToRun(&sf, &cur);

        /* Coalesce on the five style booleans only -- NOT linkID: a run keeps
           the link ID of its first character, matching the old emit loop (which
           captured a link's URL when it opened and ignored a mid-underline id
           change). */
        if (n > 0 && runs[n - 1].bold == cur.bold && runs[n - 1].italic == cur.italic &&
            runs[n - 1].code == cur.code && runs[n - 1].link == cur.link &&
            runs[n - 1].strike == cur.strike) {
            runs[n - 1].end = (i - from) + 1;
        } else if (n < cap) {
            cur.start = i - from;
            cur.end = (i - from) + 1;
            runs[n] = cur;
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
            headingLevel = GetHeadingLevel(&firstStyle);
            isHeading = (headingLevel > 0);
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
                                            monacoFont, gEmitRuns, MD_MAX_RUNS);
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
    runCount = BuildStyleRuns(te, start, end, monacoFont, gEmitRuns, MD_MAX_RUNS);

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
    ApplySpanStyles(te, insertStart, outLen, gStripSpans, spanCount, remap);

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

/*
    Strikethrough toggle in Writer mode. Strike isn't a native text face, so
    it's carried as the tsColor.green flag (see SetStrikeRange) and the visible
    line is painted by DrawStruckRuns; the caller repaints the content area
    afterward so the line appears (or a cleared one is erased). The toggle
    direction is read from the selection start, matching ToggleFace/ToggleCode.
*/
void ToggleStrike(void)
{
    short selStart = (**gHiddenTE).selStart;
    short selEnd = (**gHiddenTE).selEnd;
    TextStyle ts;
    short lh, fa, on;

    TEGetStyle(selStart, &ts, &lh, &fa, gHiddenTE);
    on = GetStrikeFlag(&ts) ? 0 : 1;

    /* Turning strike on (via either path below) means the view now has, or is
       about to have, a struck run -- arm the DrawStruckRuns fast path. */
    if (on)
        gDocHasStrike = true;

    if (selStart == selEnd) {
        /* Empty selection: park the flag as the typing style so what gets
           typed next is (un)struck, exactly like ToggleFace at a caret. */
        SetStrikeFlag(&ts, on);
        TESetStyle(doColor, &ts, true, gHiddenTE);
    } else {
        SetStrikeRange(gHiddenTE, selStart, selEnd, on);
        TESetSelect(selStart, selEnd, gHiddenTE);
    }
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
    isThisLevel = (GetHeadingLevel(&ts) == level);

    TESetSelect((short) lineStart, (short) lineEnd, gHiddenTE);
    if (isThisLevel) {
        ts.tsFace = normal;
        ts.tsSize = CurrentFontSize();
    } else {
        SetHeadingStyle(&ts, level);
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
    /* Reset the colour too, so neither a link ID (red) nor the strike flag
       (green) bleeds into whatever is typed after a completed span. */
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetSelect(pos, pos, gHiddenTE);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gHiddenTE);
}

/*
    Live "type the markdown, get the formatting" for Writer mode: called
    after every keystroke. Looks backward from the caret for a delimiter
    pair that the just-typed character completed, and if found, strips
    both delimiters and applies the corresponding style in place. All inline
    styles convert live, strikethrough (~~) included: it has no native classic
    Mac text face, so it's applied as the tsColor.green flag via SetStrikeRange
    and painted by DrawStruckRuns, while the others map to real TextEdit faces.
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
    if (e.kind == MD_KIND_STRIKE) {
        /* Strike shares tsColor with links, so it goes through the same
           read-modify-write path (never a plain TESetStyle). */
        SetStrikeRange(gHiddenTE, e.styleStart, e.styleEnd, 1);
    } else {
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

/*
    Paints the strike-through line over every struck run (tsColor.green == 1)
    visible in te's view. Strike is not a native text face, so TextEdit never
    draws it; this is called immediately after TextEdit lays the text down (in
    DoUpdate, and after each Writer-mode keystroke) so the line always tracks
    the text. Scrolling needs no special case: TEScroll shifts the drawn line
    with the text, and any newly exposed line repaints through here.

    Walks display lines via the TERec's lineStarts[], skips those outside the
    view, and within each visible line draws one segment per maximal run of
    struck characters sharing a font/face/size (so TextWidth measures it). The
    per-visible-line cost mirrors the existing per-char TEGetStyle sweeps, so
    gDocHasStrike short-circuits the whole thing for the common document that
    has no strikethrough -- no per-keystroke sweep unless strike is in play.
    (Only meaningful for gHiddenTE; gTE never carries the green flag, and its
    caller in the Markdown-mode path is already gated on gHideMarkdown.)

    TEScroll redraws the scrolled text immediately without posting an update
    event, so it does NOT repaint the overpainted strike line -- every scroll
    path (scrolling.c) calls this afterward in Writer mode, the same way the
    keystroke path in main.c does.
*/
void DrawStruckRuns(TEHandle te)
{
    Rect view;
    short nLines, L;
    long teLen;
    Handle hText;
    PenState savePen;
    short saveFont, saveFace, saveSize;
    RgnHandle saveClip;
    Rect clipR;
    Pattern blackPat;
    short pi;

    if (!gDocHasStrike)
        return;

    view = (**te).viewRect;
    nLines = (**te).nLines;
    teLen = (**te).teLength;
    hText = (**te).hText;
    if (teLen == 0 || nLines == 0 || hText == NULL)
        return;

    saveClip = NewRgn();
    if (saveClip == NULL)
        return;
    GetClip(saveClip);
    /* Confine drawing to the text view intersected with whatever clip is
       already in force (the update region inside DoUpdate, the full port when
       called after a keystroke) so a partially-visible top/bottom line can't
       spill over the margins or scrollbar. */
    if (!SectRect(&view, &(**saveClip).rgnBBox, &clipR))
        clipR = view;
    ClipRect(&clipR);

    GetPenState(&savePen);
    saveFont = qd.thePort->txFont;
    saveFace = qd.thePort->txFace;
    saveSize = qd.thePort->txSize;
    PenNormal();
    BackColor(whiteColor);
    for (pi = 0; pi < 8; pi++)
        blackPat.pat[pi] = 0xFF;   /* solid black, independent of qd.black init */

    HLock(hText);
    for (L = 0; L < nLines; L++) {
        long ls = (**te).lineStarts[L];
        long le = (L + 1 < nLines) ? (**te).lineStarts[L + 1] : teLen;
        Point base = TEGetPoint((short) ls, te);
        long c;

        if (base.v < view.top - MAX_LINE_HEIGHT)
            continue;                 /* wholly above the view */
        if (base.v - MAX_LINE_HEIGHT > view.bottom)
            break;                    /* this and every later line are below */

        c = ls;
        while (c < le) {
            TextStyle st;
            short lh, fa;
            long segEnd;
            short segFont, segFace, segSize;
            FontInfo fi;
            Point p;
            short x0, y, w;

            TEGetStyle((short) c, &st, &lh, &fa, te);
            if (!GetStrikeFlag(&st)) {
                c++;
                continue;
            }

            segFont = st.tsFont;
            segFace = st.tsFace;
            segSize = st.tsSize;
            segEnd = c + 1;
            while (segEnd < le) {
                TextStyle st2;
                short lh2, fa2;

                TEGetStyle((short) segEnd, &st2, &lh2, &fa2, te);
                if (!GetStrikeFlag(&st2) || st2.tsFont != segFont ||
                    st2.tsFace != segFace || st2.tsSize != segSize)
                    break;
                segEnd++;
            }

            p = TEGetPoint((short) c, te);
            x0 = p.h;
            TextFont(segFont);
            TextFace(segFace);
            TextSize(segSize);
            /* TEGetPoint's vertical is the BOTTOM of the line, so the text
               baseline sits one descent higher. Strike through the middle of
               the glyph body -- about a third of the ascent above the baseline.
               (The original code treated p.v as the baseline and used the
               style's fontAscent, which dropped the line onto the baseline
               where it read as an underline.) GetFontInfo reflects the font/
               face/size just set above. */
            GetFontInfo(&fi);
            y = (short) (p.v - fi.descent - fi.ascent / 3);
            w = TextWidth(*hText, (short) c, (short) (segEnd - c));
            if (w > 0) {
                /* KNOWN ISSUE (#9): on the tested emulators this line paints in
                   white (the highlight/background colour) when the struck text is
                   NOT selected, and black when it is -- so strikethrough is
                   effectively invisible on white paper. The position and
                   scroll-repaint are correct; only the colour is wrong. We
                   re-assert every drawing attribute below (highlight bit, fore
                   colour, solid-black pen, patCopy) as a best effort; the fill
                   still comes out highlighted, pointing at per-run state the
                   TextEdit calls above leave in the port under the MPW-interfaces
                   build. The line is left in because it DOES render (just the
                   wrong colour); the colour is tracked in issue #9.

                   Re-assert the QuickDraw highlight bit: the per-run TextEdit
                   calls (TEGetStyle/TEGetPoint) can leave the HiliteMode highlight
                   bit CLEAR, which makes a fill paint in the highlight colour --
                   the classic "my drawing came out highlighted" gotcha (Inside
                   Macintosh: Imaging, Highlighting). This did not fully cure it. */
                Rect lineRect;
                LMSetHiliteMode((UInt8) (LMGetHiliteMode() | (1 << hiliteBit)));
                ForeColor(blackColor);
                PenPat(&blackPat);
                PenMode(patCopy);
                SetRect(&lineRect, x0, y, (short) (x0 + w), (short) (y + 1));
                PaintRect(&lineRect);
            }
            c = segEnd;
        }
    }
    HUnlock(hText);

    TextFont(saveFont);
    TextFace(saveFace);
    TextSize(saveSize);
    SetPenState(&savePen);
    SetClip(saveClip);
    DisposeRgn(saveClip);
}

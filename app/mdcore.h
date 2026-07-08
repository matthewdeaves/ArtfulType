/*
    mdcore -- the pure, Toolbox-free markdown engine at the heart of
    ArtfulType.

    Everything here operates on plain char buffers and the small model
    structs below. It includes no Mac headers, references no TextEdit
    record and no globals, so it compiles and runs on any host with a C
    compiler -- which is what lets it be unit-tested in milliseconds
    (see tests/). The Mac side (markdown.c) is a thin adapter: it locks
    the TextEdit handles, calls into mdcore, and maps the resulting spans
    onto real TextStyle runs. The mapping from a markdown "kind" to a
    concrete bold/Monaco/heading-size style lives only in that adapter.
*/
#ifndef MDCORE_H
#define MDCORE_H

/* Caps shared by the pure core and the Mac adapter. */
#define MD_MAX_LINKS 64
#define MD_MAX_SPANS 512

/* Upper bound on the runs MdSpansToRuns can produce from MD_MAX_SPANS spans.
   Each span contributes at most two run boundaries (its start and end), so N
   disjoint spans flatten to at most 2N+1 runs -- e.g. a styled word, a plain
   gap, another styled word... A run buffer sized to this can never overflow
   (and so never has to fold the tail into one wrong style). */
#define MD_MAX_RUNS (2 * MD_MAX_SPANS + 1)

/* A styled span, expressed in the STRIPPED text's own coordinates
   (i.e. after the delimiter characters have been removed). kind is one
   of the MD_KIND_* letters below; level is 1..3 for a heading, else 0;
   linkID is a 1-based index into an MdLinkTable for a link, else 0. */
#define MD_KIND_BOLD    'B'
#define MD_KIND_ITALIC  'I'
#define MD_KIND_CODE    'C'
#define MD_KIND_LINK    'L'
#define MD_KIND_HEADING 'H'
#define MD_KIND_STRIKE  'S'

typedef struct {
    long  start;
    long  end;
    short kind;
    short level;
    short linkID;
} MdSpan;

/* Link URLs, stored Pascal-string style (byte [0] is the length). Index 0
   is unused; valid link IDs run 1..count, matching the linkID in MdSpan. */
typedef struct {
    short         count;
    unsigned char url[MD_MAX_LINKS + 1][256];
} MdLinkTable;

/* How MdStrip treats a leading "# " at the start of a line. */
#define MD_HEADINGS_OFF   0  /* ignore '#'; leave "# " literal (paste path) */
#define MD_HEADINGS_SPAN  1  /* strip the whole heading, record an H span   */
                             /* over its body (the Writer-view path)        */
#define MD_HEADINGS_STRIP 2  /* strip only the "# " prefix and keep parsing */
                             /* the body inline, recording no span (the     */
                             /* "clear formatting" path)                    */

/* Options controlling MdStrip. */
typedef struct {
    int headingMode;       /* one of MD_HEADINGS_* above                   */
    int startsAtLineStart; /* is src[0] the first character of a line?     */
} MdStripOpts;

/*
    Strips markdown delimiters (**bold**, *italic*, `code`, ~~strike~~,
    ***bold italic***, [text](url), and leading "# " headings) from
    src[0..len), writing the surviving text to out and recording where each
    styled run landed.

    Inline styles NEST: the content of a delimiter pair is itself stripped, so
    ~~**x**~~ records a strike span AND a bold span over the same "x", a linked
    [**x**](u) records link + bold, and ***x*** records bold + italic. Nested
    spans therefore OVERLAP (share stripped-text coordinates); a consumer that
    needs one attribute set per character coalesces them with MdSpansToRuns.
    (Headings are line-level and never nest inside inline styles.)

    - out must have room for at least len bytes (stripping never grows the
      text). Returns the stripped length written to out.
    - spans receives up to spanCap styled runs; *spanCount gets the count.
      Runs past spanCap are still stripped, just not recorded (matching the
      old MAX_STYLE_OPS behaviour). With nesting a single character can be
      covered by several spans, so a heavily nested doc uses spans faster.
    - links receives the URLs found (reset to empty first). Pass NULL if the
      caller doesn't need them (e.g. "clear formatting"); link text is still
      stripped, spans just carry linkID 0.

    Pure: no allocation, no globals, no Toolbox.
*/
long MdStrip(const char *src, long len, const MdStripOpts *opts,
             char *out, long outCap,
             MdSpan *spans, short spanCap, short *spanCount,
             MdLinkTable *links);

/* A maximal run of identically-styled characters (in src coordinates).
   The five attributes combine freely -- a run can be bold + italic + code
   + link + strike all at once -- exactly as classic TextEdit reports a
   styled run. linkID indexes an MdLinkTable when link is set. */
typedef struct {
    long  start;
    long  end;
    int   bold;
    int   italic;
    int   code;
    int   link;
    int   strike;
    short linkID;
} MdRun;

/*
    Flattens the (possibly overlapping, single-kind) spans MdStrip produces
    over textLen stripped characters into a contiguous list of maximal
    same-style MdRuns -- the multi-attribute view TextEdit and MdEmitInline
    both want. For each character it ORs together the attributes of every
    span covering it (so a char inside ~~**x**~~ comes out bold AND strike),
    carries the linkID of whatever LINK span covers it, then coalesces equal
    neighbours. HEADING spans are line-level, carry no inline attribute, and
    are ignored here (the adapter applies them separately). Writes up to `cap`
    runs, folding any overflow into the last run so no character is dropped;
    returns the run count. Pure: no allocation, no globals, no Toolbox.
*/
short MdSpansToRuns(long textLen, const MdSpan *spans, short spanCount,
                    MdRun *runs, short cap);

/*
    Abstract text-face bits. A classic-Mac Style byte spells bold/italic/
    underline with exactly this bit layout, so the adapter maps between these
    and the Toolbox constants with a trivial 1:1 bridge -- but the pure codec
    below (and its host test) needs no Toolbox Style type to express a face.
*/
#define MD_FACE_BOLD      0x01
#define MD_FACE_ITALIC    0x02
#define MD_FACE_UNDERLINE 0x04

/*
    The four concrete style-run fields ArtfulType packs an MdRun's attributes
    into: a face bitmask (MD_FACE_*), a "this run is the code font" flag, and
    the two TextStyle colour channels the app repurposes -- red carries a
    1-based link ID (0 == no link), green carries the strike flag (no classic
    face exists for strikethrough). This struct is deliberately Toolbox-free so
    the round-trip below is unit-testable off the Mac.
*/
typedef struct {
    short face;    /* OR of MD_FACE_*                         */
    int   code;    /* 1 => the run uses the code (Monaco) font */
    short linkID;  /* red channel: 1..MD_MAX_LINKS, 0 == none  */
    int   strike;  /* green channel: 1 == struck               */
} MdStyleFields;

/*
    The single encoding of "an MdRun's five attributes as style-run fields",
    and its exact inverse. This is the combined-write invariant Writer mode
    lives on: bold+strike+link must coexist in ONE field set (face carries the
    faces, red the link ID, green the strike flag -- red and green independent),
    and reading it back must recover every attribute. The Mac adapter
    (ApplySpanStyles / BuildStyleRuns) moves these fields into and out of a real
    TextStyle; extracting the packing here lets a host test prove every one of
    the 32 attribute combinations round-trips. MdFieldsToRun leaves run->start
    and run->end untouched (fields carry no coordinates); the caller owns those.
    Pure: no allocation, no globals, no Toolbox.
*/
MdStyleFields MdRunToFields(const MdRun *run);
void          MdFieldsToRun(const MdStyleFields *fields, MdRun *run);

/*
    A heading renders in the Writer view as bold at a size stepped up from the
    body text: level 1 is largest, each deeper level 4pt smaller, so a level-N
    heading (N == 1..3) is baseSize + (4 - N) * 4 points. MdHeadingLevelForSize
    is the exact inverse -- it returns 0 for any size that is not one of those
    heading steps -- so the Writer<->Markdown round-trip never loses or invents
    a heading level. baseSize is the document's current body size (the app's
    zoom size), passed in rather than read from a global so this stays a pure,
    Toolbox-free pair the host tests exercise directly. Keep the two paired.
    Pure: no allocation, no globals, no Toolbox.
*/
short MdHeadingSizeForLevel(short baseSize, short level);
short MdHeadingLevelForSize(short baseSize, short size);

/*
    Emits inline markdown (**bold**, *italic*, `code`, ~~strike~~,
    [text](url)) for
    src[0..len) given `runs` that partition it -- contiguous, in order,
    covering every character. Delimiters open and close as the run
    attributes change, innermost-first on close and outermost-first on
    open (link is the outer wrapper), and everything still open is closed
    at the end. links supplies URLs for linked runs (may be NULL -> the
    URL comes out empty). Returns the number of bytes written to out.

    Never writes past outCap: callers size out generously (see the adapters),
    but because a single character can toggle all five styles -- emitting ~12
    delimiter bytes for one content byte -- a pathologically over-styled buffer
    could otherwise exceed any fixed n*k+c estimate and overrun the heap. When
    the buffer fills, further bytes are dropped (the tail truncates) rather than
    written out of bounds; the return value never exceeds outCap.

    This is the reverse of MdStrip and the exact per-run equivalent of the
    old per-character TEGetStyle emit loop. Pure: no globals, no Toolbox.
*/
long MdEmitInline(const char *src, long len,
                  const MdRun *runs, short runCount,
                  const MdLinkTable *links,
                  char *out, long outCap);

/*
    The plan MdDetectInline returns: the exact edit the Mac adapter applies
    when a keystroke completes a markdown pattern. kind is MD_INLINE_NONE
    when nothing matched (the adapter leaves the text alone), otherwise an
    MD_KIND_* naming the style to apply.

    The adapter performs, in this order:
      1. delete [del1Start, del1End)   -- the higher-positioned range, so
      2. delete [del2Start, del2End)   -- this one's coordinates stay valid
      3. style  [styleStart, styleEnd) -- an empty range means "typing style"
      4. if resetNormal, park a normal typing style at newCaret

    Every position is in the pre-edit buffer's coordinates; because del1 is
    always at higher offsets than del2 and the style range, running the two
    deletes in order lands the style range exactly where these fields say.
    A range with start == end is empty and skipped (headings have no del2).
    linkURL carries the Pascal-string URL for kind == MD_KIND_LINK (the
    adapter registers it and colours the run); it is empty otherwise.
*/
#define MD_INLINE_NONE 0  /* no pattern completed; leave the buffer alone */

typedef struct {
    short         kind;      /* MD_INLINE_NONE, or the MD_KIND_* matched   */
    short         level;     /* heading level 1..3 (kind==HEADING), else 0 */
    long          del1Start, del1End;   /* first (higher) deletion         */
    long          del2Start, del2End;   /* second (lower) deletion         */
    long          styleStart, styleEnd; /* range to style (empty => typing)*/
    long          newCaret;  /* where resetNormal parks the typing style   */
    int           resetNormal; /* 1: normal typing style at newCaret after */
    unsigned char linkURL[256]; /* Pascal URL for a link, else [0] == 0    */
} MdInlineEdit;

/*
    Live "type the markdown, get the formatting" detector for Writer mode.
    Given the whole buffer, its length, the caret (selEnd, just past the
    inserted character) and the character justTyped, decides whether that
    keystroke completed **bold**, *italic*, `code`, ~~strike~~,
    [text](url), or a leading "# " heading, and returns the edit plan
    above. Pure: it reads
    buf but mutates nothing. '\r' is handled by the adapter, not here.
*/
MdInlineEdit MdDetectInline(const char *buf, long len, long caret, char justTyped);

#endif /* MDCORE_H */

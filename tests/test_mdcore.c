/*
 * test_mdcore.c -- Golden tests for the pure markdown strip direction.
 *
 * MdStrip turns markdown source into stripped text + styled spans + a link
 * table. Each case pins down exactly what a given input must produce; the
 * cases are chosen to fail if the delimiter matching, span coordinates, or
 * link capture regress.
 */
#include <string.h>
#include "test_util.h"
#include "mdcore.h"

static char g_out[8192];
static MdSpan g_spans[MD_MAX_SPANS];
static short g_nSpans;
static MdLinkTable g_links;
static char g_out2[8192];
static MdRun g_runs[MD_MAX_RUNS];

/* Run MdStrip over a C string; returns stripped length. headingMode is one
   of MD_HEADINGS_OFF / _SPAN / _STRIP. */
static long strip(const char *src, int headingMode, int atLineStart)
{
    MdStripOpts opts;
    opts.headingMode = headingMode;
    opts.startsAtLineStart = atLineStart;
    return MdStrip(src, (long) strlen(src), &opts,
                   g_out, (long) sizeof g_out,
                   g_spans, MD_MAX_SPANS, &g_nSpans, &g_links);
}

static void test_plain(void)
{
    long n = strip("hello world", 0, 1);
    CHECK_STR(g_out, n, "hello world", "plain text passes through");
    CHECK_EQ(g_nSpans, 0, "plain text has no spans");
}

static void test_bold(void)
{
    long n = strip("**hi**", 0, 1);
    CHECK_STR(g_out, n, "hi", "bold delimiters stripped");
    CHECK_EQ(g_nSpans, 1, "one bold span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_BOLD, "kind is bold");
    CHECK_EQ(g_spans[0].start, 0, "bold span start");
    CHECK_EQ(g_spans[0].end, 2, "bold span end");
}

static void test_italic_code(void)
{
    long n = strip("*i* `c`", 0, 1);
    CHECK_STR(g_out, n, "i c", "italic and code stripped");
    CHECK_EQ(g_nSpans, 2, "two spans");
    CHECK_EQ(g_spans[0].kind, MD_KIND_ITALIC, "first italic");
    CHECK_EQ(g_spans[0].start, 0, "italic start");
    CHECK_EQ(g_spans[0].end, 1, "italic end");
    CHECK_EQ(g_spans[1].kind, MD_KIND_CODE, "second code");
    CHECK_EQ(g_spans[1].start, 2, "code start (after 'i ')");
    CHECK_EQ(g_spans[1].end, 3, "code end");
}

static void test_bold_beats_italic(void)
{
    /* "**x**" must parse as bold, not as two empty italics. */
    long n = strip("a **x** b", 0, 1);
    CHECK_STR(g_out, n, "a x b", "mixed inline stripped");
    CHECK_EQ(g_nSpans, 1, "single bold span in the middle");
    CHECK_EQ(g_spans[0].kind, MD_KIND_BOLD, "middle span is bold");
    CHECK_EQ(g_spans[0].start, 2, "bold starts at 'x'");
    CHECK_EQ(g_spans[0].end, 3, "bold ends after 'x'");
}

static void test_strike(void)
{
    long n = strip("~~gone~~", 0, 1);
    CHECK_STR(g_out, n, "gone", "strike delimiters stripped");
    CHECK_EQ(g_nSpans, 1, "one strike span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_STRIKE, "kind is strike");
    CHECK_EQ(g_spans[0].start, 0, "strike span start");
    CHECK_EQ(g_spans[0].end, 4, "strike span covers 'gone'");

    /* In context, and not confused by a single '~'. */
    n = strip("a ~~x~~ b", 0, 1);
    CHECK_STR(g_out, n, "a x b", "strike in context stripped");
    CHECK_EQ(g_nSpans, 1, "single strike span in the middle");
    CHECK_EQ(g_spans[0].start, 2, "strike starts at 'x'");
    CHECK_EQ(g_spans[0].end, 3, "strike ends after 'x'");

    /* A lone '~~' with no partner on the line stays literal. */
    n = strip("a ~~ b", 0, 1);
    CHECK_STR(g_out, n, "a ~~ b", "unmatched ~~ kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for an unmatched ~~");

    /* A single tilde is never a delimiter. */
    n = strip("1~2", 0, 1);
    CHECK_STR(g_out, n, "1~2", "lone tilde kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for a lone tilde");

    /* A run of three or more tildes is not a valid ~~ delimiter: ~~~word~~~
       must round-trip as literal text, not collapse to a strike span (the
       bug behind "can't type the last ~" / the whole row highlighting). */
    n = strip("~~~word~~~", 0, 1);
    CHECK_STR(g_out, n, "~~~word~~~", "triple ~~~ kept literal");
    CHECK_EQ(g_nSpans, 0, "no strike span for a ~~~ run");

    /* Mismatched run lengths (2 open, 3 close) also stay literal. */
    n = strip("~~word~~~", 0, 1);
    CHECK_STR(g_out, n, "~~word~~~", "mismatched ~~ / ~~~ kept literal");
    CHECK_EQ(g_nSpans, 0, "no strike span for mismatched tilde runs");
}

static void test_highlight(void)
{
    long n = strip("==mark==", 0, 1);
    CHECK_STR(g_out, n, "mark", "highlight delimiters stripped");
    CHECK_EQ(g_nSpans, 1, "one highlight span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_HIGHLIGHT, "kind is highlight");
    CHECK_EQ(g_spans[0].start, 0, "highlight span start");
    CHECK_EQ(g_spans[0].end, 4, "highlight span covers 'mark'");

    /* In context, and not confused by a single '='. */
    n = strip("a ==x== b", 0, 1);
    CHECK_STR(g_out, n, "a x b", "highlight in context stripped");
    CHECK_EQ(g_nSpans, 1, "single highlight span in the middle");
    CHECK_EQ(g_spans[0].start, 2, "highlight starts at 'x'");
    CHECK_EQ(g_spans[0].end, 3, "highlight ends after 'x'");

    /* A lone '==' with no partner on the line stays literal. */
    n = strip("a == b", 0, 1);
    CHECK_STR(g_out, n, "a == b", "unmatched == kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for an unmatched ==");

    /* A single equals is never a delimiter. */
    n = strip("1=2", 0, 1);
    CHECK_STR(g_out, n, "1=2", "lone equals kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for a lone equals");

    /* A run of three or more equals is not a valid == delimiter. */
    n = strip("===mark===", 0, 1);
    CHECK_STR(g_out, n, "===mark===", "triple === kept literal");
    CHECK_EQ(g_nSpans, 0, "no highlight span for a === run");
}

static void test_codeblock(void)
{
    /* A ``` fence hides its markers (like every other style) and emits the body
       as one Monaco CODE span. The markers -- opening, closing, and info string --
       are dropped; only the body remains. */
    long n = strip("```\rcode block\r```", 0, 1);
    CHECK_STR(g_out, n, "code block", "fence markers dropped, body kept");
    CHECK_EQ(g_nSpans, 1, "one code span for the whole body");
    CHECK_EQ(g_spans[0].kind, MD_KIND_CODE, "block body is code (Monaco)");
    CHECK_EQ(g_spans[0].start, 0, "code span start");
    CHECK_EQ(g_spans[0].end, 10, "code span covers 'code block'");

    /* The body is LITERAL -- never re-parsed as inline markdown. A backtick or an
       unmatched delimiter in the body must survive verbatim. */
    n = strip("```\ra`b\r```", 0, 1);
    CHECK_STR(g_out, n, "a`b", "a backtick in the body is preserved verbatim");
    n = strip("```\r**x**\r```", 0, 1);
    CHECK_STR(g_out, n, "**x**", "bold markers in the body stay literal code");

    /* The opener's info string ("```ruby") is dropped. */
    n = strip("```ruby\rputs 1\r```", 0, 1);
    CHECK_STR(g_out, n, "puts 1", "opener info string is dropped");

    /* A block between paragraphs drops markers, keeps the surrounding breaks. */
    n = strip("before\r```\rcode\r```\rafter", 0, 1);
    CHECK_STR(g_out, n, "before\rcode\rafter", "markers gone, before/after intact");

    /* Multi-line body: the CODE span INCLUDES the internal '\r' -- that Monaco
       line break is exactly what the emit uses to re-fence a multi-line block. */
    n = strip("```\rl1\rl2\r```", 0, 1);
    CHECK_STR(g_out, n, "l1\rl2", "multi-line code body keeps its line break");
    CHECK_EQ(g_nSpans, 1, "one span across the whole multi-line body");
    CHECK_EQ(g_spans[0].start, 0, "multi-line body span start");
    CHECK_EQ(g_spans[0].end, 5, "span covers 'l1\\rl2' including the newline");
}

static void test_link(void)
{
    long n = strip("see [text](http://x) end", 0, 1);
    CHECK_STR(g_out, n, "see text end", "link renders as its text");
    CHECK_EQ(g_nSpans, 1, "one link span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_LINK, "kind link");
    CHECK_EQ(g_spans[0].start, 4, "link span starts at 'text'");
    CHECK_EQ(g_spans[0].end, 8, "link span covers 'text'");
    CHECK_EQ(g_spans[0].linkID, 1, "first link id is 1");
    CHECK_EQ(g_links.count, 1, "one url captured");
    CHECK_STR(&g_links.url[1][1], g_links.url[1][0], "http://x", "url captured verbatim");
}

static void test_link_without_table(void)
{
    /* Clear-formatting path passes links=NULL: text still stripped. */
    MdStripOpts opts;
    long n;
    opts.headingMode = MD_HEADINGS_SPAN;
    opts.startsAtLineStart = 1;
    n = MdStrip("[t](u)", 6, &opts, g_out, sizeof g_out,
                g_spans, MD_MAX_SPANS, &g_nSpans, (MdLinkTable *) 0);
    CHECK_STR(g_out, n, "t", "link text stripped with no table");
    CHECK_EQ(g_spans[0].linkID, 0, "linkID is 0 when no table");
}

static void test_heading(void)
{
    long n = strip("## Title", 1, 1);
    CHECK_STR(g_out, n, "Title", "heading marker stripped");
    CHECK_EQ(g_nSpans, 1, "one heading span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_HEADING, "kind heading");
    CHECK_EQ(g_spans[0].level, 2, "level 2");
    CHECK_EQ(g_spans[0].start, 0, "heading span start");
    CHECK_EQ(g_spans[0].end, 5, "heading span covers 'Title'");
}

static void test_heading_disabled(void)
{
    /* Inline (paste) parsing leaves a literal "# " alone. */
    long n = strip("# Title", 0, 1);
    CHECK_STR(g_out, n, "# Title", "heading not stripped when disabled");
    CHECK_EQ(g_nSpans, 0, "no spans when heading disabled");
}

static void test_heading_needs_line_start(void)
{
    /* startsAtLineStart=0 => src[0] is mid-line, so no heading at 0. */
    long n = strip("# Title", 1, 0);
    CHECK_STR(g_out, n, "# Title", "no heading when not at a line start");
}

static void test_interior_heading(void)
{
    long n = strip("a\r# B", 1, 1);
    CHECK_STR(g_out, n, "a\rB", "heading detected after a return");
    CHECK_EQ(g_nSpans, 1, "one heading span");
    CHECK_EQ(g_spans[0].start, 2, "span starts after 'a\\r'");
    CHECK_EQ(g_spans[0].level, 1, "level 1");
}

static void test_heading_strip_mode(void)
{
    /* MD_HEADINGS_STRIP (the "clear formatting" path): drop the "# " prefix
       but keep stripping inline markdown in the heading body -- exactly what
       the old ClearMarkdownInSelection did. */
    long n = strip("# **Bold** and *it*", MD_HEADINGS_STRIP, 1);
    CHECK_STR(g_out, n, "Bold and it", "strip mode clears heading prefix AND inline");
    n = strip("## `code` x", MD_HEADINGS_STRIP, 1);
    CHECK_STR(g_out, n, "code x", "strip mode over a level-2 heading");
    n = strip("# [t](u) y", MD_HEADINGS_STRIP, 1);
    CHECK_STR(g_out, n, "t y", "strip mode clears a link inside a heading");

    /* SPAN mode, by contrast, leaves the heading body verbatim. */
    n = strip("# **Bold**", MD_HEADINGS_SPAN, 1);
    CHECK_STR(g_out, n, "**Bold**", "span mode keeps the heading body verbatim");
}

static void test_unmatched_delimiters(void)
{
    /* A lone '*' with no partner on the line stays literal, no span. */
    long n = strip("a * b", 0, 1);
    CHECK_STR(g_out, n, "a * b", "lone star kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for a lone star");

    /* A lone '`' likewise. */
    n = strip("x ` y", 0, 1);
    CHECK_STR(g_out, n, "x ` y", "lone backtick kept literal");
    CHECK_EQ(g_nSpans, 0, "no span for a lone backtick");
}

static void test_empty_italic_from_double_star(void)
{
    /* Faithful quirk of the original parser: an unmatched "**" is consumed
       as an EMPTY italic (bold fails, then the two stars match as *..*),
       contributing no visible text. Locked in so the refactor preserves it. */
    long n = strip("**hi", 0, 1);
    CHECK_STR(g_out, n, "hi", "unmatched ** becomes an empty italic");
    CHECK_EQ(g_nSpans, 1, "one (empty) span");
    CHECK_EQ(g_spans[0].kind, MD_KIND_ITALIC, "quirk span is italic");
    CHECK_EQ(g_spans[0].start, 0, "empty italic at 0");
    CHECK_EQ(g_spans[0].end, 0, "empty italic zero-length");
}

/* strip then emit must recover the original inline markdown. Uses the real
   pure MdSpansToRuns (the same span->run flattening the Mac adapter runs) to
   turn strip's overlapping spans into the runs MdEmitInline consumes, so the
   round-trip exercises production coalescing, not a test-only copy of it. */
static void roundtrip(const char *s, const char *msg)
{
    long stripped = strip(s, MD_HEADINGS_OFF, 1);
    short nRuns = MdSpansToRuns(stripped, g_spans, g_nSpans, g_runs, MD_MAX_RUNS);
    long emitted = MdEmitInline(g_out, stripped, g_runs, nRuns, &g_links,
                                g_out2, (long) sizeof g_out2);
    CHECK_STR(g_out2, emitted, s, msg);
}

/* Regression guard: many DISJOINT inline spans flatten to ~2x as many runs
   (styled word, plain gap, styled word...). With the run buffer sized only to
   MD_MAX_SPANS this folded the document's tail into one wrong style; sized to
   MD_MAX_RUNS it must style every span and round-trip exactly. 300 italic
   words -> 300 spans -> ~600 runs, comfortably past the 512 span cap. */
static void test_many_spans_no_truncation(void)
{
    static char src[4096];
    long n = 0;
    int i;
    short nRuns;
    long emitted;

    for (i = 0; i < 300; i++) {
        src[n++] = '*'; src[n++] = 'a'; src[n++] = '*'; src[n++] = ' ';
    }
    src[n] = '\0';

    {
        MdStripOpts opts;
        opts.headingMode = MD_HEADINGS_OFF;
        opts.startsAtLineStart = 1;
        (void) MdStrip(src, n, &opts, g_out, (long) sizeof g_out,
                       g_spans, MD_MAX_SPANS, &g_nSpans, &g_links);
    }
    CHECK_EQ(g_nSpans, 300, "300 disjoint italic spans recorded");

    nRuns = MdSpansToRuns((long) (600 /* "a " x300 */), g_spans, g_nSpans,
                          g_runs, MD_MAX_RUNS);
    CHECK(nRuns > MD_MAX_SPANS, "flattens to more runs than the span cap");
    emitted = MdEmitInline(g_out, 600, g_runs, nRuns, &g_links,
                           g_out2, (long) sizeof g_out2);
    CHECK_STR(g_out2, emitted, src, "300-span document round-trips untruncated");
}

static void test_emit_roundtrip(void)
{
    roundtrip("plain text", "plain round-trips");
    roundtrip("**bold**", "bold round-trips");
    roundtrip("*italic*", "italic round-trips");
    roundtrip("`code`", "code round-trips");
    roundtrip("a **b** c", "bold in context round-trips");
    roundtrip("**bold** and *italic* and `code`", "several inline round-trip");
    roundtrip("see [text](http://x) end", "link round-trips");
    roundtrip("[a](u1) and [b](u2)", "two links round-trip");
    roundtrip("~~struck~~", "strike round-trips");
    roundtrip("a ~~b~~ c", "strike in context round-trips");
    roundtrip("==marked==", "highlight round-trips");
    roundtrip("a ==b== c", "highlight in context round-trips");
    roundtrip("~~struck~~ and **bold** and *it*", "strike beside bold/italic round-trips");
}

/* Nested inline styles: the content of a delimiter pair is itself styled.
   Each string is the CANONICAL serialization MdEmitInline produces for the
   combined run (open order link>strike>bold>italic>code), so strip->emit
   recovers it byte-for-byte -- the exact Writer<->Markdown mode-switch path
   that used to corrupt combined styles (bug: ~~**x**~~ stripped to literal
   "**x**", strike-only). */
static void test_nested_roundtrip(void)
{
    roundtrip("~~**bold**~~", "strike wrapping bold round-trips");
    roundtrip("~~*it*~~", "strike wrapping italic round-trips");
    roundtrip("~~`c`~~", "strike wrapping code round-trips");
    roundtrip("***bi***", "bold+italic (***) round-trips");
    roundtrip("**`c`**", "bold wrapping code round-trips");
    roundtrip("a ***x*** b", "bold+italic in context round-trips");
    roundtrip("[**x**](u)", "bold link round-trips");
    roundtrip("[~~x~~](u)", "struck link round-trips");
    roundtrip("[***x***](u)", "bold+italic link round-trips");
    roundtrip("~~***x***~~", "strike+bold+italic round-trips");
    roundtrip("[~~**x**~~](u)", "link+strike+bold round-trips");
    roundtrip("see [~~**t**~~](u) ok", "everything nested in context round-trips");
    roundtrip("==**x**==", "highlight wrapping bold round-trips");
    roundtrip("==~~x~~==", "highlight wrapping strike round-trips");
    roundtrip("[==**x**==](u)", "link+highlight+bold round-trips");
}

/* Flatten a fully-uniform strip result to its single combined run and return
   it, asserting the whole string collapsed to exactly one run. */
static MdRun only_run(const char *src, const char *msg)
{
    long n = strip(src, MD_HEADINGS_OFF, 1);
    short nRuns = MdSpansToRuns(n, g_spans, g_nSpans, g_runs, MD_MAX_SPANS);
    CHECK_EQ(nRuns, 1, msg);
    return g_runs[0];
}

/* The nested spans strip records must OR into the right combined attributes. */
static void test_nested_strip_attributes(void)
{
    MdRun r;

    r = only_run("~~**x**~~", "strike+bold collapses to one run");
    CHECK(r.strike && r.bold && !r.italic && !r.code && !r.link,
          "~~**x**~~ is bold AND strike");

    r = only_run("***x***", "bold+italic collapses to one run");
    CHECK(r.bold && r.italic && !r.strike && !r.code, "***x*** is bold AND italic");

    r = only_run("[**x**](u)", "bold link collapses to one run");
    CHECK(r.link && r.bold && r.linkID == 1, "[**x**](u) is link AND bold, id 1");
    CHECK_STR(&g_links.url[1][1], g_links.url[1][0], "u", "nested link url captured");

    r = only_run("~~***x***~~", "triple collapses to one run");
    CHECK(r.bold && r.italic && r.strike, "~~***x***~~ is bold+italic+strike");

    r = only_run("==**x**==", "highlight+bold collapses to one run");
    CHECK(r.highlight && r.bold && !r.strike && !r.italic && !r.code && !r.link,
          "==**x**== is bold AND highlight");

    r = only_run("[==~~x~~==](u)", "link+highlight+strike collapses to one run");
    CHECK(r.link && r.highlight && r.strike && r.linkID == 1,
          "[==~~x~~==](u) is link AND highlight AND strike, id 1");
}

/* MdSpansToRuns in isolation: overlap ORs, disjoint spans split, equal
   neighbours coalesce, HEADING spans are ignored, and linkID rides along. */
static void test_spans_to_runs(void)
{
    MdSpan sp[3];
    MdRun rn[8];
    short n;

    /* Two overlapping spans over "abcd": bold[0,3), italic[1,4) -> three runs
       b | b+i | i. */
    sp[0].start = 0; sp[0].end = 3; sp[0].kind = MD_KIND_BOLD;   sp[0].level = 0; sp[0].linkID = 0;
    sp[1].start = 1; sp[1].end = 4; sp[1].kind = MD_KIND_ITALIC; sp[1].level = 0; sp[1].linkID = 0;
    n = MdSpansToRuns(4, sp, 2, rn, 8);
    CHECK_EQ(n, 3, "overlap of bold[0,3) and italic[1,4) makes 3 runs");
    CHECK(rn[0].bold && !rn[0].italic && rn[0].start == 0 && rn[0].end == 1, "run 0 bold only [0,1)");
    CHECK(rn[1].bold && rn[1].italic && rn[1].start == 1 && rn[1].end == 3, "run 1 bold+italic [1,3)");
    CHECK(!rn[2].bold && rn[2].italic && rn[2].start == 3 && rn[2].end == 4, "run 2 italic only [3,4)");

    /* A HEADING span contributes no inline attribute: plain single run. */
    sp[0].start = 0; sp[0].end = 5; sp[0].kind = MD_KIND_HEADING; sp[0].level = 2; sp[0].linkID = 0;
    n = MdSpansToRuns(5, sp, 1, rn, 8);
    CHECK_EQ(n, 1, "a heading-only span makes one plain run");
    CHECK(!rn[0].bold && !rn[0].italic && !rn[0].link, "heading span adds no inline attribute");

    /* linkID rides along and coalesces. */
    sp[0].start = 0; sp[0].end = 2; sp[0].kind = MD_KIND_LINK; sp[0].level = 0; sp[0].linkID = 7;
    n = MdSpansToRuns(2, sp, 1, rn, 8);
    CHECK_EQ(n, 1, "uniform link is one run");
    CHECK(rn[0].link && rn[0].linkID == 7, "link run carries its id");
}

/* The pure style-field codec: every one of the 64 attribute combinations must
   survive MdRunToFields -> MdFieldsToRun unchanged, and the link (red), strike
   (green) and highlight (blue) channels must stay independent. This is the
   combined-write invariant the Mac adapter (ApplySpanStyles/BuildStyleRuns)
   rides on -- the exact class of the bug where a second style clobbered the
   first. */
static void test_style_fields_roundtrip(void)
{
    int combo;

    for (combo = 0; combo < 64; combo++) {
        MdRun in, out;
        MdStyleFields f;

        in.start = 11; in.end = 22;           /* codec must not touch these */
        in.bold      = (combo & 1)  ? 1 : 0;
        in.italic    = (combo & 2)  ? 1 : 0;
        in.code      = (combo & 4)  ? 1 : 0;
        in.link      = (combo & 8)  ? 1 : 0;
        in.strike    = (combo & 16) ? 1 : 0;
        in.highlight = (combo & 32) ? 1 : 0;
        in.linkID = in.link ? 7 : 0;          /* a link always has a real id */

        f = MdRunToFields(&in);

        out.start = out.end = -1;
        MdFieldsToRun(&f, &out);

        CHECK_EQ(out.bold, in.bold, "codec preserves bold");
        CHECK_EQ(out.italic, in.italic, "codec preserves italic");
        CHECK_EQ(out.code, in.code, "codec preserves code");
        CHECK_EQ(out.link, in.link, "codec preserves link");
        CHECK_EQ(out.strike, in.strike, "codec preserves strike");
        CHECK_EQ(out.highlight, in.highlight, "codec preserves highlight");
        CHECK_EQ(out.linkID, in.linkID, "codec preserves link id");
    }
}

/* The heading level<->size mapping the Writer view rides on: a level 1..3
   heading is baseSize + (4 - level) * 4, and the inverse recovers the level
   exactly while rejecting every non-heading size -- the round-trip that keeps
   the Writer<->Markdown switch from losing or inventing heading levels. */
static void test_heading_size_level_roundtrip(void)
{
    short base, level;

    /* Round-trips across a spread of body (zoom) sizes: each heading size maps
       back to the level that produced it, and is always larger than the body. */
    for (base = 9; base <= 36; base++) {
        for (level = 1; level <= 3; level++) {
            short size = MdHeadingSizeForLevel(base, level);
            CHECK(size > base, "a heading is larger than the body size");
            CHECK_EQ(MdHeadingLevelForSize(base, size), level,
                     "size recovers its heading level");
        }
    }

    /* Concrete steps at an 18pt body: 4pt per level, level 1 the largest. */
    CHECK_EQ(MdHeadingSizeForLevel(18, 1), 30, "H1 at 18pt body is 30pt");
    CHECK_EQ(MdHeadingSizeForLevel(18, 2), 26, "H2 at 18pt body is 26pt");
    CHECK_EQ(MdHeadingSizeForLevel(18, 3), 22, "H3 at 18pt body is 22pt");

    /* The body size itself is not a heading, nor is an in-between or off-by-one
       size -- the inverse must return 0 so plain text never reads as a heading. */
    CHECK_EQ(MdHeadingLevelForSize(18, 18), 0, "body size is not a heading");
    CHECK_EQ(MdHeadingLevelForSize(18, 24), 0, "a non-step size is not a heading");
    CHECK_EQ(MdHeadingLevelForSize(18, 31), 0, "an off-by-one size is not a heading");
}

static void test_style_fields_channels_independent(void)
{
    MdRun in;
    MdStyleFields f;

    /* A struck, highlighted link must keep ALL THREE channels: red carries the
       id, green the strike, blue the highlight, and the underline face bit rides
       along -- none clobbers another. */
    in.start = 0; in.end = 1;
    in.bold = 0; in.italic = 0; in.code = 0; in.link = 1; in.strike = 1;
    in.highlight = 1;
    in.linkID = 42;
    f = MdRunToFields(&in);
    CHECK_EQ(f.linkID, 42, "link id lands in the red channel");
    CHECK_EQ(f.strike, 1, "strike lands in the green channel");
    CHECK_EQ(f.highlight, 1, "highlight lands in the blue channel");
    CHECK(f.face & MD_FACE_UNDERLINE, "link sets the underline face bit");

    /* A non-link run must carry no id, even if some caller left junk around. */
    in.link = 0; in.linkID = 99;
    f = MdRunToFields(&in);
    CHECK_EQ(f.linkID, 0, "a non-link run carries link id 0");
}

/* MdSpansToRuns must never drop a character when its run buffer overflows:
   past cap it folds the tail into the last run (production's "unreachable"
   safety net), and a degenerate cap==0 must return 0 without writing. */
static void test_spans_to_runs_overflow(void)
{
    MdSpan sp[4];
    MdRun rn[8];
    short i, n;

    /* Four disjoint bold words over "a b c d" (positions 0,2,4,6) flatten to
       7 runs (word,gap,word,...); with cap 3 the tail folds into run 2. */
    for (i = 0; i < 4; i++) {
        sp[i].start = i * 2; sp[i].end = i * 2 + 1;
        sp[i].kind = MD_KIND_BOLD; sp[i].level = 0; sp[i].linkID = 0;
    }
    n = MdSpansToRuns(7, sp, 4, rn, 3);
    CHECK_EQ(n, 3, "overflow caps the run count at 3");
    CHECK_EQ(rn[0].start, 0, "first run starts at 0");
    CHECK_EQ(rn[n - 1].end, 7, "last run folds the tail to cover every char");

    /* Degenerate cap: no room for even one run -> 0, and (crucially) no write
       to rn[-1]. We can only assert the count here; the guard is what matters. */
    n = MdSpansToRuns(5, sp, 1, rn, 0);
    CHECK_EQ(n, 0, "cap 0 records no runs and does not underflow");
}

/* MdStrip must strip the text in full even when it runs out of span slots:
   runs past spanCap are still removed, just not recorded (the documented
   MAX_STYLE_OPS behaviour). */
static void test_strip_span_cap_overflow(void)
{
    MdStripOpts opts;
    MdSpan few[2];
    short nSpans;
    long n;

    opts.headingMode = MD_HEADINGS_OFF;
    opts.startsAtLineStart = 1;
    /* Four italic words but room recorded for only two spans. */
    n = MdStrip("*a* *b* *c* *d*", 15, &opts, g_out, (long) sizeof g_out,
                few, 2, &nSpans, &g_links);
    CHECK_STR(g_out, n, "a b c d", "all delimiters stripped despite the span cap");
    CHECK_EQ(nSpans, 2, "only spanCap spans are recorded");
}

/* MdEmitInline must never write past outCap. Emit a heavily-styled range into
   a deliberately tiny window inside a sentinel-filled buffer and prove the
   bytes beyond the cap are untouched and the return value is bounded. */
static void test_emit_respects_outcap(void)
{
    char buf[64];
    MdRun runs[6];
    long i, ret;
    int clean = 1;

    for (i = 0; i < (long) sizeof buf; i++)
        buf[i] = (char) 0xAA;

    /* Six single-char bold runs emit "**a****b**..." = 5 bytes each = 30,
       far past the 10-byte cap. */
    for (i = 0; i < 6; i++) {
        runs[i].start = i; runs[i].end = i + 1;
        runs[i].bold = 1; runs[i].italic = 0; runs[i].code = 0;
        runs[i].link = 0; runs[i].strike = 0; runs[i].highlight = 0;
        runs[i].linkID = 0;
    }
    ret = MdEmitInline("abcdef", 6, runs, 6, (MdLinkTable *) 0, buf, 10);

    CHECK(ret <= 10, "emit never returns more than outCap");
    for (i = 10; i < (long) sizeof buf; i++)
        if (buf[i] != (char) 0xAA)
            clean = 0;
    CHECK(clean, "emit writes nothing past outCap (no heap overrun)");
}

static void test_emit_combined_attributes(void)
{
    MdRun runs[1];
    long n;

    /* A run that is bold AND italic: open **, then *, close *, then **. */
    runs[0].start = 0; runs[0].end = 2;
    runs[0].bold = 1; runs[0].italic = 1; runs[0].code = 0; runs[0].link = 0;
    runs[0].strike = 0; runs[0].highlight = 0; runs[0].linkID = 0;
    n = MdEmitInline("hi", 2, runs, 1, (MdLinkTable *) 0, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "***hi***", "bold+italic nests as ***");

    /* A bold link: open [ then **, close **, then ](url). */
    g_links.count = 1;
    g_links.url[1][0] = 1;
    g_links.url[1][1] = 'u';
    runs[0].start = 0; runs[0].end = 1;
    runs[0].bold = 1; runs[0].italic = 0; runs[0].code = 0; runs[0].link = 1;
    runs[0].strike = 0; runs[0].highlight = 0; runs[0].linkID = 1;
    n = MdEmitInline("x", 1, runs, 1, &g_links, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "[**x**](u)", "bold link wraps correctly");

    /* Strike + bold: strike is the outer of the two, so ~~ wraps **. */
    runs[0].start = 0; runs[0].end = 2;
    runs[0].bold = 1; runs[0].italic = 0; runs[0].code = 0; runs[0].link = 0;
    runs[0].strike = 1; runs[0].highlight = 0; runs[0].linkID = 0;
    n = MdEmitInline("hi", 2, runs, 1, (MdLinkTable *) 0, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "~~**hi**~~", "strike wraps bold");

    /* Highlight + bold: highlight is the outer of the two, so == wraps **. */
    runs[0].start = 0; runs[0].end = 2;
    runs[0].bold = 1; runs[0].italic = 0; runs[0].code = 0; runs[0].link = 0;
    runs[0].strike = 0; runs[0].highlight = 1; runs[0].linkID = 0;
    n = MdEmitInline("hi", 2, runs, 1, (MdLinkTable *) 0, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "==**hi**==", "highlight wraps bold");
}

int main(void)
{
    printf("test_mdcore (strip):\n");
    test_plain();
    test_bold();
    test_italic_code();
    test_bold_beats_italic();
    test_strike();
    test_highlight();
    test_codeblock();
    test_link();
    test_link_without_table();
    test_heading();
    test_heading_disabled();
    test_heading_needs_line_start();
    test_interior_heading();
    test_heading_strip_mode();
    test_unmatched_delimiters();
    test_empty_italic_from_double_star();
    test_nested_strip_attributes();
    test_spans_to_runs();
    test_spans_to_runs_overflow();
    test_strip_span_cap_overflow();
    test_many_spans_no_truncation();
    printf("test_mdcore (style codec):\n");
    test_style_fields_roundtrip();
    test_heading_size_level_roundtrip();
    test_style_fields_channels_independent();
    printf("test_mdcore (emit):\n");
    test_emit_roundtrip();
    test_nested_roundtrip();
    test_emit_respects_outcap();
    test_emit_combined_attributes();
    return TEST_RESULT();
}

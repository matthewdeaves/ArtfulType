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

static void test_emit_combined_attributes(void)
{
    MdRun runs[1];
    long n;

    /* A run that is bold AND italic: open **, then *, close *, then **. */
    runs[0].start = 0; runs[0].end = 2;
    runs[0].bold = 1; runs[0].italic = 1; runs[0].code = 0; runs[0].link = 0;
    runs[0].strike = 0; runs[0].linkID = 0;
    n = MdEmitInline("hi", 2, runs, 1, (MdLinkTable *) 0, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "***hi***", "bold+italic nests as ***");

    /* A bold link: open [ then **, close **, then ](url). */
    g_links.count = 1;
    g_links.url[1][0] = 1;
    g_links.url[1][1] = 'u';
    runs[0].start = 0; runs[0].end = 1;
    runs[0].bold = 1; runs[0].italic = 0; runs[0].code = 0; runs[0].link = 1;
    runs[0].strike = 0; runs[0].linkID = 1;
    n = MdEmitInline("x", 1, runs, 1, &g_links, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "[**x**](u)", "bold link wraps correctly");

    /* Strike + bold: strike is the outer of the two, so ~~ wraps **. */
    runs[0].start = 0; runs[0].end = 2;
    runs[0].bold = 1; runs[0].italic = 0; runs[0].code = 0; runs[0].link = 0;
    runs[0].strike = 1; runs[0].linkID = 0;
    n = MdEmitInline("hi", 2, runs, 1, (MdLinkTable *) 0, g_out2, sizeof g_out2);
    CHECK_STR(g_out2, n, "~~**hi**~~", "strike wraps bold");
}

int main(void)
{
    printf("test_mdcore (strip):\n");
    test_plain();
    test_bold();
    test_italic_code();
    test_bold_beats_italic();
    test_strike();
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
    test_many_spans_no_truncation();
    printf("test_mdcore (emit):\n");
    test_emit_roundtrip();
    test_nested_roundtrip();
    test_emit_combined_attributes();
    return TEST_RESULT();
}

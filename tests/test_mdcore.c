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

static char g_out[4096];
static MdSpan g_spans[MD_MAX_SPANS];
static short g_nSpans;
static MdLinkTable g_links;

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

int main(void)
{
    printf("test_mdcore (strip):\n");
    test_plain();
    test_bold();
    test_italic_code();
    test_bold_beats_italic();
    test_link();
    test_link_without_table();
    test_heading();
    test_heading_disabled();
    test_heading_needs_line_start();
    test_interior_heading();
    test_heading_strip_mode();
    test_unmatched_delimiters();
    test_empty_italic_from_double_star();
    return TEST_RESULT();
}

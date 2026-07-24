/*
 * test_roundtrip.c -- Property tests for the Writer<->Markdown round trip.
 *
 * ArtfulType is a bidirectional transform: MdStrip turns canonical Markdown
 * into the Writer view (stripped text + styled spans), and MdEmit turns the
 * Writer view back into canonical Markdown. The two must compose to the
 * identity (up to the documented normalisations). Every earlier fence/heading
 * regression in this codebase was a broken round trip; the heap overflow the
 * architecture review found lived in the emit half precisely because the emit
 * half had no host coverage. This file closes that gap by driving the WHOLE
 * pipeline purely:
 *
 *     src --MdStrip--> (stripped, spans) --MdSpansToDocRuns--> runs
 *         --MdEmit--> canonical, asserted == the normalised src.
 *
 * MdSpansToDocRuns is the pure stand-in for the Mac adapter's BuildDocRuns
 * (which reads the same run model out of TextEdit); MdEmit -- the block-level
 * fence/heading/bounds logic that used to live untested in the adapter -- is
 * the code actually under test.
 *
 * Line endings are '\r' (0x0D), as everywhere in ArtfulType.
 *
 * Documented normalisations (NOT losses -- identical rendering):
 *   - a one-line ``` fence collapses to inline `code`;
 *   - a fence info string (```python) is dropped;
 *   - inline styles inside a heading are left literal (never re-derived).
 */
#include <string.h>
#include "test_util.h"
#include "mdcore.h"

static char       g_out[8192];
static MdSpan     g_spans[MD_MAX_SPANS];
static short      g_nSpans;
static MdLinkTable g_links;
static MdRun      g_docruns[MD_MAX_RUNS];
static char       g_emit[8192];

/* Full strip -> doc-runs -> emit. Returns the emitted length; result in g_emit.
   Uses the Writer-view heading mode (MD_HEADINGS_SPAN), starting at a line
   start, exactly as BuildHiddenView drives MdStrip on the Mac. */
static long roundtrip(const char *src)
{
    MdStripOpts opts;
    long slen;
    short nRuns;

    opts.headingMode = MD_HEADINGS_SPAN;
    opts.startsAtLineStart = 1;
    slen = MdStrip(src, (long) strlen(src), &opts,
                   g_out, (long) sizeof g_out,
                   g_spans, MD_MAX_SPANS, &g_nSpans, &g_links);
    nRuns = MdSpansToDocRuns(slen, g_spans, g_nSpans, g_docruns, MD_MAX_RUNS);
    return MdEmit(g_out, slen, g_docruns, nRuns, &g_links,
                  g_emit, (long) sizeof g_emit);
}

/* Assert that src round-trips to want. */
#define RT(src, want, msg) \
    do { long n_ = roundtrip(src); CHECK_STR(g_emit, n_, (want), (msg)); } while (0)

/* --- Plain text and paragraphs ------------------------------------------- */

static void test_plain(void)
{
    RT("hello world", "hello world", "plain line");
    RT("para1\r\rpara2", "para1\r\rpara2", "blank line between paragraphs");
    RT("a\rb\rc", "a\rb\rc", "three plain lines");
    RT("", "", "empty document");
    RT("trailing\r", "trailing\r", "document ending in a line break");
}

/* --- Inline styles -------------------------------------------------------- */

static void test_inline(void)
{
    RT("**bold**", "**bold**", "bold");
    RT("*italic*", "*italic*", "italic");
    RT("`code`", "`code`", "inline code");
    RT("~~strike~~", "~~strike~~", "strikethrough");
    RT("==mark==", "==mark==", "highlight");
    RT("[text](http://x)", "[text](http://x)", "link");
    RT("a **b** c *d* e", "a **b** c *d* e", "inline styles in context");
}

/* --- Nested / overlapping inline (the combined-attribute path) ------------ */

static void test_nested(void)
{
    RT("~~**x**~~", "~~**x**~~", "strike wrapping bold");
    RT("***x***", "***x***", "bold + italic");
    RT("[**x**](u)", "[**x**](u)", "link wrapping bold");
    RT("==~~x~~==", "==~~x~~==", "highlight wrapping strike");
    RT("a [**b**](u) c", "a [**b**](u) c", "nested link in context");
}

/* --- Headings ------------------------------------------------------------- */

static void test_headings(void)
{
    RT("# Title", "# Title", "h1");
    RT("## Sub", "## Sub", "h2");
    RT("### Deep", "### Deep", "h3");
    RT("# Title\rbody text", "# Title\rbody text", "heading then paragraph");
    RT("intro\r## Middle\rmore", "intro\r## Middle\rmore", "heading between lines");
    /* Inline styles inside a heading are intentionally left literal (MdStrip
       does not descend into a heading body), so they round-trip verbatim. */
    RT("# a **b** c", "# a **b** c", "heading keeps inline markers literal");
}

/* --- Fenced code blocks (the structural fence reconstruction) ------------- */

static void test_fences(void)
{
    RT("```\rline1\rline2\r```",
       "```\rline1\rline2\r```", "multi-line fence round-trips");
    RT("before\r```\rcode\rhere\r```\rafter",
       "before\r```\rcode\rhere\r```\rafter", "fence between paragraphs");
    /* A fence whose body is a single blank line strips to a lone Monaco '\r'
       (MdStrip drops the '\r' that precedes the closing fence, so TWO body
       lines are needed to leave one behind). That 1-char body re-fences to 9
       output chars -- the ~5x blow-up the emit buffer sizing is proven against;
       the emit path used to be able to overrun the heap here. */
    RT("```\r\r\r```", "```\r\r\r```", "fence body of one blank line (lone '\\r')");
    RT("```\ra\r\rb\r```", "```\ra\r\rb\r```", "blank line inside fence body");
    /* Two adjacent inline-code lines stay SEPARATE: the '\r' between them is
       plain, not Monaco, so it bounds each code run -- not one multi-line fence. */
    RT("`a`\r`b`", "`a`\r`b`", "adjacent inline code stays two spans");
}

/* --- Documented normalisations (rendering-identical, not losses) ---------- */

static void test_normalisations(void)
{
    /* A one-line ``` block has no internal '\r', so it is inline `code`. */
    RT("```\rhello\r```", "`hello`", "one-line fence normalises to inline code");
    /* A fence info string is dropped (only ``` is kept). */
    RT("```python\rx = 1\ry = 2\r```",
       "```\rx = 1\ry = 2\r```", "fence info string dropped");
}

/* --- A larger mixed document --------------------------------------------- */

static void test_mixed(void)
{
    const char *doc =
        "# ArtfulType\r"
        "\r"
        "A **distraction-free** editor for `classic` Macs.\r"
        "\r"
        "## Features\r"
        "\r"
        "- see the ~~old~~ new list\r"
        "\r"
        "```\r"
        "int main(void) {\r"
        "    return 0;\r"
        "}\r"
        "```\r"
        "\r"
        "Read [more](http://x) about it.";
    /* Everything here is already in canonical form, so the round trip is the
       identity. (The '- ' bullet is a draw-time overpaint, never a span, so it
       is plain text to strip/emit and passes through untouched.) */
    RT(doc, doc, "mixed document round-trips");
}

/* --- Idempotence: one round trip reaches a fixed point ------------------- */

/* Whatever a document normalises to on the first strip->emit, a SECOND round
   trip must leave unchanged. If it didn't, the canonical form the app saves
   would keep drifting on every save/mode-switch -- the exact failure mode the
   fenced-code work risked. Checked for inputs that DO normalise (so the second
   pass is the interesting one) as well as already-canonical ones. */
static char g_first[8192];

static void checkIdempotent(const char *src, const char *msg)
{
    long n1, n2;

    n1 = roundtrip(src);
    memcpy(g_first, g_emit, (size_t) n1);
    g_first[n1] = '\0';        /* roundtrip() takes a C string */
    n2 = roundtrip(g_first);   /* g_first is a valid canonical document */
    CHECK_STR(g_emit, n2, g_first, msg);
}

static void test_idempotent(void)
{
    checkIdempotent("```\rhello\r```", "inline-normalised fence is a fixed point");
    checkIdempotent("```python\rx\ry\r```", "info-stripped fence is a fixed point");
    checkIdempotent("# a **b** c\r`x`\r~~y~~", "mixed normalised doc is stable");
    checkIdempotent("plain\r\rtext", "plain doc is a fixed point");
}

int main(void)
{
    printf("test_roundtrip:\n");
    test_plain();
    test_inline();
    test_nested();
    test_headings();
    test_fences();
    test_normalisations();
    test_mixed();
    test_idempotent();
    return TEST_RESULT();
}

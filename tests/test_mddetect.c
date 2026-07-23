/*
 * test_mddetect.c -- Golden tests for the live-typing inline detector.
 *
 * MdDetectInline is the pure heart of Writer mode's "type the markdown, get
 * the formatting" behaviour: given the buffer just after a keystroke, it
 * decides whether that key completed **bold**, *italic*, `code`, a
 * [text](url) link, or a "# " heading, and returns the exact edit plan the
 * Mac adapter applies. Each case pins the whole plan (both deletions, the
 * style range, the parked caret) so a regression in the position arithmetic
 * fails loudly. Buffers are written exactly as they stand right after the
 * character was inserted, with caret == the offset just past it.
 */
#include <string.h>
#include "test_util.h"
#include "mdcore.h"

/* Run the detector over a C string with the caret just past the typed char. */
static MdInlineEdit det(const char *buf, long caret, char justTyped)
{
    return MdDetectInline(buf, (long) strlen(buf), caret, justTyped);
}

/* Assert an edit's two deletions, style range and parked caret in one shot. */
static void check_plan(MdInlineEdit e, short kind,
                       long d1s, long d1e, long d2s, long d2e,
                       long ss, long se, long caret, int reset,
                       const char *what)
{
    CHECK_EQ(e.kind, kind, what);
    CHECK_EQ(e.del1Start, d1s, what);
    CHECK_EQ(e.del1End, d1e, what);
    CHECK_EQ(e.del2Start, d2s, what);
    CHECK_EQ(e.del2End, d2e, what);
    CHECK_EQ(e.styleStart, ss, what);
    CHECK_EQ(e.styleEnd, se, what);
    CHECK_EQ(e.newCaret, caret, what);
    CHECK_EQ(e.resetNormal, reset, what);
}

static void test_bold_closing_completes(void)
{
    /* "**hi**" + the final '*': the just-typed ** closes an opening ** two
       chars back. Delete both pairs, bold the "hi" between them. */
    MdInlineEdit e = det("**hi**", 6, '*');
    check_plan(e, MD_KIND_BOLD, 4, 6, 0, 2, 0, 2, 2, 1,
               "bold: closing ** completes the pair");
}

static void test_bold_opening_completes(void)
{
    /* "AB**CD**" + the '*' at index 3: the just-typed ** (2..3) has no
       opening behind it, so it becomes the OPENING for the closing ** at
       6..7 -- bold the "CD" that lies between them. */
    MdInlineEdit e = det("AB**CD**", 4, '*');
    check_plan(e, MD_KIND_BOLD, 6, 8, 2, 4, 2, 4, 2, 1,
               "bold: opening ** reaches a later closing **");
}

static void test_italic_closing_completes(void)
{
    MdInlineEdit e = det("*i*", 3, '*');
    check_plan(e, MD_KIND_ITALIC, 2, 3, 0, 1, 0, 1, 1, 1,
               "italic: closing * completes the pair");
}

static void test_italic_opening_completes(void)
{
    /* "AB*C*" + the '*' at index 2: no opening behind, so it opens toward
       the closing '*' at index 4. */
    MdInlineEdit e = det("AB*C*", 3, '*');
    check_plan(e, MD_KIND_ITALIC, 4, 5, 2, 3, 2, 3, 2, 1,
               "italic: opening * reaches a later closing *");
}

static void test_code_closing_completes(void)
{
    MdInlineEdit e = det("`c`", 3, '`');
    check_plan(e, MD_KIND_CODE, 2, 3, 0, 1, 0, 1, 1, 1,
               "code: closing ` completes the pair");
}

static void test_code_opening_completes(void)
{
    MdInlineEdit e = det("AB`C`", 3, '`');
    check_plan(e, MD_KIND_CODE, 4, 5, 2, 3, 2, 3, 2, 1,
               "code: opening ` reaches a later closing `");
}

static void test_strike_closing_completes(void)
{
    /* "~~gone~~" + the final '~': the just-typed ~~ closes an opening ~~ at
       the line start. Delete both pairs, strike the "gone" between them. */
    MdInlineEdit e = det("~~gone~~", 8, '~');
    check_plan(e, MD_KIND_STRIKE, 6, 8, 0, 2, 0, 4, 4, 1,
               "strike: closing ~~ completes the pair");
}

static void test_strike_opening_completes(void)
{
    /* "AB~~CD~~" + the '~' at index 3: the just-typed ~~ (2..3) has no
       opening behind it, so it becomes the OPENING for the closing ~~ at
       6..7 -- strike the "CD" that lies between them. */
    MdInlineEdit e = det("AB~~CD~~", 4, '~');
    check_plan(e, MD_KIND_STRIKE, 6, 8, 2, 4, 2, 4, 2, 1,
               "strike: opening ~~ reaches a later closing ~~");
}

static void test_highlight_closing_completes(void)
{
    /* "==mark==" + the final '=': the just-typed == closes an opening == at
       the line start. Delete both pairs, highlight the "mark" between them. */
    MdInlineEdit e = det("==mark==", 8, '=');
    check_plan(e, MD_KIND_HIGHLIGHT, 6, 8, 0, 2, 0, 4, 4, 1,
               "highlight: closing == completes the pair");
}

static void test_highlight_opening_completes(void)
{
    /* "AB==CD==" + the '=' at index 3: the just-typed == (2..3) has no
       opening behind it, so it becomes the OPENING for the closing == at
       6..7 -- highlight the "CD" that lies between them. */
    MdInlineEdit e = det("AB==CD==", 4, '=');
    check_plan(e, MD_KIND_HIGHLIGHT, 6, 8, 2, 4, 2, 4, 2, 1,
               "highlight: opening == reaches a later closing ==");
}

static void test_link_completes(void)
{
    /* "[t](u)" + the closing ')': strip the "](u)" tail and the leading
       "[", underline the "t", and hand back the URL "u". */
    MdInlineEdit e = det("[t](u)", 6, ')');
    check_plan(e, MD_KIND_LINK, 2, 6, 0, 1, 0, 1, 1, 1,
               "link: ) completes [text](url)");
    CHECK_EQ(e.linkURL[0], 1, "link URL length is 1");
    CHECK_EQ(e.linkURL[1], 'u', "link URL byte is 'u'");
}

static void test_link_longer_url(void)
{
    MdInlineEdit e = det("[Ars](http://a)", 15, ')');
    /* closeBracket at 4, openBracket at 0, url = "http://a" (8 bytes). */
    check_plan(e, MD_KIND_LINK, 4, 15, 0, 1, 0, 3, 3, 1,
               "link: multi-char text and URL");
    CHECK_EQ(e.linkURL[0], 8, "URL length is 8");
    CHECK(memcmp(e.linkURL + 1, "http://a", 8) == 0, "URL bytes captured");
}

static void test_heading_levels(void)
{
    MdInlineEdit e1 = det("# ", 2, ' ');
    check_plan(e1, MD_KIND_HEADING, 0, 2, 0, 0, 0, 0, 0, 0,
               "h1: '# ' becomes a level-1 heading");
    CHECK_EQ(e1.level, 1, "h1 level");

    {
        MdInlineEdit e2 = det("## ", 3, ' ');
        check_plan(e2, MD_KIND_HEADING, 0, 3, 0, 0, 0, 0, 0, 0,
                   "h2: '## ' becomes a level-2 heading");
        CHECK_EQ(e2.level, 2, "h2 level");
    }
    {
        MdInlineEdit e3 = det("### ", 4, ' ');
        check_plan(e3, MD_KIND_HEADING, 0, 4, 0, 0, 0, 0, 0, 0,
                   "h3: '### ' becomes a level-3 heading");
        CHECK_EQ(e3.level, 3, "h3 level");
    }
}

static void test_heading_interior_line(void)
{
    /* Heading detection keys off the line, not the buffer start: "a\r## "
       with the caret past the space is still a level-2 heading on line 2,
       deleting only that line's "## " prefix. */
    MdInlineEdit e = det("a\r## ", 5, ' ');
    check_plan(e, MD_KIND_HEADING, 2, 5, 0, 0, 2, 2, 2, 0,
               "heading detected on an interior line");
    CHECK_EQ(e.level, 2, "interior heading level");
}

static void test_four_hashes_not_heading(void)
{
    /* Only 1..3 '#' make a heading; a fourth means it is body text. */
    MdInlineEdit e = det("#### ", 5, ' ');
    CHECK_EQ(e.kind, MD_INLINE_NONE, "four hashes is not a heading");
}

static void test_no_match_cases(void)
{
    CHECK_EQ(det("hello", 5, 'o').kind, MD_INLINE_NONE,
             "an ordinary letter never matches");
    CHECK_EQ(det("**", 2, '*').kind, MD_INLINE_NONE,
             "a bare ** with nothing before it does not match");
    CHECK_EQ(det("*x", 2, 'x').kind, MD_INLINE_NONE,
             "an unmatched * does not match");
    CHECK_EQ(det("x ", 2, ' ').kind, MD_INLINE_NONE,
             "a space that is not a heading prefix does not match");
    CHECK_EQ(det("[t]u)", 5, ')').kind, MD_INLINE_NONE,
             "a ) without a preceding ]( does not match");
    CHECK_EQ(det("a~", 2, '~').kind, MD_INLINE_NONE,
             "a single ~ does not match");
    CHECK_EQ(det("~~", 2, '~').kind, MD_INLINE_NONE,
             "a bare ~~ with nothing before it does not match");
}

static void test_carriage_return_not_handled(void)
{
    /* '\r' is deliberately the adapter's job (it just resets the typing
       style); the pure detector reports no edit for it. */
    CHECK_EQ(det("abc\r", 4, '\r').kind, MD_INLINE_NONE,
             "the detector leaves '\\r' to the adapter");
}

int main(void)
{
    printf("test_mddetect (live typing):\n");
    test_bold_closing_completes();
    test_bold_opening_completes();
    test_italic_closing_completes();
    test_italic_opening_completes();
    test_code_closing_completes();
    test_code_opening_completes();
    test_strike_closing_completes();
    test_strike_opening_completes();
    test_highlight_closing_completes();
    test_highlight_opening_completes();
    test_link_completes();
    test_link_longer_url();
    test_heading_levels();
    test_heading_interior_line();
    test_four_hashes_not_heading();
    test_no_match_cases();
    test_carriage_return_not_handled();
    return TEST_RESULT();
}

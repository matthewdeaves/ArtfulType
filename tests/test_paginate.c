/*
 * test_paginate.c -- Golden tests for MdPaginate, the pure page-break planner.
 *
 * Printing walks a document whose lines vary in height (headings are taller
 * than body text), so each page holds a variable number of lines. MdPaginate
 * turns a list of per-line heights plus a page height into the 0-based index
 * of the first line on each page. These cases pin the greedy break points, the
 * "never split a line" rule, the "a line taller than the page still gets its
 * own page" guarantee, and the maxPages clamp.
 */
#include "test_util.h"
#include "mdcore.h"

static short g_starts[64];

int main(void)
{
    printf("test_paginate:\n");

    /* Uniform 10pt lines on a 30pt page => exactly 3 lines per page. */
    {
        short h[7];
        int i, n;
        for (i = 0; i < 7; i++) h[i] = 10;
        n = MdPaginate(h, 7, 30, g_starts, 64);
        CHECK_EQ(n, 3, "7 uniform lines, 3 per page => 3 pages");
        CHECK_EQ(g_starts[0], 0, "page 1 starts at line 0");
        CHECK_EQ(g_starts[1], 3, "page 2 starts at line 3");
        CHECK_EQ(g_starts[2], 6, "page 3 starts at line 6");
    }

    /* Exact fit: three 10pt lines fill a 30pt page with no early break. */
    {
        short h[6];
        int i, n;
        for (i = 0; i < 6; i++) h[i] = 10;
        n = MdPaginate(h, 6, 30, g_starts, 64);
        CHECK_EQ(n, 2, "6 lines exactly fill 2 pages (== is not overflow)");
        CHECK_EQ(g_starts[1], 3, "second page starts at line 3");
    }

    /* A tall heading (25pt) can't share a 30pt page with a following 10pt
       line (25+10 > 30), so it sits alone; the body then packs 3 per page. */
    {
        short h[5];
        int n;
        h[0] = 25; h[1] = 10; h[2] = 10; h[3] = 10; h[4] = 10;
        n = MdPaginate(h, 5, 30, g_starts, 64);
        CHECK_EQ(n, 3, "heading forces a break: 3 pages");
        CHECK_EQ(g_starts[0], 0, "page 1 = the heading");
        CHECK_EQ(g_starts[1], 1, "page 2 begins at first body line");
        CHECK_EQ(g_starts[2], 4, "page 3 = the overflow line");
    }

    /* A single line taller than the whole page still consumes one page and
       progress continues -- no infinite loop. */
    {
        short h[3];
        int n;
        h[0] = 50; h[1] = 10; h[2] = 10;
        n = MdPaginate(h, 3, 30, g_starts, 64);
        CHECK_EQ(n, 2, "oversized line takes a page by itself");
        CHECK_EQ(g_starts[0], 0, "oversized line on page 1");
        CHECK_EQ(g_starts[1], 1, "page 2 resumes at line 1");
    }

    /* maxPages clamp: never writes past the cap. With one line per page and a
       cap of 3, only 3 starts are recorded; the caller draws the rest onto the
       last page. */
    {
        short h[10];
        int i, n;
        for (i = 0; i < 10; i++) h[i] = 10;
        n = MdPaginate(h, 10, 10, g_starts, 3);
        CHECK_EQ(n, 3, "clamped to maxPages");
        CHECK_EQ(g_starts[0], 0, "clamp page 1");
        CHECK_EQ(g_starts[1], 1, "clamp page 2");
        CHECK_EQ(g_starts[2], 2, "clamp page 3 (absorbs the tail)");
    }

    /* Degenerate inputs return 0 pages and never dereference lineHeights. */
    {
        CHECK_EQ(MdPaginate((short *) 0, 0, 30, g_starts, 64), 0,
                 "no lines => 0 pages");
        CHECK_EQ(MdPaginate(g_starts, 5, 30, g_starts, 0), 0,
                 "maxPages 0 => 0 pages");
    }

    /* A zero/negative page height degrades to one line per page rather than
       looping forever. */
    {
        short h[3];
        int n;
        h[0] = 10; h[1] = 10; h[2] = 10;
        n = MdPaginate(h, 3, 0, g_starts, 64);
        CHECK_EQ(n, 3, "pageHeight 0 => one line per page");
    }

    return TEST_RESULT();
}

/*
 * test_text.c -- host tests for the plain-text helpers added in ADR 0003:
 * MdNormalizeImport, MdFind, MdWordCount. Pure buffer functions, so they run
 * off the Mac exactly as they do on it.
 */
#include <string.h>
#include "mdcore.h"
#include "test_util.h"

/* Copy a byte string (which may contain NUL-free high bytes) into a writable
   buffer and normalize it in place; compare against expected bytes. */
static void check_norm(const char *in, long inLen,
                       const char *want, long wantLen, const char *msg)
{
    char buf[256];
    long out;
    memcpy(buf, in, (size_t) inLen);
    out = MdNormalizeImport(buf, inLen);
    g_checks++;
    if (out != wantLen || memcmp(buf, want, (size_t) wantLen) != 0) {
        g_fails++;
        printf("  FAIL: %s: got len %ld want %ld\n", msg, out, wantLen);
    }
}

static void test_normalize(void)
{
    printf("test_text (MdNormalizeImport):\n");

    /* Line endings: CRLF and lone LF both fold to CR. */
    check_norm("a\r\nb", 4, "a\rb", 3, "CRLF -> CR");
    check_norm("a\nb", 3, "a\rb", 3, "LF -> CR");
    check_norm("a\rb", 3, "a\rb", 3, "CR unchanged");

    /* UTF-8 BOM stripped. */
    check_norm("\xEF\xBB\xBF" "hi", 5, "hi", 2, "BOM stripped");

    /* Smart punctuation -> MacRoman. */
    check_norm("\xE2\x80\x99", 3, "\xD5", 1, "right single quote");
    check_norm("\xE2\x80\x9C" "x\xE2\x80\x9D", 7, "\xD2" "x\xD3", 3, "double quotes");
    check_norm("\xE2\x80\x94", 3, "\xD1", 1, "em dash");
    check_norm("\xE2\x80\xA6", 3, "\xC9", 1, "ellipsis");
    check_norm("\xE2\x80\xA2", 3, "\xA5", 1, "bullet");

    /* Accented letter: e-acute U+00E9 -> 0x8E. */
    check_norm("caf\xC3\xA9", 5, "caf\x8E", 4, "e-acute");

    /* Unknown code point -> '?'. U+2603 SNOWMAN (E2 98 83). */
    check_norm("\xE2\x98\x83", 3, "?", 1, "unknown -> ?");

    /* Already-MacRoman high byte with no valid continuation passes through:
       0xD5 (') followed by ASCII 's'. */
    check_norm("\xD5s", 2, "\xD5s", 2, "MacRoman passthrough");

    /* Plain ASCII untouched. */
    check_norm("hello world", 11, "hello world", 11, "ascii untouched");
}

static void test_find(void)
{
    const char *hay = "The quick Brown fox";
    long n = (long) strlen(hay);

    printf("test_text (MdFind):\n");

    CHECK_EQ(MdFind(hay, n, "quick", 5, 0, 1), 4, "find quick");
    CHECK_EQ(MdFind(hay, n, "QUICK", 5, 0, 1), -1, "case-sensitive miss");
    CHECK_EQ(MdFind(hay, n, "QUICK", 5, 0, 0), 4, "case-insensitive hit");
    CHECK_EQ(MdFind(hay, n, "brown", 5, 0, 0), 10, "ci brown");
    CHECK_EQ(MdFind(hay, n, "o", 1, 0, 1), 12, "first o");
    CHECK_EQ(MdFind(hay, n, "o", 1, 13, 1), 17, "next o from 13");
    CHECK_EQ(MdFind(hay, n, "xyz", 3, 0, 1), -1, "no match");
    CHECK_EQ(MdFind(hay, n, "The", 3, 0, 1), 0, "match at start");
    CHECK_EQ(MdFind(hay, n, "", 0, 0, 1), -1, "empty needle");
}

static void test_wordcount(void)
{
    printf("test_text (MdWordCount):\n");

    CHECK_EQ(MdWordCount("", 0), 0, "empty");
    CHECK_EQ(MdWordCount("   ", 3), 0, "spaces only");
    CHECK_EQ(MdWordCount("hello", 5), 1, "one word");
    CHECK_EQ(MdWordCount("hello world", 11), 2, "two words");
    CHECK_EQ(MdWordCount("  a  b  c  ", 11), 3, "padded");
    CHECK_EQ(MdWordCount("a\rb\nc\td", 7), 4, "mixed whitespace");
}

static void test_hrule(void)
{
    printf("test_text (MdIsHorizontalRule):\n");

    CHECK_EQ(MdIsHorizontalRule("---", 3), 1, "three dashes");
    CHECK_EQ(MdIsHorizontalRule("***", 3), 1, "three stars");
    CHECK_EQ(MdIsHorizontalRule("___", 3), 1, "three underscores");
    CHECK_EQ(MdIsHorizontalRule("----------", 10), 1, "many dashes");
    CHECK_EQ(MdIsHorizontalRule("- - -", 5), 1, "spaced dashes");
    CHECK_EQ(MdIsHorizontalRule("  ***  ", 7), 1, "padded stars");

    CHECK_EQ(MdIsHorizontalRule("--", 2), 0, "only two dashes");
    CHECK_EQ(MdIsHorizontalRule("", 0), 0, "empty line");
    CHECK_EQ(MdIsHorizontalRule("   ", 3), 0, "spaces only");
    CHECK_EQ(MdIsHorizontalRule("---x", 4), 0, "trailing text");
    CHECK_EQ(MdIsHorizontalRule("abc", 3), 0, "plain text");
    CHECK_EQ(MdIsHorizontalRule("-*-", 3), 0, "mixed markers");
    CHECK_EQ(MdIsHorizontalRule("- item", 6), 0, "list item, not a rule");
}

static void test_blockquote(void)
{
    printf("test_text (MdBlockquoteDepth):\n");

    CHECK_EQ(MdBlockquoteDepth("> quote", 7), 1, "single quote");
    CHECK_EQ(MdBlockquoteDepth(">quote", 6), 1, "no space after marker");
    CHECK_EQ(MdBlockquoteDepth("> > nested", 10), 2, "nested with spaces");
    CHECK_EQ(MdBlockquoteDepth(">> nested", 9), 2, "nested compact");
    CHECK_EQ(MdBlockquoteDepth("  > indented", 12), 1, "two leading spaces");
    CHECK_EQ(MdBlockquoteDepth(">", 1), 1, "bare marker");

    CHECK_EQ(MdBlockquoteDepth("quote", 5), 0, "plain text");
    CHECK_EQ(MdBlockquoteDepth("", 0), 0, "empty line");
    CHECK_EQ(MdBlockquoteDepth("    > code-indented", 19), 0, "four spaces is code, not quote");
    CHECK_EQ(MdBlockquoteDepth("a > b", 5), 0, "marker not at line start");
}

static void test_codefence(void)
{
    printf("test_text (MdIsCodeFence):\n");

    CHECK_EQ(MdIsCodeFence("```", 3), 1, "three backticks");
    CHECK_EQ(MdIsCodeFence("~~~", 3), 1, "three tildes");
    CHECK_EQ(MdIsCodeFence("```c", 4), 1, "backticks with info string");
    CHECK_EQ(MdIsCodeFence("`````", 5), 1, "five backticks");
    CHECK_EQ(MdIsCodeFence("  ```", 5), 1, "two leading spaces");

    CHECK_EQ(MdIsCodeFence("``", 2), 0, "only two backticks");
    CHECK_EQ(MdIsCodeFence("```a`b", 6), 0, "backtick in info string");
    CHECK_EQ(MdIsCodeFence("", 0), 0, "empty line");
    CHECK_EQ(MdIsCodeFence("code", 4), 0, "plain text");
    CHECK_EQ(MdIsCodeFence("    ```", 7), 0, "four spaces disqualifies");
}

static void test_listitem(void)
{
    MdListInfo info;

    printf("test_text (MdParseListItem):\n");

    info = MdParseListItem("- item", 6);
    CHECK_EQ(info.isList, 1, "dash bullet is a list");
    CHECK_EQ(info.indent, 0, "dash bullet indent");
    CHECK_EQ(info.markerChars, 2, "dash bullet marker len");
    CHECK_EQ(info.ordered, 0, "dash bullet not ordered");
    CHECK_EQ(info.checkbox, 0, "dash bullet no checkbox");

    info = MdParseListItem("* item", 6);
    CHECK_EQ(info.isList, 1, "star bullet is a list");

    info = MdParseListItem("+ item", 6);
    CHECK_EQ(info.isList, 1, "plus bullet is a list");

    info = MdParseListItem("  - nested", 10);
    CHECK_EQ(info.isList, 1, "nested bullet is a list");
    CHECK_EQ(info.indent, 2, "nested bullet indent");
    CHECK_EQ(info.markerChars, 2, "nested bullet marker len");

    info = MdParseListItem("1. first", 8);
    CHECK_EQ(info.isList, 1, "numbered is a list");
    CHECK_EQ(info.ordered, 1, "numbered is ordered");
    CHECK_EQ(info.markerChars, 3, "numbered marker len");

    info = MdParseListItem("42) item", 8);
    CHECK_EQ(info.isList, 1, "paren-numbered is a list");
    CHECK_EQ(info.ordered, 1, "paren-numbered is ordered");
    CHECK_EQ(info.markerChars, 4, "two-digit paren marker len");

    info = MdParseListItem("- [ ] task", 10);
    CHECK_EQ(info.isList, 1, "unchecked task is a list");
    CHECK_EQ(info.checkbox, 1, "unchecked task has checkbox");
    CHECK_EQ(info.checked, 0, "unchecked task not checked");
    CHECK_EQ(info.markerChars, 6, "unchecked task marker len");

    info = MdParseListItem("- [x] done", 10);
    CHECK_EQ(info.isList, 1, "checked task is a list");
    CHECK_EQ(info.checkbox, 1, "checked task has checkbox");
    CHECK_EQ(info.checked, 1, "checked task is checked");

    info = MdParseListItem("- [X] done", 10);
    CHECK_EQ(info.checked, 1, "capital X counts as checked");

    /* Non-lists */
    info = MdParseListItem("-nospace", 8);
    CHECK_EQ(info.isList, 0, "dash without space is not a list");
    info = MdParseListItem("**bold**", 8);
    CHECK_EQ(info.isList, 0, "bold marker is not a list");
    info = MdParseListItem("plain", 5);
    CHECK_EQ(info.isList, 0, "plain text is not a list");
    info = MdParseListItem("1.no space", 10);
    CHECK_EQ(info.isList, 0, "numbered without space is not a list");
}

int main(void)
{
    test_normalize();
    test_find();
    test_wordcount();
    test_hrule();
    test_blockquote();
    test_codefence();
    test_listitem();
    return TEST_RESULT();
}

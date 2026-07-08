/*
    mdcore -- pure markdown engine. See mdcore.h. No Mac headers, no
    globals, no allocation: just arithmetic on caller-provided buffers.
*/
#include "mdcore.h"

/* Records a URL (a Pascal string at src+off, urlLen chars) in the link
   table and returns its 1-based ID, or 0 if the table is full or absent.
   Mirrors the old AddLinkURL semantics. */
static short MdAddLink(MdLinkTable *links, const char *src, long off, long urlLen)
{
    short id;
    long k;

    if (links == 0 || links->count >= MD_MAX_LINKS)
        return 0;

    if (urlLen < 0)
        urlLen = 0;
    if (urlLen > 255)
        urlLen = 255;

    links->count++;
    id = links->count;
    links->url[id][0] = (unsigned char) urlLen;
    for (k = 0; k < urlLen; k++)
        links->url[id][1 + k] = (unsigned char) src[off + k];
    return id;
}

static void MdRecordSpan(MdSpan *spans, short spanCap, short *nSpans,
                         long start, long end, short kind, short level, short linkID)
{
    if (*nSpans < spanCap) {
        spans[*nSpans].start = start;
        spans[*nSpans].end = end;
        spans[*nSpans].kind = kind;
        spans[*nSpans].level = level;
        spans[*nSpans].linkID = linkID;
        (*nSpans)++;
    }
}

long MdStrip(const char *src, long len, const MdStripOpts *opts,
             char *out, long outCap,
             MdSpan *spans, short spanCap, short *spanCount,
             MdLinkTable *links)
{
    long i = 0;
    long outLen = 0;
    short nSpans = 0;

    (void) outCap; /* out is guaranteed >= len by contract; see mdcore.h */

    if (links != 0)
        links->count = 0;

    while (i < len) {
        /* Heading: a run of up to 3 '#' then a space, at the start of a
           line. Only when enabled, and only if src[0] is itself a line
           start (interior line starts are found via the preceding '\r'). */
        if (opts->headingMode != MD_HEADINGS_OFF &&
            ((i == 0 && opts->startsAtLineStart) || (i > 0 && src[i - 1] == '\r'))) {
            short level = 0;

            while (level < 3 && i + level < len && src[i + level] == '#')
                level++;
            if (level > 0 && i + level < len && src[i + level] == ' ') {
                if (opts->headingMode == MD_HEADINGS_STRIP) {
                    /* Skip only the "# " prefix; the heading body flows on
                       through the inline stripping below, so any bold/
                       italic/code/link inside it is stripped too. Records
                       no span (the "clear formatting" path). */
                    i = i + level + 1;
                    continue;
                } else {
                    /* SPAN: consume the whole line verbatim and record an
                       H span over its body (the Writer-view path). */
                    long lineStart = i + level + 1;
                    long lineEnd = lineStart;
                    long outStart = outLen;

                    while (lineEnd < len && src[lineEnd] != '\r')
                        out[outLen++] = src[lineEnd++];
                    MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                                 MD_KIND_HEADING, level, 0);
                    i = lineEnd;
                    continue;
                }
            }
        }

        if (i + 1 < len && src[i] == '*' && src[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < len && !(src[j] == '*' && src[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    out[outLen++] = src[m];
                MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                             MD_KIND_BOLD, 0, 0);
                i = j + 2;
                continue;
            }
        }
        if (src[i] == '*') {
            long j = i + 1;

            while (j < len && src[j] != '*')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    out[outLen++] = src[m];
                MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                             MD_KIND_ITALIC, 0, 0);
                i = j + 1;
                continue;
            }
        }
        if (src[i] == '`') {
            long j = i + 1;

            while (j < len && src[j] != '`')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    out[outLen++] = src[m];
                MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                             MD_KIND_CODE, 0, 0);
                i = j + 1;
                continue;
            }
        }
        if (src[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < len && src[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < len && closeBracket + 1 < len && src[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < len && src[closeParen] != ')')
                    closeParen++;
                if (closeParen < len) {
                    long outStart = outLen, m;
                    long urlLen = closeParen - (closeBracket + 2);

                    for (m = i + 1; m < closeBracket; m++)
                        out[outLen++] = src[m];
                    /* Add the URL only when its span will actually be
                       recorded, so a doc past the span cap doesn't grow the
                       link table for runs it can't reference (matches the
                       original AddLinkURL-inside-the-cap-guard behaviour). */
                    if (nSpans < spanCap)
                        MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                                     MD_KIND_LINK, 0,
                                     MdAddLink(links, src, closeBracket + 2, urlLen));
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        out[outLen++] = src[i];
        i++;
    }

    *spanCount = nSpans;
    return outLen;
}

long MdEmitInline(const char *src, long len,
                  const MdRun *runs, short runCount,
                  const MdLinkTable *links,
                  char *out, long outCap)
{
    long outLen = 0;
    int inBold = 0, inItalic = 0, inCode = 0, inLink = 0;
    unsigned char curLinkURL[256];
    short r;

    (void) len;
    (void) outCap; /* caller sizes out generously; see the adapters */

    curLinkURL[0] = 0;

    /* One extra iteration past the last run (all attributes false) closes
       whatever is still open at the end of the range -- the same job the
       old loop's final i == lineEnd / i == end pass did. */
    for (r = 0; r <= runCount; r++) {
        int wantBold = 0, wantItalic = 0, wantCode = 0, wantLink = 0;
        short linkID = 0;
        long runStart, runEnd, p;

        if (r < runCount) {
            wantBold = runs[r].bold;
            wantItalic = runs[r].italic;
            wantCode = runs[r].code;
            wantLink = runs[r].link;
            linkID = runs[r].linkID;
            runStart = runs[r].start;
            runEnd = runs[r].end;
        } else {
            runStart = runEnd = len;
        }

        /* Close innermost-first: code, italic, bold, then link (link is the
           outermost wrapper, [bold link](url)). */
        if (inCode && !wantCode) { out[outLen++] = '`'; inCode = 0; }
        if (inItalic && !wantItalic) { out[outLen++] = '*'; inItalic = 0; }
        if (inBold && !wantBold) {
            out[outLen++] = '*';
            out[outLen++] = '*';
            inBold = 0;
        }
        if (inLink && !wantLink) {
            long k;
            out[outLen++] = ']';
            out[outLen++] = '(';
            for (k = 0; k < curLinkURL[0]; k++)
                out[outLen++] = (char) curLinkURL[1 + k];
            out[outLen++] = ')';
            inLink = 0;
        }

        /* Open outermost-first: link, bold, italic, code. */
        if (!inLink && wantLink) {
            out[outLen++] = '[';
            inLink = 1;
            if (links != 0 && linkID >= 1 && linkID <= links->count) {
                long n = links->url[linkID][0];
                long k;
                curLinkURL[0] = (unsigned char) n;
                for (k = 0; k < n; k++)
                    curLinkURL[1 + k] = links->url[linkID][1 + k];
            } else {
                curLinkURL[0] = 0;
            }
        }
        if (!inBold && wantBold) {
            out[outLen++] = '*';
            out[outLen++] = '*';
            inBold = 1;
        }
        if (!inItalic && wantItalic) { out[outLen++] = '*'; inItalic = 1; }
        if (!inCode && wantCode) { out[outLen++] = '`'; inCode = 1; }

        for (p = runStart; p < runEnd; p++)
            out[outLen++] = src[p];
    }

    return outLen;
}

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

/* Forward decl: the inline stripper and its span-content recursion are
   mutually recursive so that a delimiter pair's content is itself stripped
   (this is what makes inline styles nest -- see mdcore.h). */
static long MdStripInlineAt(const char *src, long i, long end,
                            char *out, long *outLen,
                            MdSpan *spans, short spanCap, short *nSpans,
                            MdLinkTable *links);

/* Strips src[a..b) as inline content, appending stripped text to out and
   recording spans, recursing into any nested delimiter pairs. */
static void MdStripSpanContent(const char *src, long a, long b,
                               char *out, long *outLen,
                               MdSpan *spans, short spanCap, short *nSpans,
                               MdLinkTable *links)
{
    long p = a;

    while (p < b) {
        long consumed = MdStripInlineAt(src, p, b, out, outLen,
                                        spans, spanCap, nSpans, links);
        if (consumed > 0) {
            p += consumed;
        } else {
            out[(*outLen)++] = src[p];
            p++;
        }
    }
}

/*
    If an inline construct starts at src[i] (bounded by end), strips it --
    appending its stripped content to out, recording the outer span (over the
    stripped content, so it survives the recursion that strips any nested
    inner styles), and adding its URL for a link -- and returns how many
    SOURCE characters it consumed. Returns 0 if nothing matched at i, so the
    caller copies src[i] literally. The match order (***, then **, then *,
    then `, then ~~, then [..](..)) preserves the original single-level
    parser's precedence, including the "unmatched ** is an empty italic" quirk.
*/
static long MdStripInlineAt(const char *src, long i, long end,
                            char *out, long *outLen,
                            MdSpan *spans, short spanCap, short *nSpans,
                            MdLinkTable *links)
{
    /* ***bold italic***: three stars, closed by three stars. Checked before
       ** so the triple isn't mis-read as bold + a stray star. Records BOTH a
       bold and an italic span over the (recursively stripped) content. */
    if (i + 2 < end && src[i] == '*' && src[i + 1] == '*' && src[i + 2] == '*') {
        long j = i + 3;

        while (j + 2 < end &&
               !(src[j] == '*' && src[j + 1] == '*' && src[j + 2] == '*'))
            j++;
        if (j + 2 < end && src[j] == '*' && src[j + 1] == '*' && src[j + 2] == '*') {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 3, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_BOLD, 0, 0);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_ITALIC, 0, 0);
            return (j + 3) - i;
        }
    }
    if (i + 1 < end && src[i] == '*' && src[i + 1] == '*') {
        long j = i + 2;

        while (j + 1 < end && !(src[j] == '*' && src[j + 1] == '*'))
            j++;
        if (j + 1 < end) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 2, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_BOLD, 0, 0);
            return (j + 2) - i;
        }
    }
    if (src[i] == '*') {
        long j = i + 1;

        while (j < end && src[j] != '*')
            j++;
        if (j < end) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 1, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_ITALIC, 0, 0);
            return (j + 1) - i;
        }
    }
    if (src[i] == '`') {
        long j = i + 1;

        while (j < end && src[j] != '`')
            j++;
        if (j < end) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 1, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_CODE, 0, 0);
            return (j + 1) - i;
        }
    }
    if (i + 1 < end && src[i] == '~' && src[i + 1] == '~') {
        long j = i + 2;

        while (j + 1 < end && !(src[j] == '~' && src[j + 1] == '~'))
            j++;
        if (j + 1 < end) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 2, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_STRIKE, 0, 0);
            return (j + 2) - i;
        }
    }
    if (src[i] == '[') {
        long closeBracket = i + 1;

        while (closeBracket < end && src[closeBracket] != ']')
            closeBracket++;
        if (closeBracket < end && closeBracket + 1 < end &&
            src[closeBracket + 1] == '(') {
            long closeParen = closeBracket + 2;

            while (closeParen < end && src[closeParen] != ')')
                closeParen++;
            if (closeParen < end) {
                long urlLen = closeParen - (closeBracket + 2);
                long outStart = *outLen;
                short id;

                /* Register the URL first so its ID is known, then strip the
                   link text (which may itself carry nested styles) and record
                   the LINK span over it. Guard the whole record on the span
                   cap so a doc past the cap doesn't grow the link table for a
                   run it can't reference (matches the original behaviour). (If
                   the recursion exhausts the last span slots the LINK span is
                   dropped while its URL stays registered -- a harmless orphaned
                   table entry, only reachable in a >512-span pathological doc.) */
                if (*nSpans < spanCap) {
                    id = MdAddLink(links, src, closeBracket + 2, urlLen);
                    MdStripSpanContent(src, i + 1, closeBracket, out, outLen,
                                       spans, spanCap, nSpans, links);
                    MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                                 MD_KIND_LINK, 0, id);
                } else {
                    MdStripSpanContent(src, i + 1, closeBracket, out, outLen,
                                       spans, spanCap, nSpans, links);
                }
                return (closeParen + 1) - i;
            }
        }
    }

    return 0;
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
        long consumed;

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
                       italic/code/link inside it is stripped (and nested) too.
                       Records no span (the "clear formatting" path). */
                    i = i + level + 1;
                    continue;
                } else {
                    /* SPAN: consume the whole line verbatim and record an
                       H span over its body (the Writer-view path). Inline
                       styles inside a heading are intentionally left literal. */
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

        consumed = MdStripInlineAt(src, i, len, out, &outLen,
                                   spans, spanCap, &nSpans, links);
        if (consumed > 0) {
            i += consumed;
            continue;
        }

        out[outLen++] = src[i];
        i++;
    }

    *spanCount = nSpans;
    return outLen;
}

short MdSpansToRuns(long textLen, const MdSpan *spans, short spanCount,
                    MdRun *runs, short cap)
{
    short nRuns = 0;
    long c;

    for (c = 0; c < textLen; c++) {
        int b = 0, it = 0, cd = 0, lk = 0, sk = 0;
        short id = 0, s;

        for (s = 0; s < spanCount; s++) {
            if (spans[s].start <= c && c < spans[s].end) {
                switch (spans[s].kind) {
                    case MD_KIND_BOLD:   b = 1; break;
                    case MD_KIND_ITALIC: it = 1; break;
                    case MD_KIND_CODE:   cd = 1; break;
                    case MD_KIND_LINK:   lk = 1; id = spans[s].linkID; break;
                    case MD_KIND_STRIKE: sk = 1; break;
                    default: break;  /* MD_KIND_HEADING: line-level, ignored */
                }
            }
        }

        if (nRuns > 0 && runs[nRuns - 1].bold == b && runs[nRuns - 1].italic == it &&
            runs[nRuns - 1].code == cd && runs[nRuns - 1].link == lk &&
            runs[nRuns - 1].strike == sk && runs[nRuns - 1].linkID == id) {
            runs[nRuns - 1].end = c + 1;
        } else if (nRuns < cap) {
            runs[nRuns].start = c;
            runs[nRuns].end = c + 1;
            runs[nRuns].bold = b;
            runs[nRuns].italic = it;
            runs[nRuns].code = cd;
            runs[nRuns].link = lk;
            runs[nRuns].strike = sk;
            runs[nRuns].linkID = id;
            nRuns++;
        } else if (nRuns > 0) {
            /* Run table full: fold the rest into the last run so no character
               is dropped, matching BuildStyleRuns' overflow policy. With cap
               == MD_MAX_RUNS this is unreachable (MD_MAX_SPANS spans can't make
               more than MD_MAX_RUNS runs); the nRuns > 0 guard just keeps a
               degenerate cap == 0 call from writing runs[-1]. */
            runs[nRuns - 1].end = c + 1;
        }
    }

    return nRuns;
}

long MdEmitInline(const char *src, long len,
                  const MdRun *runs, short runCount,
                  const MdLinkTable *links,
                  char *out, long outCap)
{
    long outLen = 0;
    int inBold = 0, inItalic = 0, inCode = 0, inLink = 0, inStrike = 0;
    unsigned char curLinkURL[256];
    short r;

    (void) len;
    (void) outCap; /* caller sizes out generously; see the adapters */

    curLinkURL[0] = 0;

    /* One extra iteration past the last run (all attributes false) closes
       whatever is still open at the end of the range -- the same job the
       old loop's final i == lineEnd / i == end pass did. */
    for (r = 0; r <= runCount; r++) {
        int wantBold = 0, wantItalic = 0, wantCode = 0, wantLink = 0, wantStrike = 0;
        short linkID = 0;
        long runStart, runEnd, p;

        if (r < runCount) {
            wantBold = runs[r].bold;
            wantItalic = runs[r].italic;
            wantCode = runs[r].code;
            wantLink = runs[r].link;
            wantStrike = runs[r].strike;
            linkID = runs[r].linkID;
            runStart = runs[r].start;
            runEnd = runs[r].end;
        } else {
            runStart = runEnd = len;
        }

        /* Close innermost-first: code, italic, bold, strike, then link (link
           is the outermost wrapper, [~~bold link~~](url)). */
        if (inCode && !wantCode) { out[outLen++] = '`'; inCode = 0; }
        if (inItalic && !wantItalic) { out[outLen++] = '*'; inItalic = 0; }
        if (inBold && !wantBold) {
            out[outLen++] = '*';
            out[outLen++] = '*';
            inBold = 0;
        }
        if (inStrike && !wantStrike) {
            out[outLen++] = '~';
            out[outLen++] = '~';
            inStrike = 0;
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

        /* Open outermost-first: link, strike, bold, italic, code. */
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
        if (!inStrike && wantStrike) {
            out[outLen++] = '~';
            out[outLen++] = '~';
            inStrike = 1;
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

MdInlineEdit MdDetectInline(const char *buf, long len, long caret, char justTyped)
{
    MdInlineEdit e;
    long lineStart, lineEnd;

    e.kind = MD_INLINE_NONE;
    e.level = 0;
    e.del1Start = e.del1End = 0;
    e.del2Start = e.del2End = 0;
    e.styleStart = e.styleEnd = 0;
    e.newCaret = 0;
    e.resetNormal = 0;
    e.linkURL[0] = 0;

    lineStart = caret;
    while (lineStart > 0 && buf[lineStart - 1] != '\r')
        lineStart--;

    lineEnd = caret;
    while (lineEnd < len && buf[lineEnd] != '\r')
        lineEnd++;

    if (justTyped == ' ') {
        short level = 0;
        long p = lineStart;

        while (level < 3 && p < caret - 1 && buf[p] == '#') {
            level++;
            p++;
        }
        if (level > 0 && p == caret - 1) {
            /* "# " through "### " at line start: delete the prefix and the
               space, then leave a bold, larger typing style in place so the
               heading is typed live. No del2, no reset. */
            e.kind = MD_KIND_HEADING;
            e.level = level;
            e.del1Start = lineStart;
            e.del1End = caret;
            e.styleStart = e.styleEnd = lineStart;
            e.newCaret = lineStart;
            e.resetNormal = 0;
            return e;
        }
    } else if (justTyped == '*') {
        if (caret >= 4 && buf[caret - 2] == '*' && buf[caret - 1] == '*') {
            long p = caret - 4;

            while (p >= lineStart) {
                if (buf[p] == '*' && buf[p + 1] == '*' && p + 2 < caret - 2) {
                    long innerStart = p + 2;
                    long innerEnd = caret - 2;

                    e.kind = MD_KIND_BOLD;
                    e.del1Start = innerEnd; e.del1End = caret;
                    e.del2Start = p;        e.del2End = innerStart;
                    e.styleStart = p;       e.styleEnd = innerEnd - 2;
                    e.newCaret = innerEnd - 2;
                    e.resetNormal = 1;
                    return e;
                }
                p--;
            }

            /* No opening ** behind the caret -- the just-typed ** may
               instead be an OPENING delimiter for a closing ** already
               sitting later in the line (bold typed closing-first). */
            {
                long q = caret + 1;

                while (q + 1 < lineEnd) {
                    if (buf[q] == '*' && buf[q + 1] == '*') {
                        long innerEnd = q;

                        e.kind = MD_KIND_BOLD;
                        e.del1Start = innerEnd;  e.del1End = innerEnd + 2;
                        e.del2Start = caret - 2; e.del2End = caret;
                        e.styleStart = caret - 2; e.styleEnd = innerEnd - 2;
                        e.newCaret = caret - 2;
                        e.resetNormal = 1;
                        return e;
                    }
                    q++;
                }
            }
        } else if (caret >= 3 && buf[caret - 2] != '*') {
            long p = caret - 2;

            while (p >= lineStart) {
                if (buf[p] == '*' &&
                    (p == lineStart || buf[p - 1] != '*') &&
                    buf[p + 1] != '*' && p + 1 < caret - 1) {
                    long innerStart = p + 1;
                    long innerEnd = caret - 1;

                    e.kind = MD_KIND_ITALIC;
                    e.del1Start = innerEnd; e.del1End = caret;
                    e.del2Start = p;        e.del2End = innerStart;
                    e.styleStart = p;       e.styleEnd = innerEnd - 1;
                    e.newCaret = innerEnd - 1;
                    e.resetNormal = 1;
                    return e;
                }
                p--;
            }

            /* No opening * behind the caret -- the just-typed * may instead
               be an OPENING italic delimiter for a closing * already later
               in the line. */
            {
                long q = caret;

                while (q < lineEnd) {
                    if (buf[q] == '*' &&
                        buf[q - 1] != '*' &&
                        (q + 1 == lineEnd || buf[q + 1] != '*') &&
                        q > caret) {
                        long innerEnd = q;

                        e.kind = MD_KIND_ITALIC;
                        e.del1Start = innerEnd;  e.del1End = innerEnd + 1;
                        e.del2Start = caret - 1; e.del2End = caret;
                        e.styleStart = caret - 1; e.styleEnd = innerEnd - 1;
                        e.newCaret = caret - 1;
                        e.resetNormal = 1;
                        return e;
                    }
                    q++;
                }
            }
        }
    } else if (justTyped == '`') {
        long p = caret - 2;

        while (p >= lineStart) {
            if (buf[p] == '`' && p + 1 < caret - 1) {
                long innerStart = p + 1;
                long innerEnd = caret - 1;

                e.kind = MD_KIND_CODE;
                e.del1Start = innerEnd; e.del1End = caret;
                e.del2Start = p;        e.del2End = innerStart;
                e.styleStart = p;       e.styleEnd = innerEnd - 1;
                e.newCaret = innerEnd - 1;
                e.resetNormal = 1;
                return e;
            }
            p--;
        }

        /* No opening ` behind the caret -- the just-typed ` may instead be
           an OPENING code delimiter for a closing ` already later. */
        {
            long q = caret;

            while (q < lineEnd) {
                if (buf[q] == '`' && q > caret) {
                    long innerEnd = q;

                    e.kind = MD_KIND_CODE;
                    e.del1Start = innerEnd;  e.del1End = innerEnd + 1;
                    e.del2Start = caret - 1; e.del2End = caret;
                    e.styleStart = caret - 1; e.styleEnd = innerEnd - 1;
                    e.newCaret = caret - 1;
                    e.resetNormal = 1;
                    return e;
                }
                q++;
            }
        }
    } else if (justTyped == '~') {
        if (caret >= 4 && buf[caret - 2] == '~' && buf[caret - 1] == '~') {
            long p = caret - 4;

            while (p >= lineStart) {
                if (buf[p] == '~' && buf[p + 1] == '~' && p + 2 < caret - 2) {
                    long innerStart = p + 2;
                    long innerEnd = caret - 2;

                    e.kind = MD_KIND_STRIKE;
                    e.del1Start = innerEnd; e.del1End = caret;
                    e.del2Start = p;        e.del2End = innerStart;
                    e.styleStart = p;       e.styleEnd = innerEnd - 2;
                    e.newCaret = innerEnd - 2;
                    e.resetNormal = 1;
                    return e;
                }
                p--;
            }

            /* No opening ~~ behind the caret -- the just-typed ~~ may
               instead be an OPENING delimiter for a closing ~~ already
               sitting later in the line (strike typed closing-first). */
            {
                long q = caret + 1;

                while (q + 1 < lineEnd) {
                    if (buf[q] == '~' && buf[q + 1] == '~') {
                        long innerEnd = q;

                        e.kind = MD_KIND_STRIKE;
                        e.del1Start = innerEnd;  e.del1End = innerEnd + 2;
                        e.del2Start = caret - 2; e.del2End = caret;
                        e.styleStart = caret - 2; e.styleEnd = innerEnd - 2;
                        e.newCaret = caret - 2;
                        e.resetNormal = 1;
                        return e;
                    }
                    q++;
                }
            }
        }
    } else if (justTyped == ')') {
        long closeParenPos = caret - 1;
        long p = closeParenPos - 1;

        while (p >= lineStart && buf[p] != '(')
            p--;

        if (p >= lineStart && p > lineStart && buf[p - 1] == ']') {
            long openParenPos = p;
            long closeBracketPos = openParenPos - 1;
            long urlStart = openParenPos + 1;
            long urlLen = closeParenPos - urlStart;
            long q = closeBracketPos - 1;

            while (q >= lineStart && buf[q] != '[')
                q--;

            if (q >= lineStart) {
                long openBracketPos = q;
                long k;

                if (urlLen < 0) urlLen = 0;
                if (urlLen > 255) urlLen = 255;
                e.linkURL[0] = (unsigned char) urlLen;
                for (k = 0; k < urlLen; k++)
                    e.linkURL[1 + k] = (unsigned char) buf[urlStart + k];

                e.kind = MD_KIND_LINK;
                e.del1Start = closeBracketPos; e.del1End = caret;
                e.del2Start = openBracketPos;  e.del2End = openBracketPos + 1;
                e.styleStart = openBracketPos; e.styleEnd = closeBracketPos - 1;
                e.newCaret = closeBracketPos - 1;
                e.resetNormal = 1;
                return e;
            }
        }
    }

    return e;
}

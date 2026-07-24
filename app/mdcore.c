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

/* True when src[p..p+2) is exactly a two-character run of delimiter d -- not
   the edge of a longer run. A run of three or more (e.g. ~~~) is not a valid
   ~~/== delimiter, so "~~~word~~~" is stripped as literal text rather than a
   strike/highlight span (mirrors the live detector and the fence rule). The
   nesting delimiters '*'/'`' are handled separately and keep their own logic. */
static int IsPairDelimAt(const char *src, long p, long end, char d)
{
    return p + 1 < end && src[p] == d && src[p + 1] == d
        && !(p > 0 && src[p - 1] == d)
        && !(p + 2 < end && src[p + 2] == d);
}

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
    if (IsPairDelimAt(src, i, end, '~')) {
        long j = i + 2;

        while (j + 1 < end && !IsPairDelimAt(src, j, end, '~'))
            j++;
        if (IsPairDelimAt(src, j, end, '~')) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 2, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_STRIKE, 0, 0);
            return (j + 2) - i;
        }
    }
    if (IsPairDelimAt(src, i, end, '=')) {
        long j = i + 2;

        while (j + 1 < end && !IsPairDelimAt(src, j, end, '='))
            j++;
        if (IsPairDelimAt(src, j, end, '=')) {
            long outStart = *outLen;
            MdStripSpanContent(src, i + 2, j, out, outLen,
                               spans, spanCap, nSpans, links);
            MdRecordSpan(spans, spanCap, nSpans, outStart, *outLen,
                         MD_KIND_HIGHLIGHT, 0, 0);
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
        int atLineStart = ((i == 0 && opts->startsAtLineStart) ||
                           (i > 0 && src[i - 1] == '\r'));

        /* Fenced code block: at a line start, ``` (3+ backticks) opens a block
           whose body is LITERAL code. Like every other Writer style, the marker
           lines are HIDDEN -- the opening and closing ``` (info string and all)
           are dropped and only the body is emitted, styled as one Monaco CODE span
           that INCLUDES the body's internal '\r' line breaks. That code-run-spans-
           a-newline is exactly how the emit (SyncHiddenToCanonical) later
           reconstructs the fence: a Monaco run containing a '\r' is a multi-line
           block (re-fenced with ```), one without is inline `code`. No marker text
           is kept, and nothing else can produce a multi-line code run, so the
           round-trip needs no font-existence heuristic. Requires a matching
           closing fence; an unclosed opener falls through to ordinary text. This is
           the sole block feature that becomes a style run rather than a draw-time
           overpaint (see markdown.c). */
        if (atLineStart) {
            long lineEnd = i;

            while (lineEnd < len && src[lineEnd] != '\r')
                lineEnd++;
            if (MdIsCodeFence(src + i, lineEnd - i)) {
                long bodyStart = (lineEnd < len) ? lineEnd + 1 : len;
                long p = bodyStart;
                long closeStart = -1;
                long closeEnd = len;

                while (p < len) {
                    long le = p;

                    while (le < len && src[le] != '\r')
                        le++;
                    if (MdIsCodeFence(src + p, le - p)) {
                        closeStart = p;
                        closeEnd = le;
                        break;
                    }
                    p = (le < len) ? le + 1 : len;
                }
                if (closeStart >= 0) {
                    long bodyEnd = closeStart;
                    long outStart = outLen;
                    long k;

                    /* Drop the '\r' that ends the last body line (it precedes the
                       closing fence), so the block body carries no trailing blank.
                       The '\r' before the opening fence was already emitted by the
                       previous line; both marker lines are skipped entirely. */
                    if (bodyEnd > bodyStart && src[bodyEnd - 1] == '\r')
                        bodyEnd--;
                    for (k = bodyStart; k < bodyEnd; k++)
                        out[outLen++] = src[k];
                    if (outLen > outStart)
                        MdRecordSpan(spans, spanCap, &nSpans, outStart, outLen,
                                     MD_KIND_CODE, 0, 0);
                    i = closeEnd;   /* keep the '\r' after the closing fence */
                    continue;
                }
                /* Unclosed: fall through and treat the opener line as text. */
            }
        }

        /* Heading: a run of up to 3 '#' then a space, at the start of a
           line. Only when enabled, and only if src[0] is itself a line
           start (interior line starts are found via the preceding '\r'). */
        if (opts->headingMode != MD_HEADINGS_OFF && atLineStart) {
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
        int b = 0, it = 0, cd = 0, lk = 0, sk = 0, hl = 0;
        short id = 0, s;

        for (s = 0; s < spanCount; s++) {
            if (spans[s].start <= c && c < spans[s].end) {
                switch (spans[s].kind) {
                    case MD_KIND_BOLD:      b = 1; break;
                    case MD_KIND_ITALIC:    it = 1; break;
                    case MD_KIND_CODE:      cd = 1; break;
                    case MD_KIND_LINK:      lk = 1; id = spans[s].linkID; break;
                    case MD_KIND_STRIKE:    sk = 1; break;
                    case MD_KIND_HIGHLIGHT: hl = 1; break;
                    default: break;  /* MD_KIND_HEADING: line-level, ignored */
                }
            }
        }

        if (nRuns > 0 && runs[nRuns - 1].bold == b && runs[nRuns - 1].italic == it &&
            runs[nRuns - 1].code == cd && runs[nRuns - 1].link == lk &&
            runs[nRuns - 1].strike == sk && runs[nRuns - 1].highlight == hl &&
            runs[nRuns - 1].linkID == id) {
            runs[nRuns - 1].end = c + 1;
        } else if (nRuns < cap) {
            runs[nRuns].start = c;
            runs[nRuns].end = c + 1;
            runs[nRuns].bold = b;
            runs[nRuns].italic = it;
            runs[nRuns].code = cd;
            runs[nRuns].link = lk;
            runs[nRuns].strike = sk;
            runs[nRuns].highlight = hl;
            runs[nRuns].linkID = id;
            runs[nRuns].heading = 0;   /* headings are line-level, applied by
                                          MdSpansToDocRuns, not here */
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

MdStyleFields MdRunToFields(const MdRun *run)
{
    MdStyleFields f;

    f.face = (short) ((run->bold ? MD_FACE_BOLD : 0) |
                      (run->italic ? MD_FACE_ITALIC : 0) |
                      (run->link ? MD_FACE_UNDERLINE : 0));
    f.code = run->code ? 1 : 0;
    f.linkID = run->link ? run->linkID : 0;
    f.strike = run->strike ? 1 : 0;
    f.highlight = run->highlight ? 1 : 0;
    return f;
}

void MdFieldsToRun(const MdStyleFields *fields, MdRun *run)
{
    run->bold = (fields->face & MD_FACE_BOLD) != 0;
    run->italic = (fields->face & MD_FACE_ITALIC) != 0;
    run->code = fields->code ? 1 : 0;
    run->link = (fields->face & MD_FACE_UNDERLINE) != 0;
    run->strike = fields->strike ? 1 : 0;
    run->highlight = fields->highlight ? 1 : 0;
    run->linkID = fields->linkID;
}

short MdHeadingSizeForLevel(short baseSize, short level)
{
    return (short) (baseSize + (4 - level) * 4);
}

short MdHeadingLevelForSize(short baseSize, short size)
{
    short level;

    for (level = 1; level <= 3; level++)
        if (size == MdHeadingSizeForLevel(baseSize, level))
            return level;
    return 0;
}

long MdEmitInline(const char *src, long len,
                  const MdRun *runs, short runCount,
                  const MdLinkTable *links,
                  char *out, long outCap)
{
    long outLen = 0;
    int inBold = 0, inItalic = 0, inCode = 0, inLink = 0, inStrike = 0, inHigh = 0;
    unsigned char curLinkURL[256];
    short r;

    /* Every write goes through PUT so the buffer can never overrun: a char that
       toggles all five styles emits ~12 delimiter bytes, so no fixed size
       estimate the caller passes is provably enough for an adversarial buffer.
       When full, PUT drops the byte (truncating the tail) instead of writing
       past outCap. Undef'd at the end of the function. */
#define PUT(ch) do { if (outLen < outCap) out[outLen++] = (char) (ch); } while (0)

    (void) len;

    curLinkURL[0] = 0;

    /* One extra iteration past the last run (all attributes false) closes
       whatever is still open at the end of the range -- the same job the
       old loop's final i == lineEnd / i == end pass did. */
    for (r = 0; r <= runCount; r++) {
        int wantBold = 0, wantItalic = 0, wantCode = 0, wantLink = 0, wantStrike = 0;
        int wantHigh = 0;
        short linkID = 0;
        long runStart, runEnd, p;

        if (r < runCount) {
            wantBold = runs[r].bold;
            wantItalic = runs[r].italic;
            wantCode = runs[r].code;
            wantLink = runs[r].link;
            wantStrike = runs[r].strike;
            wantHigh = runs[r].highlight;
            linkID = runs[r].linkID;
            runStart = runs[r].start;
            runEnd = runs[r].end;
        } else {
            runStart = runEnd = len;
        }

        /* Close innermost-first: code, italic, bold, strike, highlight, then
           link (link is the outermost wrapper, [==~~x~~==](url)). */
        if (inCode && !wantCode) { PUT('`'); inCode = 0; }
        if (inItalic && !wantItalic) { PUT('*'); inItalic = 0; }
        if (inBold && !wantBold) {
            PUT('*');
            PUT('*');
            inBold = 0;
        }
        if (inStrike && !wantStrike) {
            PUT('~');
            PUT('~');
            inStrike = 0;
        }
        if (inHigh && !wantHigh) {
            PUT('=');
            PUT('=');
            inHigh = 0;
        }
        if (inLink && !wantLink) {
            long k;
            PUT(']');
            PUT('(');
            for (k = 0; k < curLinkURL[0]; k++)
                PUT(curLinkURL[1 + k]);
            PUT(')');
            inLink = 0;
        }

        /* Open outermost-first: link, highlight, strike, bold, italic, code. */
        if (!inLink && wantLink) {
            PUT('[');
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
        if (!inHigh && wantHigh) {
            PUT('=');
            PUT('=');
            inHigh = 1;
        }
        if (!inStrike && wantStrike) {
            PUT('~');
            PUT('~');
            inStrike = 1;
        }
        if (!inBold && wantBold) {
            PUT('*');
            PUT('*');
            inBold = 1;
        }
        if (!inItalic && wantItalic) { PUT('*'); inItalic = 1; }
        if (!inCode && wantCode) { PUT('`'); inCode = 1; }

        for (p = runStart; p < runEnd; p++)
            PUT(src[p]);
    }

    return outLen;

#undef PUT
}

/* Level of the HEADING span covering character c, or 0 if none. Headings never
   nest inside inline styles and cover a whole line's body, so at most one covers
   any character. */
static short HeadingLevelAt(long c, const MdSpan *spans, short spanCount)
{
    short s;

    for (s = 0; s < spanCount; s++)
        if (spans[s].kind == MD_KIND_HEADING &&
            spans[s].start <= c && c < spans[s].end)
            return spans[s].level;
    return 0;
}

short MdSpansToDocRuns(long textLen, const MdSpan *spans, short spanCount,
                       MdRun *runs, short cap)
{
    short nRuns = 0;
    long c;

    for (c = 0; c < textLen; c++) {
        int b = 0, it = 0, cd = 0, lk = 0, sk = 0, hl = 0;
        short id = 0, hd, s;

        for (s = 0; s < spanCount; s++) {
            if (spans[s].start <= c && c < spans[s].end) {
                switch (spans[s].kind) {
                    case MD_KIND_BOLD:      b = 1; break;
                    case MD_KIND_ITALIC:    it = 1; break;
                    case MD_KIND_CODE:      cd = 1; break;
                    case MD_KIND_LINK:      lk = 1; id = spans[s].linkID; break;
                    case MD_KIND_STRIKE:    sk = 1; break;
                    case MD_KIND_HIGHLIGHT: hl = 1; break;
                    default: break;  /* MD_KIND_HEADING handled below */
                }
            }
        }
        hd = HeadingLevelAt(c, spans, spanCount);

        /* Coalesce like MdSpansToRuns, but ALSO break on heading level so a
           heading line is its own run(s) MdEmit can prefix -- and so a heading
           line never merges into the surrounding plain text. */
        if (nRuns > 0 && runs[nRuns - 1].bold == b && runs[nRuns - 1].italic == it &&
            runs[nRuns - 1].code == cd && runs[nRuns - 1].link == lk &&
            runs[nRuns - 1].strike == sk && runs[nRuns - 1].highlight == hl &&
            runs[nRuns - 1].linkID == id && runs[nRuns - 1].heading == hd) {
            runs[nRuns - 1].end = c + 1;
        } else if (nRuns < cap) {
            runs[nRuns].start = c;
            runs[nRuns].end = c + 1;
            runs[nRuns].bold = b;
            runs[nRuns].italic = it;
            runs[nRuns].code = cd;
            runs[nRuns].link = lk;
            runs[nRuns].strike = sk;
            runs[nRuns].highlight = hl;
            runs[nRuns].linkID = id;
            runs[nRuns].heading = hd;
            nRuns++;
        } else if (nRuns > 0) {
            runs[nRuns - 1].end = c + 1;
        }
    }

    return nRuns;
}

/* True iff runs[r]'s text contains a '\r' -- the signal that a `code` run is a
   multi-line fenced block rather than inline `code`. */
static int RunHasCR(const char *stripped, const MdRun *run)
{
    long p;

    for (p = run->start; p < run->end; p++)
        if (stripped[p] == '\r')
            return 1;
    return 0;
}

long MdEmit(const char *stripped, long len,
            MdRun *runs, short runCount,
            const MdLinkTable *links,
            char *out, long outCap)
{
    long outLen = 0;
    long lineStart = 0;
    short ri = 0;   /* index of the run covering lineStart */

#define EPUT(ch) do { if (outLen < outCap) out[outLen++] = (char) (ch); } while (0)

    /* One extra pass at lineStart == len emits nothing but lets a document that
       ends in '\r' close out cleanly (mirrors the adapter's old loop bound). */
    while (lineStart <= len) {
        long lineEnd;

        while (ri < runCount && runs[ri].end <= lineStart)
            ri++;

        /* Multi-line fenced code block: the run at the line start is `code` and
           its text spans a '\r'. Re-fence its whole body verbatim. The fence body
           begins at the line start (a fenced block always opens at one); the plain
           '\r' that bounds the run is skipped, exactly as MdStrip dropped it. */
        if (lineStart < len && ri < runCount && runs[ri].code &&
            RunHasCR(stripped, &runs[ri])) {
            long bodyEnd = runs[ri].end;
            long p;

            EPUT('`'); EPUT('`'); EPUT('`'); EPUT('\r');
            for (p = lineStart; p < bodyEnd; p++)
                EPUT(stripped[p]);
            EPUT('\r'); EPUT('`'); EPUT('`'); EPUT('`');
            if (bodyEnd < len)              /* the plain '\r' that bounded the run */
                EPUT('\r');
            lineStart = bodyEnd + 1;
            continue;
        }

        lineEnd = lineStart;
        while (lineEnd < len && stripped[lineEnd] != '\r')
            lineEnd++;

        if (lineEnd > lineStart && ri < runCount && runs[ri].heading > 0) {
            short k;
            long p;

            for (k = 0; k < runs[ri].heading; k++)
                EPUT('#');
            EPUT(' ');
            for (p = lineStart; p < lineEnd; p++)
                EPUT(stripped[p]);
        } else if (lineEnd > lineStart && ri < runCount) {
            short rhi = ri;
            long savedStart, savedEnd;

            while (rhi < runCount && runs[rhi].start < lineEnd)
                rhi++;
            /* Emit exactly [lineStart, lineEnd) inline. Runs coalesce across '\r',
               so the first/last run of this line may spill past the line bounds;
               clamp them to the line for the call, then restore so the next line
               still sees the run's real extent. Interior runs already lie wholly
               within the line. */
            savedStart = runs[ri].start;
            savedEnd = runs[rhi - 1].end;
            runs[ri].start = lineStart;
            runs[rhi - 1].end = lineEnd;
            outLen += MdEmitInline(stripped, lineEnd, &runs[ri],
                                   (short) (rhi - ri), links,
                                   out + outLen, outCap - outLen);
            runs[ri].start = savedStart;
            runs[rhi - 1].end = savedEnd;
        }

        if (lineEnd < len)
            EPUT('\r');
        lineStart = lineEnd + 1;
    }

    return outLen;

#undef EPUT
}

/*
    Each of the following DetectXxx helpers owns one delimiter's live-typing
    rule. They read the buffer, and on a match fill *e with the edit plan and
    return 1; on no match they return 0 and leave *e untouched (the caller has
    already parked MD_INLINE_NONE there). MdDetectInline is then just the
    line-bounds scan plus a dispatch on the character just typed. Splitting the
    old one big switch this way keeps each rule small and individually testable
    (the host suite drives them all through MdDetectInline).
*/

/* Leading "# " through "### " at the line start (justTyped == ' '). */
static int DetectHeading(const char *buf, long caret, long lineStart,
                         MdInlineEdit *e)
{
    short level = 0;
    long p = lineStart;

    while (level < 3 && p < caret - 1 && buf[p] == '#') {
        level++;
        p++;
    }
    if (level > 0 && p == caret - 1) {
        /* delete the prefix and the space, then leave a bold, larger typing
           style in place so the heading is typed live. No del2, no reset. */
        e->kind = MD_KIND_HEADING;
        e->level = level;
        e->del1Start = lineStart;
        e->del1End = caret;
        e->styleStart = e->styleEnd = lineStart;
        e->newCaret = lineStart;
        e->resetNormal = 0;
        return 1;
    }
    return 0;
}

/*
    A two-character paired delimiter -- **bold**, ~~strike~~, ==highlight==.
    All three share this exact shape, differing only in the delimiter byte d
    and the MD_KIND_* to record. Called when justTyped is that byte; self-gates
    on the just-completed "dd" so it is safe to call unconditionally.
*/
static int DetectPairDelim(const char *buf, long caret, long lineStart,
                           long lineEnd, char d, short kind, MdInlineEdit *e)
{
    long p, q;

    if (!(caret >= 4 && buf[caret - 2] == d && buf[caret - 1] == d))
        return 0;

    /* A run of three or more 'd's is not a valid "dd" delimiter: typing
       ~~~word~~~ (or ***x***, ===y===) must stay literal, not collapse to a
       styled span. Reject when the just-completed pair is only the tail of a
       longer run; nesting like ***x*** is left for MdStrip to resolve on the
       mode switch. */
    if (caret - 3 >= lineStart && buf[caret - 3] == d)
        return 0;

    /* An opening "dd" behind the caret => the pair just closed. */
    for (p = caret - 4; p >= lineStart; p--) {
        if (buf[p] == d && buf[p + 1] == d && p + 2 < caret - 2) {
            long innerEnd = caret - 2;

            /* Skip an opener that is itself part of a 3+ run. */
            if ((p > lineStart && buf[p - 1] == d) || buf[p + 2] == d)
                continue;

            e->kind = kind;
            e->del1Start = innerEnd; e->del1End = caret;
            e->del2Start = p;        e->del2End = p + 2;
            e->styleStart = p;       e->styleEnd = innerEnd - 2;
            e->newCaret = innerEnd - 2;
            e->resetNormal = 1;
            return 1;
        }
    }

    /* No opening "dd" behind the caret -- the just-typed "dd" may instead be
       an OPENING delimiter for a closing "dd" already later in the line
       (typed closing-first). */
    for (q = caret + 1; q + 1 < lineEnd; q++) {
        if (buf[q] == d && buf[q + 1] == d) {
            long innerEnd = q;

            /* Skip a closer that is itself part of a 3+ run. */
            if ((q > caret && buf[q - 1] == d) ||
                (q + 2 < lineEnd && buf[q + 2] == d))
                continue;

            e->kind = kind;
            e->del1Start = innerEnd;  e->del1End = innerEnd + 2;
            e->del2Start = caret - 2; e->del2End = caret;
            e->styleStart = caret - 2; e->styleEnd = innerEnd - 2;
            e->newCaret = caret - 2;
            e->resetNormal = 1;
            return 1;
        }
    }
    return 0;
}

/* *italic* -- single '*' that is not part of a '**' run (justTyped == '*'). */
static int DetectItalic(const char *buf, long caret, long lineStart,
                        long lineEnd, MdInlineEdit *e)
{
    long p, q;

    for (p = caret - 2; p >= lineStart; p--) {
        if (buf[p] == '*' &&
            (p == lineStart || buf[p - 1] != '*') &&
            buf[p + 1] != '*' && p + 1 < caret - 1) {
            long innerEnd = caret - 1;

            e->kind = MD_KIND_ITALIC;
            e->del1Start = innerEnd; e->del1End = caret;
            e->del2Start = p;        e->del2End = p + 1;
            e->styleStart = p;       e->styleEnd = innerEnd - 1;
            e->newCaret = innerEnd - 1;
            e->resetNormal = 1;
            return 1;
        }
    }

    /* No opening '*' behind the caret -- the just-typed '*' may instead be an
       OPENING italic delimiter for a closing '*' already later in the line. */
    for (q = caret; q < lineEnd; q++) {
        if (buf[q] == '*' &&
            buf[q - 1] != '*' &&
            (q + 1 == lineEnd || buf[q + 1] != '*') &&
            q > caret) {
            long innerEnd = q;

            e->kind = MD_KIND_ITALIC;
            e->del1Start = innerEnd;  e->del1End = innerEnd + 1;
            e->del2Start = caret - 1; e->del2End = caret;
            e->styleStart = caret - 1; e->styleEnd = innerEnd - 1;
            e->newCaret = caret - 1;
            e->resetNormal = 1;
            return 1;
        }
    }
    return 0;
}

/* `code` -- single '`' delimiter (justTyped == '`'). */
static int DetectCode(const char *buf, long caret, long lineStart,
                      long lineEnd, MdInlineEdit *e)
{
    long p, q;

    /* Three or more consecutive backticks are a code FENCE (a block-level
       feature kept literal, its body styled Monaco at rebuild time), not inline
       `code`. Don't collapse the run -- otherwise typing ``` deletes the outer two
       backticks and leaves the middle one as a stray inline-code span. Mirrors the
       ~~/== 3-run rule; the just-typed backtick is at caret-1. */
    if (caret - 3 >= lineStart &&
        buf[caret - 2] == '`' && buf[caret - 3] == '`')
        return 0;

    for (p = caret - 2; p >= lineStart; p--) {
        if (buf[p] == '`' && p + 1 < caret - 1) {
            long innerEnd = caret - 1;

            e->kind = MD_KIND_CODE;
            e->del1Start = innerEnd; e->del1End = caret;
            e->del2Start = p;        e->del2End = p + 1;
            e->styleStart = p;       e->styleEnd = innerEnd - 1;
            e->newCaret = innerEnd - 1;
            e->resetNormal = 1;
            return 1;
        }
    }

    /* No opening '`' behind the caret -- the just-typed '`' may instead be an
       OPENING code delimiter for a closing '`' already later. */
    for (q = caret; q < lineEnd; q++) {
        if (buf[q] == '`' && q > caret) {
            long innerEnd = q;

            e->kind = MD_KIND_CODE;
            e->del1Start = innerEnd;  e->del1End = innerEnd + 1;
            e->del2Start = caret - 1; e->del2End = caret;
            e->styleStart = caret - 1; e->styleEnd = innerEnd - 1;
            e->newCaret = caret - 1;
            e->resetNormal = 1;
            return 1;
        }
    }
    return 0;
}

/* [text](url) -- closing ')' completes a link (justTyped == ')'). */
static int DetectLink(const char *buf, long caret, long lineStart,
                      MdInlineEdit *e)
{
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
            e->linkURL[0] = (unsigned char) urlLen;
            for (k = 0; k < urlLen; k++)
                e->linkURL[1 + k] = (unsigned char) buf[urlStart + k];

            e->kind = MD_KIND_LINK;
            e->del1Start = closeBracketPos; e->del1End = caret;
            e->del2Start = openBracketPos;  e->del2End = openBracketPos + 1;
            e->styleStart = openBracketPos; e->styleEnd = closeBracketPos - 1;
            e->newCaret = closeBracketPos - 1;
            e->resetNormal = 1;
            return 1;
        }
    }
    return 0;
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

    switch (justTyped) {
    case ' ':
        DetectHeading(buf, caret, lineStart, &e);
        break;
    case '*':
        /* '**' just completed => bold; otherwise a lone '*' => italic. */
        if (!DetectPairDelim(buf, caret, lineStart, lineEnd, '*',
                             MD_KIND_BOLD, &e) &&
            caret >= 3 && buf[caret - 2] != '*')
            DetectItalic(buf, caret, lineStart, lineEnd, &e);
        break;
    case '`':
        DetectCode(buf, caret, lineStart, lineEnd, &e);
        break;
    case '~':
        DetectPairDelim(buf, caret, lineStart, lineEnd, '~', MD_KIND_STRIKE, &e);
        break;
    case '=':
        DetectPairDelim(buf, caret, lineStart, lineEnd, '=', MD_KIND_HIGHLIGHT, &e);
        break;
    case ')':
        DetectLink(buf, caret, lineStart, &e);
        break;
    default:
        break;
    }

    return e;
}

int MdPaginate(const short *lineHeights, int nLines, int pageHeight,
               short *pageStart, int maxPages)
{
    int nPages = 0;
    int line = 0;

    if (nLines <= 0 || maxPages <= 0)
        return 0;
    if (pageHeight < 1)
        pageHeight = 1;                 /* degenerate: one line per page */

    while (line < nLines && nPages < maxPages) {
        long used = 0;

        pageStart[nPages++] = (short) line;

        /* Always place the first line (used == 0), then keep adding lines
           until the next one would overflow the page. This guarantees
           forward progress even for a line taller than the whole page. */
        while (line < nLines) {
            long h = lineHeights[line];
            if (h < 0)
                h = 0;
            if (used != 0 && used + h > (long) pageHeight)
                break;
            used += h;
            line++;
        }
    }

    return nPages;
}

/* ------------------------------------------------------------------------- */
/* Plain-text helpers (ADR 0003). Pure: buffers only, no Toolbox, host-tested.*/
/* ------------------------------------------------------------------------- */

/* Maps a Unicode code point to its MacRoman byte for the characters a writer
   actually meets in imported UTF-8 text -- smart quotes, dashes, ellipsis,
   bullet, and the common accented Latin letters. Returns 0 for anything not in
   the table (the caller substitutes '?'). */
static unsigned char MacRomanForCP(unsigned long cp)
{
    switch (cp) {
    /* General punctuation */
    case 0x2018: return 0xD4;  /* left single quote  */
    case 0x2019: return 0xD5;  /* right single quote / apostrophe */
    case 0x201A: return 0xE2;  /* single low-9 quote */
    case 0x201C: return 0xD2;  /* left double quote  */
    case 0x201D: return 0xD3;  /* right double quote */
    case 0x201E: return 0xE3;  /* double low-9 quote */
    case 0x2013: return 0xD0;  /* en dash */
    case 0x2014: return 0xD1;  /* em dash */
    case 0x2026: return 0xC9;  /* horizontal ellipsis */
    case 0x2022: return 0xA5;  /* bullet */
    case 0x00A0: return 0xCA;  /* no-break space */
    case 0x00AB: return 0xC7;  /* << */
    case 0x00BB: return 0xC8;  /* >> */
    /* Lowercase accented */
    case 0x00E1: return 0x87; case 0x00E0: return 0x88; case 0x00E2: return 0x89;
    case 0x00E4: return 0x8A; case 0x00E3: return 0x8B; case 0x00E5: return 0x8C;
    case 0x00E7: return 0x8D; case 0x00E9: return 0x8E; case 0x00E8: return 0x8F;
    case 0x00EA: return 0x90; case 0x00EB: return 0x91; case 0x00ED: return 0x92;
    case 0x00EC: return 0x93; case 0x00EE: return 0x94; case 0x00EF: return 0x95;
    case 0x00F1: return 0x96; case 0x00F3: return 0x97; case 0x00F2: return 0x98;
    case 0x00F4: return 0x99; case 0x00F6: return 0x9A; case 0x00F5: return 0x9B;
    case 0x00FA: return 0x9C; case 0x00F9: return 0x9D; case 0x00FB: return 0x9E;
    case 0x00FC: return 0x9F;
    /* Uppercase accented */
    case 0x00C4: return 0x80; case 0x00C5: return 0x81; case 0x00C7: return 0x82;
    case 0x00C9: return 0x83; case 0x00D1: return 0x84; case 0x00D6: return 0x85;
    case 0x00DC: return 0x86; case 0x00E6: return 0xBE; case 0x00C6: return 0xAE;
    /* Misc common symbols */
    case 0x00A9: return 0xA9;  /* (C) */
    case 0x00AE: return 0xA8;  /* (R) */
    case 0x2122: return 0xAA;  /* TM */
    case 0x00B0: return 0xFB;  /* degree */
    case 0x00A3: return 0xA3;  /* pound */
    case 0x20AC: return 0xDB;  /* euro (MacRoman rev) */
    }
    return 0;
}

/*
    Cleans up a freshly-read text buffer in place and returns the new length
    (which never grows). Strips a UTF-8 BOM, normalizes CRLF and lone LF to the
    Mac CR line ending TextEdit expects, and converts valid UTF-8 sequences to
    MacRoman (known characters via MacRomanForCP, others to '?'). A byte that
    is not part of a valid UTF-8 sequence is passed through unchanged, so text
    that is already MacRoman survives intact.
*/
long MdNormalizeImport(char *buf, long len)
{
    unsigned char *b = (unsigned char *) buf;
    long in = 0, out = 0;

    if (len >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        in = 3;

    while (in < len) {
        unsigned char c = b[in];

        if (c == 0x0D) {                     /* CR (drop a following LF) */
            b[out++] = 0x0D; in++;
            if (in < len && b[in] == 0x0A) in++;
        } else if (c == 0x0A) {              /* lone LF -> CR */
            b[out++] = 0x0D; in++;
        } else if (c < 0x80) {               /* ASCII */
            b[out++] = c; in++;
        } else if (c >= 0xC0) {              /* possible UTF-8 lead byte */
            long seqLen = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            int valid = (in + seqLen <= len);
            long k;

            for (k = 1; valid && k < seqLen; k++)
                if ((b[in + k] & 0xC0) != 0x80)
                    valid = 0;               /* not real UTF-8 continuation */

            if (valid) {
                unsigned long cp = (seqLen == 2) ? (unsigned long)(c & 0x1F)
                                 : (seqLen == 3) ? (unsigned long)(c & 0x0F)
                                                 : (unsigned long)(c & 0x07);
                unsigned char mapped;
                for (k = 1; k < seqLen; k++)
                    cp = (cp << 6) | (unsigned long)(b[in + k] & 0x3F);
                mapped = MacRomanForCP(cp);
                b[out++] = mapped ? mapped : '?';
                in += seqLen;
            } else {
                b[out++] = c; in++;          /* leave already-MacRoman byte */
            }
        } else {                             /* stray 0x80..0xBF: pass through */
            b[out++] = c; in++;
        }
    }
    return out;
}

static char MdLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/*
    Returns the offset of the first occurrence of needle[0..needleLen) in
    hay[0..hayLen) at or after `from`, or -1 if none. Case-insensitive folding
    is ASCII-only (enough for search-as-you-type). Pure.
*/
long MdFind(const char *hay, long hayLen, const char *needle, long needleLen,
            long from, int caseSensitive)
{
    long i, j;

    if (needleLen <= 0 || needleLen > hayLen)
        return -1;
    if (from < 0)
        from = 0;

    for (i = from; i + needleLen <= hayLen; i++) {
        for (j = 0; j < needleLen; j++) {
            char a = hay[i + j], b = needle[j];
            if (!caseSensitive) { a = MdLowerAscii(a); b = MdLowerAscii(b); }
            if (a != b)
                break;
        }
        if (j == needleLen)
            return i;
    }
    return -1;
}

/* Reports whether line[0..len) is a Markdown thematic break: 3+ of one marker
   char ('-', '*' or '_'), spaces/tabs allowed anywhere, nothing else. Pure. */
int MdIsHorizontalRule(const char *line, long len)
{
    char marker = 0;
    long i, count = 0;

    for (i = 0; i < len; i++) {
        char c = line[i];
        if (c == ' ' || c == '\t')
            continue;
        if (c == '-' || c == '*' || c == '_') {
            if (marker == 0)
                marker = c;
            else if (c != marker)
                return 0;          /* markers must all match */
            count++;
        } else {
            return 0;              /* any other character disqualifies */
        }
    }
    return count >= 3 ? 1 : 0;
}

/* Blockquote nesting depth: leading '>' markers, each optionally followed by a
   space, with up to three leading spaces allowed before each. 0 == not a quote. */
int MdBlockquoteDepth(const char *line, long len)
{
    long i = 0;
    int depth = 0;

    for (;;) {
        int spaces = 0;
        while (i < len && line[i] == ' ' && spaces < 3) {
            i++;
            spaces++;
        }
        if (i < len && line[i] == '>') {
            depth++;
            i++;
            if (i < len && line[i] == ' ')
                i++;                  /* optional single space after '>' */
        } else {
            break;
        }
    }
    return depth;
}

/* Fenced-code delimiter: 3+ backticks, up to three leading spaces, and no '`'
   in the trailing info string. 1/0.

   Backticks only -- the tilde fence form (~~~) that GitHub Markdown also allows
   is deliberately NOT supported, so a run of tildes is unambiguously either
   strikethrough (~~word~~) or literal text and can never collide with a code
   fence. Backticks are the common, collision-free fence marker; the tilde
   alternate is rarely used. */
int MdIsCodeFence(const char *line, long len)
{
    long i = 0;
    int spaces = 0, count = 0;

    while (i < len && line[i] == ' ' && spaces < 3) {
        i++;
        spaces++;
    }
    if (i >= len || line[i] != '`')
        return 0;
    while (i < len && line[i] == '`') {
        count++;
        i++;
    }
    if (count < 3)
        return 0;
    /* A backtick info string may not itself contain a backtick (CommonMark):
       "```code`inline" is a line with inline code, not a fence. */
    while (i < len) {
        if (line[i] == '`')
            return 0;
        i++;
    }
    return 1;
}

/* Parses a leading list marker (bullet '-'/'*'/'+', numbered "N."/"N)", or a
   GitHub task checkbox) off line[0..len). See mdcore.h for the field contract. */
MdListInfo MdParseListItem(const char *line, long len)
{
    MdListInfo info;
    long i = 0;
    int indent = 0;

    info.isList = 0;
    info.indent = 0;
    info.markerChars = 0;
    info.ordered = 0;
    info.checkbox = 0;
    info.checked = 0;

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
        indent++;
    }

    if (i < len && (line[i] == '-' || line[i] == '*' || line[i] == '+')) {
        /* A bullet needs a following space -- otherwise "**bold**" or "-x" at
           line start would read as a list. */
        if (i + 1 < len && line[i + 1] == ' ')
            i += 2;
        else
            return info;
    } else if (i < len && line[i] >= '0' && line[i] <= '9') {
        long d = i;
        while (d < len && line[d] >= '0' && line[d] <= '9')
            d++;
        if (d < len && (line[d] == '.' || line[d] == ')') &&
            d + 1 < len && line[d + 1] == ' ') {
            info.ordered = 1;
            i = d + 2;
        } else {
            return info;
        }
    } else {
        return info;
    }

    /* Optional task-list checkbox immediately after a bullet: "[ ] "/"[x] ". */
    if (!info.ordered && i + 3 < len && line[i] == '[' &&
        (line[i + 1] == ' ' || line[i + 1] == 'x' || line[i + 1] == 'X') &&
        line[i + 2] == ']' && line[i + 3] == ' ') {
        info.checkbox = 1;
        info.checked = (line[i + 1] == 'x' || line[i + 1] == 'X') ? 1 : 0;
        i += 4;
    }

    info.isList = 1;
    info.indent = indent;
    info.markerChars = (int) (i - indent);
    return info;
}

/* Counts whitespace-separated words in buf[0..len). Pure. */
long MdWordCount(const char *buf, long len)
{
    long i, count = 0;
    int inWord = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char) buf[i];
        int isSpace = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (isSpace)
            inWord = 0;
        else if (!inWord) {
            count++;
            inWord = 1;
        }
    }
    return count;
}

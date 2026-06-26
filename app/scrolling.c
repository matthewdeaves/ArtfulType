#include "app.h"

/*
    The scrollbar's value is never tracked as an independent counter --
    it's always derived fresh from TextEdit's own destRect vs. viewRect,
    so it can't drift out of sync with where the text actually is. Every
    scroll operation below scrolls by (current real offset - desired
    offset) and then re-reads the real offset afterward, rather than
    trusting an incrementally-adjusted running total.
*/
static short CurrentScrollOffset(TEHandle te)
{
    return (**te).viewRect.top - (**te).destRect.top;
}

static void SyncScrollbarToOffset(void)
{
    short newValue = CurrentScrollOffset(gActiveTE);

    /* SetControlValue always redraws the control, even when the value is
       unchanged -- called every tick, an unguarded call here would redraw
       the scrollbar (and the flicker that comes with it) on every single
       keystroke for no reason. */
    if (newValue != GetControlValue(gScrollBar))
        SetControlValue(gScrollBar, newValue);
}

/*
    TEGetHeight(nLines, 0, te) and the two calls in ScrollCaretIntoView
    below are cumulative-from-line-0 height sums -- the form that's
    proven reliable (see the comment in ScrollCaretIntoView), but O(n)
    in the document's current line count. Calling that on every single
    keystroke is fine on a fast emulator but visibly slows typing down
    on real 68000 hardware as a document grows. These two small caches
    skip the recompute whenever nothing that affects the answer has
    changed since the last call -- the underlying TEGetHeight calls and
    their cumulative-from-0 form are otherwise untouched.

    Invalidated via InvalidateHeightCache(): unconditionally from
    AdjustScrollbar (the full/infrequent path covering style changes,
    zoom, mode switches, undo/redo, save/load -- anything that can
    change a line's height without necessarily changing nLines), and
    from DetectInlineMarkdown's live-typing conversions (markdown.c),
    since those happen within the fast per-keystroke path and can also
    change a line's height (heading conversion) without changing
    nLines.
*/
static short gCachedTotalHeightNLines = -1;
static long gCachedTotalHeight = 0;

static short gCachedCaretLine = -1;
static long gCachedHeightToLine = 0;
static long gCachedHeightToLineNext = 0;

void InvalidateHeightCache(void)
{
    gCachedTotalHeightNLines = -1;
    gCachedCaretLine = -1;
}

/*
    Updates the scrollbar's range/visibility only -- no clamping of the
    current position. Used on the typing path, where ScrollCaretIntoView
    already owns getting the position right; re-deriving maxVal from
    TEGetHeight for a line that's actively growing as you type is exactly
    the kind of thing that could disagree with ScrollCaretIntoView's own
    (separately computed) target by a pixel or two, and clamping on that
    discrepancy every keystroke is what was causing a brief upward jump.
*/
void UpdateScrollbarRange(void)
{
    long textHeight;
    short viewHeight;
    short maxVal;
    Boolean shouldShow;

    if ((**gActiveTE).nLines == gCachedTotalHeightNLines) {
        textHeight = gCachedTotalHeight;
    } else {
        textHeight = TEGetHeight((**gActiveTE).nLines, 0, gActiveTE);
        gCachedTotalHeightNLines = (**gActiveTE).nLines;
        gCachedTotalHeight = textHeight;
    }
    viewHeight = (**gActiveTE).viewRect.bottom - (**gActiveTE).viewRect.top;

    maxVal = (textHeight > viewHeight) ? (short) (textHeight - viewHeight) : 0;

    if (maxVal != GetControlMaximum(gScrollBar))
        SetControlMaximum(gScrollBar, maxVal);

    shouldShow = (maxVal > 0);
    if (shouldShow != gScrollBarVisible) {
        if (shouldShow)
            ShowControl(gScrollBar);
        else
            HideControl(gScrollBar);
        gScrollBarVisible = shouldShow;
    }
}

/*
    Full version: also clamps the current scroll position if it now
    exceeds the (possibly shrunk) range. Needed after anything that can
    reduce content height -- Style commands, zoom, load/new, mode switch
    -- but not after plain typing, which only ever grows it.
*/
void AdjustScrollbar(void)
{
    short maxVal;
    short curOffset;

    InvalidateHeightCache();
    UpdateScrollbarRange();

    maxVal = GetControlMaximum(gScrollBar);
    curOffset = CurrentScrollOffset(gActiveTE);
    if (curOffset > maxVal)
        TEScroll(0, curOffset - maxVal, gActiveTE);
    else if (curOffset < 0)
        TEScroll(0, curOffset, gActiveTE);

    SyncScrollbarToOffset();
}

/* lineStarts[] is sorted, so the line containing pos is found with a
   binary search instead of a linear scan -- same result, no behavior
   change, just faster for documents with many lines. */
static short LineContaining(TEHandle te, short pos)
{
    short low = 0;
    short high = (**te).nLines - 1;

    while (low < high) {
        short mid = low + (high - low + 1) / 2;

        if ((**te).lineStarts[mid] <= pos)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

void ScrollCaretIntoView(void)
{
    short caretLine;
    long heightToLine, heightToLineNext;
    short lineTop, lineBottom;
    short viewTop, viewBottom;

    caretLine = LineContaining(gActiveTE, (**gActiveTE).selEnd);

    /* Querying a single line's height in isolation (e.g. TEGetHeight
       for just [caretLine, caretLine+1)) comes back unreliable right
       after Enter creates a new, still-empty line -- it hasn't
       "settled" with any content yet. (**te).lineHeight turned out
       to have the same problem, returning a stale/wrong value rather
       than tracking the actual current font size. Avoid isolated
       single-line queries entirely: always sum cumulatively from the
       very start of the document, the same pattern already proven
       reliable in UpdateScrollbarRange's TEGetHeight(nLines, 0, ...).
       Cached below (see InvalidateHeightCache) since this is otherwise
       an O(n) call on every keystroke -- the raw heights are cached
       rather than the final lineTop/lineBottom, since those also
       depend on destRect.top, which changes on scroll. */
    if (caretLine == gCachedCaretLine) {
        heightToLine = gCachedHeightToLine;
        heightToLineNext = gCachedHeightToLineNext;
    } else {
        heightToLine = TEGetHeight(caretLine, 0, gActiveTE);
        heightToLineNext = TEGetHeight(caretLine + 1, 0, gActiveTE);
        gCachedCaretLine = caretLine;
        gCachedHeightToLine = heightToLine;
        gCachedHeightToLineNext = heightToLineNext;
    }
    lineTop = (**gActiveTE).destRect.top + heightToLine;
    lineBottom = (**gActiveTE).destRect.top + heightToLineNext;

    viewTop = (**gActiveTE).viewRect.top;
    viewBottom = (**gActiveTE).viewRect.bottom;

    if (lineBottom > viewBottom)
        TEScroll(0, viewBottom - lineBottom, gActiveTE);
    else if (lineTop < viewTop)
        TEScroll(0, viewTop - lineTop, gActiveTE);

    SyncScrollbarToOffset();
}

static pascal void ScrollAction(ControlHandle control, short part)
{
    short max, delta, desired;
    short pageSize;

    if (part == 0)
        return;

    max = GetControlMaximum(control);
    pageSize = (**gActiveTE).viewRect.bottom - (**gActiveTE).viewRect.top;

    switch (part) {
        case inUpButton:   delta = -16; break;
        case inDownButton: delta = 16; break;
        case inPageUp:     delta = -pageSize; break;
        case inPageDown:   delta = pageSize; break;
        default:           delta = 0; break;
    }

    desired = CurrentScrollOffset(gActiveTE) + delta;
    if (desired < 0) desired = 0;
    if (desired > max) desired = max;

    TEScroll(0, CurrentScrollOffset(gActiveTE) - desired, gActiveTE);
    SetControlValue(control, CurrentScrollOffset(gActiveTE));
}

void DoScrollClick(Point pt)
{
    ControlHandle control;
    short part;
    short desired;

    part = FindControl(pt, gWindow, &control);
    if (part == 0 || control != gScrollBar)
        return;

    if (part == inThumb) {
        TrackControl(gScrollBar, pt, NULL);
        desired = GetControlValue(gScrollBar);
        TEScroll(0, CurrentScrollOffset(gActiveTE) - desired, gActiveTE);
        SyncScrollbarToOffset();
    } else {
        TrackControl(gScrollBar, pt, NewControlActionUPP(ScrollAction));
    }
}

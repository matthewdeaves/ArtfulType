#include "app.h"

static void FreeSnapshot(UndoSnapshot *snap)
{
    if (snap->textH != NULL)
        DisposeHandle(snap->textH);
    snap->textH = NULL;
}

void ClearUndoRedoStacks(void)
{
    short i;

    for (i = 0; i < gUndoCount; i++)
        FreeSnapshot(&gUndoStack[i]);
    gUndoCount = 0;
    for (i = 0; i < gRedoCount; i++)
        FreeSnapshot(&gRedoStack[i]);
    gRedoCount = 0;
    gTypingRunActive = false;
}

void UpdateEditMenuState(void)
{
    EnableItem(gEditMenu, iUndo);
    EnableItem(gEditMenu, iRedo);
    if (gUndoCount == 0)
        DisableItem(gEditMenu, iUndo);
    if (gRedoCount == 0)
        DisableItem(gEditMenu, iRedo);
}

/*
    Captures gTE's canonical text (syncing first if Writer mode is active)
    onto `stack`, evicting the oldest entry when the stack is full so it never
    grows past MAX_UNDO_LEVELS. Returns false, touching nothing, if the
    snapshot buffer can't be allocated. Shared by the undo and redo pushes,
    which differ only in which stack they target and what they do afterward.
*/
static Boolean CaptureSnapshot(UndoSnapshot stack[], short *count)
{
    UndoSnapshot *slot;
    Handle textH;
    long len;
    short i;

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    len = (**gTE).teLength;
    textH = NewHandle(len);
    if (textH == NULL)
        return false;
    HLock(textH);
    HLock((**gTE).hText);
    BlockMove(*(**gTE).hText, *textH, len);
    HUnlock((**gTE).hText);
    HUnlock(textH);

    if (*count == MAX_UNDO_LEVELS) {
        FreeSnapshot(&stack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++)
            stack[i] = stack[i + 1];
        (*count)--;
    }

    slot = &stack[(*count)++];
    slot->textH = textH;
    slot->length = len;
    slot->selStart = (**gActiveTE).selStart;
    slot->selEnd = (**gActiveTE).selEnd;
    return true;
}

/*
    Captures the current document onto the undo stack and clears the redo
    stack -- any genuine new edit invalidates whatever could have been redone.
    A failed capture leaves both stacks and the menu untouched.
*/
void PushUndoSnapshot(void)
{
    short i;

    if (!CaptureSnapshot(gUndoStack, &gUndoCount))
        return;

    for (i = 0; i < gRedoCount; i++)
        FreeSnapshot(&gRedoStack[i]);
    gRedoCount = 0;

    UpdateEditMenuState();
}

/* Same idea, onto the redo stack -- called right before undoing so redoing
   can bring the undone state back. Deliberately does NOT clear the undo stack
   or touch the menu (unlike PushUndoSnapshot): a redo push isn't a new edit. */
static void PushRedoSnapshot(void)
{
    CaptureSnapshot(gRedoStack, &gRedoCount);
}

/* Replaces gTE's text with a snapshot and, if Writer mode is active,
   rebuilds gHiddenTE from it so styling comes back correctly. Doesn't
   free the snapshot -- the caller (DoUndo/DoRedo) owns that. */
static void RestoreSnapshot(UndoSnapshot *snap)
{
    Rect savedViewRect;

    SuppressDrawing(gTE, &savedViewRect);
    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    HLock(snap->textH);
    TEInsert(*snap->textH, snap->length, gTE);
    HUnlock(snap->textH);
    RestoreDrawing(gTE, &savedViewRect);

    if (gHideMarkdown) {
        BuildHiddenView();
        TESetSelect(snap->selStart, snap->selEnd, gHiddenTE);
    } else {
        ClearStyles();
        TESetSelect(snap->selStart, snap->selEnd, gTE);
    }

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

void DoUndo(void)
{
    UndoSnapshot snap;

    if (gUndoCount == 0)
        return;

    PushRedoSnapshot();

    gUndoCount--;
    snap = gUndoStack[gUndoCount];
    RestoreSnapshot(&snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

void DoRedo(void)
{
    UndoSnapshot snap;

    if (gRedoCount == 0)
        return;

    /* Take the redo entry before pushing onto undo -- PushUndoSnapshot
       unconditionally clears the redo stack (correct for a genuine new
       edit, but redoing isn't one; grab what's needed first). */
    gRedoCount--;
    snap = gRedoStack[gRedoCount];

    PushUndoSnapshot();

    RestoreSnapshot(&snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

/*
    Builds a 'TEXT' scrap image of gActiveTE[selStart,selEnd): the canonical
    markdown for the selection in Writer mode (delimiters re-added), or the raw
    bytes in Markdown mode. Returns NULL on allocation failure. Shared by DoCut
    and DoCopy, which differ only in what they do after -- cut also snapshots
    for undo and deletes the selection.
*/
static Handle EncodeSelectionForScrap(short selStart, short selEnd)
{
    Handle scrapText;
    Handle textH;
    long selLen;

    if (gHideMarkdown)
        return EncodeSelectionAsMarkdown(selStart, selEnd, gActiveTE);

    selLen = selEnd - selStart;
    scrapText = NewHandle(selLen);
    if (scrapText != NULL) {
        textH = (**gActiveTE).hText;
        HLock(textH);
        HLock(scrapText);
        BlockMove(*textH + selStart, *scrapText, selLen);
        HUnlock(textH);
        HUnlock(scrapText);
    }
    return scrapText;
}

/* Replaces the desk scrap with scrapText's bytes as 'TEXT', then frees it. */
static void PutHandleToScrap(Handle scrapText)
{
    ZeroScrap();
    HLock(scrapText);
    PutScrap(GetHandleSize(scrapText), 'TEXT', *scrapText);
    HUnlock(scrapText);
    DisposeHandle(scrapText);
}

void DoCut(void)
{
    short selStart = (**gActiveTE).selStart;
    short selEnd = (**gActiveTE).selEnd;
    Handle scrapText;

    if (selStart == selEnd)
        return;

    scrapText = EncodeSelectionForScrap(selStart, selEnd);
    if (scrapText == NULL)
        return;

    PushUndoSnapshot();
    PutHandleToScrap(scrapText);
    TEDelete(gActiveTE);

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();

    /* A Writer-mode cut can shift or remove a struck run; TEDelete repaints
       the affected line as plain text, so force a content repaint to let
       DrawStruckRuns (in the update path) restore the strike overlay --
       mirrors DoPaste. */
    if (gHideMarkdown)
        InvalRect(&gWindow->portRect);
}

void DoCopy(void)
{
    short selStart = (**gActiveTE).selStart;
    short selEnd = (**gActiveTE).selEnd;
    Handle scrapText;

    if (selStart == selEnd)
        return;

    scrapText = EncodeSelectionForScrap(selStart, selEnd);
    if (scrapText == NULL)
        return;

    PutHandleToScrap(scrapText);
}

void DoPaste(void)
{
    Handle scrapH;
    long offset;
    long len;

    scrapH = NewHandle(0);
    if (scrapH == NULL)
        return;
    len = GetScrap(scrapH, 'TEXT', &offset);
    if (len <= 0) {
        DisposeHandle(scrapH);
        return;
    }

    if (!DocCanGrowBy(gActiveTE, len)) {
        DisposeHandle(scrapH);
        ShowError("\pThere isn't room to paste this much text.");
        return;
    }

    PushUndoSnapshot();

    if (gHideMarkdown) {
        InsertMarkdownAsStyled(scrapH, len, gActiveTE);
        DisposeHandle(scrapH);
    } else {
        HLock(scrapH);
        TEInsert(*scrapH, len, gActiveTE);
        HUnlock(scrapH);
        DisposeHandle(scrapH);
    }

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();

    /* A Writer-mode paste can bring in struck runs; force a content repaint so
       DrawStruckRuns (in the update path) paints their lines. */
    if (gHideMarkdown)
        InvalRect(&gWindow->portRect);
}

void DoSelectAll(void)
{
    TESetSelect(0, 32767, gActiveTE);
    gTypingRunActive = false;
}

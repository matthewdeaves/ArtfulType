#include "app.h"

/* Presents a one-button error alert with `msg` (a Pascal string) as its
   text. Restores the arrow cursor first, in case a watch cursor was up
   when the failure occurred. */
void ShowError(StringPtr msg)
{
    ParamText(msg, "\p", "\p", "\p");
    InitCursor();
    Alert(kErrorAlert, NULL);
}

/* True if inserting addLen characters (replacing the current selection)
   would keep the document within kMaxTELength. See app.h. */
Boolean DocCanGrowBy(TEHandle te, long addLen)
{
    long selLen = (long) (**te).selEnd - (long) (**te).selStart;
    return (Boolean) ((long) (**te).teLength - selLen + addLen <= kMaxTELength);
}

static void RefreshActiveView(void)
{
    if (gHideMarkdown)
        BuildHiddenView();
    else
        ClearStyles();
}

void SetViewMode(Boolean hideMarkdown)
{
    if (hideMarkdown == gHideMarkdown)
        return;

    ClearUndoRedoStacks();
    UpdateEditMenuState();
    TEDeactivate(gActiveTE);

    if (hideMarkdown) {
        BuildHiddenView();
        gActiveTE = gHiddenTE;
    } else {
        SyncHiddenToCanonical();
        gActiveTE = gTE;
    }

    TEActivate(gActiveTE);
    gHideMarkdown = hideMarkdown;
    CheckItem(gViewMenu, iMarkdownView, !hideMarkdown);
    CheckItem(gViewMenu, iWriterView, hideMarkdown);
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

/* Writes gTE's canonical text to disk, returning the first OSErr that
   occurs (noErr on success). Every File Manager call is checked so callers
   can report failure instead of silently losing data. */
static OSErr WriteFile(StringPtr name, short vRefNum)
{
    short refNum;
    long count;
    Handle textH = (**gTE).hText;
    OSErr err;
    OSErr closeErr;

    /* Create fails harmlessly with dupFNErr if the file already exists; a
       real problem (locked/full volume) surfaces at FSOpen below. */
    Create(name, vRefNum, 'ArtT', 'TEXT');

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return err;

    count = (**gTE).teLength;
    HLock(textH);
    err = FSWrite(refNum, &count, *textH);
    HUnlock(textH);

    /* Trim any leftover tail from a previously longer version, but only
       after a successful write -- the old code truncated to 0 up front,
       destroying the prior contents even when the write then failed. */
    if (err == noErr)
        err = SetEOF(refNum, count);

    closeErr = FSClose(refNum);
    if (err == noErr)
        err = closeErr;
    if (err == noErr)
        err = FlushVol(NULL, vRefNum);

    return err;
}

/* Loads a file into gTE, returning true on success. On any failure the
   document is left untouched and false is returned, so callers must not
   commit gFileName/gVRefNum/gHaveFile until this succeeds -- otherwise a
   later Save would overwrite the file that was just refused. */
static Boolean ReadFile(StringPtr name, short vRefNum)
{
    short refNum;
    long count;
    long eof;
    Handle textH;
    OSErr err;

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return false;

    GetEOF(refNum, &eof);
    if (eof > kMaxTELength) {
        FSClose(refNum);
        ShowError("\pThis document is too large to open in ArtfulType.");
        return false;
    }
    textH = NewHandle(eof);
    if (textH == NULL) {
        FSClose(refNum);
        return false;
    }
    HLock(textH);
    count = eof;
    FSRead(refNum, &count, *textH);
    FSClose(refNum);

    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    TEInsert(*textH, count, gTE);
    HUnlock(textH);
    DisposeHandle(textH);

    gDirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
    return true;
}

void DoStartupOpen(void)
{
    short message, count;
    AppFile theFile;

    CountAppFiles(&message, &count);
    if (count < 1 || message != appOpen)
        return;

    GetAppFiles(1, &theFile);
    if (ReadFile(theFile.fName, theFile.vRefNum)) {
        BlockMove(theFile.fName, gFileName, theFile.fName[0] + 1);
        gVRefNum = theFile.vRefNum;
        gHaveFile = true;
    }
    ClrAppFiles(1);
}

Boolean DoSaveAs(void)
{
    SFReply reply;
    Point where = {100, 100};

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    SFPutFile(where, "\pSave document as:", "\pUntitled.md", NULL, &reply);
    if (!reply.good)
        return false;

    BlockMove(reply.fName, gFileName, reply.fName[0] + 1);
    gVRefNum = reply.vRefNum;
    gHaveFile = true;
    if (WriteFile(gFileName, gVRefNum) != noErr) {
        ShowError("\pThe document could not be saved.");
        return false;
    }
    gDirty = false;
    return true;
}

Boolean DoSave(void)
{
    if (!gHaveFile)
        return DoSaveAs();

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    if (WriteFile(gFileName, gVRefNum) != noErr) {
        ShowError("\pThe document could not be saved.");
        return false;
    }
    gDirty = false;
    return true;
}

static short AskSaveChanges(void)
{
    short hit;

    if (gHaveFile)
        ParamText(gFileName, "\p", "\p", "\p");
    else
        ParamText("\pUntitled", "\p", "\p", "\p");

    hit = Alert(kSaveChangesAlert, NULL);
    return hit;
}

Boolean ConfirmDiscardChanges(void)
{
    if (!gDirty)
        return true;

    switch (AskSaveChanges()) {
        case kSaveBtn:     return DoSave();
        case kDontSaveBtn: return true;
        default:            return false;
    }
}

Boolean DoOpenFile(void)
{
    SFReply reply;
    Point where = {100, 100};
    SFTypeList types;

    types[0] = 'TEXT';

    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);
    if (!reply.good)
        return false;

    if (!ReadFile(reply.fName, reply.vRefNum))
        return false;
    BlockMove(reply.fName, gFileName, reply.fName[0] + 1);
    gVRefNum = reply.vRefNum;
    gHaveFile = true;
    return true;
}

void DoNewFile(void)
{
    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    gHaveFile = false;
    gDirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

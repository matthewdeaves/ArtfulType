/*
    Find & Replace and Word Count (ADR 0003). The search itself is the pure,
    host-tested MdFind scan in mdcore; this file is the thin Mac adapter -- it
    runs one modal dialog, then acts on the active TextEdit view's text. Working
    on gActiveTE keeps a single coordinate system in both Writer and Markdown
    mode: in Writer mode you search and replace the readable (stripped) text, and
    the canonical Markdown is re-derived from the styled view on the next
    save/mode-switch, exactly as for ordinary typed edits.
*/
#include "app.h"

static Str255 gFindStr = "\p";
static Str255 gReplaceStr = "\p";
static Boolean gFindCase = false;

static void PStrCopy(Str255 dst, ConstStr255Param src)
{
    BlockMove(src, dst, (long) src[0] + 1);
}

static void PStrCat(Str255 dst, ConstStr255Param src)
{
    short n = src[0];
    if ((short) dst[0] + n > 255)
        n = 255 - dst[0];
    BlockMove(src + 1, dst + (long) dst[0] + 1, n);
    dst[0] = (unsigned char) (dst[0] + n);
}

/* Select the next occurrence of gFindStr from the caret (optionally wrapping to
   the top). Returns true on a hit. */
static Boolean DoFindNext(Boolean wrap)
{
    TEHandle te = gActiveTE;
    Handle htext;
    long pos;

    if (gFindStr[0] == 0)
        return false;

    htext = (**te).hText;
    HLock(htext);
    pos = MdFind(*htext, (**te).teLength, (const char *) &gFindStr[1],
                 (long) gFindStr[0], (**te).selEnd, gFindCase);
    if (pos < 0 && wrap)
        pos = MdFind(*htext, (**te).teLength, (const char *) &gFindStr[1],
                     (long) gFindStr[0], 0, gFindCase);
    HUnlock(htext);

    if (pos < 0) {
        SysBeep(1);
        return false;
    }
    TESetSelect((short) pos, (short) (pos + gFindStr[0]), te);
    ScrollCaretIntoView();
    return true;
}

/* Replace every occurrence of gFindStr with gReplaceStr in the active view,
   under one undo snapshot, and report the count. */
static void DoReplaceAll(void)
{
    TEHandle te = gActiveTE;
    long pos = 0, count = 0;
    Str255 msg, num;

    if (gFindStr[0] == 0)
        return;

    SetCursor(*GetCursor(watchCursor));

    for (;;) {
        Handle htext = (**te).hText;
        long p;

        HLock(htext);
        p = MdFind(*htext, (**te).teLength, (const char *) &gFindStr[1],
                   (long) gFindStr[0], pos, gFindCase);
        HUnlock(htext);
        if (p < 0)
            break;
        if (!DocCanGrowBy(te, (long) gReplaceStr[0] - gFindStr[0]))
            break;   /* would exceed the document cap -- stop here */

        /* Take the single undo snapshot lazily, only once there is a real
           replacement to make (a no-op Replace All leaves undo untouched). */
        if (count == 0)
            PushUndoSnapshot();

        TESetSelect((short) p, (short) (p + gFindStr[0]), te);
        TEDelete(te);
        TEInsert(&gReplaceStr[1], (long) gReplaceStr[0], te);
        pos = p + gReplaceStr[0];   /* resume past the inserted text */
        count++;
    }

    InitCursor();
    if (count > 0) {
        gDirty = true;
        InvalidateHeightCache();
        AdjustScrollbar();
        InvalRect(&gWindow->portRect);
    }

    NumToString(count, num);
    PStrCopy(msg, num);
    PStrCat(msg, "\p occurrence(s) replaced.");
    ParamText(msg, "\p", "\p", "\p");
    Alert(kErrorAlert, NULL);
}

/* Return confirms (Find), Escape / Cmd-. cancels. */
static pascal Boolean FindFilter(DialogPtr dlg, EventRecord *ev, short *item)
{
    (void) dlg;
    if (ev->what == keyDown || ev->what == autoKey) {
        unsigned char c = (unsigned char) (ev->message & charCodeMask);
        if (c == kReturnKey || c == kEnterKey) {
            *item = iFindBtn;
            return true;
        }
        if (c == kEscapeKey || ((ev->modifiers & cmdKey) && c == '.')) {
            *item = iFindCancel;
            return true;
        }
    }
    return false;
}

void DoFindReplace(void)
{
    DialogPtr dlg;
    GrafPtr savePort;
    ModalFilterUPP filterUPP;
    short itemHit, type;
    Handle h;
    ControlHandle caseCtl;
    Rect box;

    GetPort(&savePort);
    dlg = GetNewDialog(kFindDialog, NULL, (WindowPtr) -1L);
    SetPort((GrafPtr) dlg);

    /* Prefill from the last search. */
    GetDialogItem(dlg, iFindField, &type, &h, &box);
    SetDialogItemText(h, gFindStr);
    GetDialogItem(dlg, iReplaceField, &type, &h, &box);
    SetDialogItemText(h, gReplaceStr);
    GetDialogItem(dlg, iFindCaseChk, &type, (Handle *) &caseCtl, &box);
    SetControlValue(caseCtl, gFindCase ? 1 : 0);
    SelectDialogItemText(dlg, iFindField, 0, 32767);

    filterUPP = NewModalFilterUPP(FindFilter);
    if (HasSystem7())
        SetDialogDefaultItem(dlg, iFindBtn);
    ShowWindow((WindowPtr) dlg);

    do {
        ModalDialog(filterUPP, &itemHit);
        if (itemHit == iFindCaseChk) {
            GetDialogItem(dlg, iFindCaseChk, &type, (Handle *) &caseCtl, &box);
            SetControlValue(caseCtl, GetControlValue(caseCtl) ? 0 : 1);
        }
    } while (itemHit != iFindBtn && itemHit != iReplaceAllBtn &&
             itemHit != iFindCancel);

    /* Keep the remembered search on Cancel; only commit the fields on an action. */
    if (itemHit != iFindCancel) {
        GetDialogItem(dlg, iFindField, &type, &h, &box);
        GetDialogItemText(h, gFindStr);
        GetDialogItem(dlg, iReplaceField, &type, &h, &box);
        GetDialogItemText(h, gReplaceStr);
        GetDialogItem(dlg, iFindCaseChk, &type, (Handle *) &caseCtl, &box);
        gFindCase = (Boolean) (GetControlValue(caseCtl) != 0);
    }

    DisposeDialog(dlg);
    DisposeModalFilterUPP(filterUPP);
    SetPort(savePort);

    if (itemHit == iFindBtn)
        DoFindNext(true);
    else if (itemHit == iReplaceAllBtn)
        DoReplaceAll();
}

/* Cmd-G: repeat the last search without reopening the dialog. */
void DoFindAgain(void)
{
    if (gFindStr[0] == 0)
        DoFindReplace();
    else
        DoFindNext(true);
}

/* Report the active view's word and character counts. */
void DoWordCount(void)
{
    TEHandle te = gActiveTE;
    Handle htext = (**te).hText;
    long len = (**te).teLength;
    long words;
    Str255 msg, num;

    HLock(htext);
    words = MdWordCount(*htext, len);
    HUnlock(htext);

    NumToString(words, num);
    PStrCopy(msg, num);
    PStrCat(msg, "\p words, ");
    NumToString(len, num);
    PStrCat(msg, num);
    PStrCat(msg, "\p characters.");
    ParamText(msg, "\p", "\p", "\p");
    Alert(kErrorAlert, NULL);
}

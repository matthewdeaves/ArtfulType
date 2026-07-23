#include "app.h"

/* Zoom levels (point deltas from FONT_SIZE). 12/14/18/24pt have a real
   Times bitmap -- confirmed by reading the FOND resource directly rather
   than assuming. The 30pt level has no native bitmap (24pt is the
   largest this font has) and renders as a scaled enlargement of the
   24pt bitmap instead -- a known, accepted tradeoff for going bigger. */
static short kZoomLevels[] = { -6, -4, 0, 6, 12 };

short CurrentFontSize(void)
{
    return FONT_SIZE + kZoomLevels[gZoomIndex];
}

/*
    Preferences persist in a small resource file in the System Folder --
    deliberately NOT in the application's own resource fork. ArtfulType ships
    from media that is frequently read-only (a write-locked 800K floppy, a
    BlueSCSI image), where a write back into the running application would
    silently fail; it is also simply the wrong place for user preferences. A
    missing or unreadable prefs file just leaves the built-in defaults untouched.

    Location, per OS (ADR 0002):
      System 7  -- the Preferences folder, found with FindFolder + the FSSpec
                   resource calls (all System 7 traps).
      System 6  -- loose in the blessed System Folder, whose working-directory
                   refNum is the low-memory global BootDrive ($0210, read via
                   LMGetBootDrive). Inside Macintosh IV, File Manager: a file
                   that outlives the app "should always create it in the
                   directory containing the system folder... stored in the
                   global variable BootDrive." Every call on this branch
                   (GetVol/SetVol/Create/CreateResFile/OpenResFile/OpenRFPerm)
                   is an original trap, safe on the 68000.
*/

/* FindFolder's "system disk" selector. */
#ifndef kOnSystemDisk
#define kOnSystemDisk (-32768)
#endif

/*
    Opens (optionally creating) the preferences resource file, returning
    its refNum or -1 on any failure. Opening a resource file makes it the
    current one, so every caller saves CurResFile() first and restores it
    (and CloseResFile) when done, leaving the app's own resource chain
    the current one for all the other GetResource callers.
*/
static short OpenPrefsFile(Boolean createIfMissing)
{
    OSErr err;

    if (HasSystem7()) {
        short vRefNum;
        long dirID;
        FSSpec spec;

        err = FindFolder(kOnSystemDisk, kPreferencesFolderType, createIfMissing,
                         &vRefNum, &dirID);
        if (err != noErr)
            return -1;

        err = FSMakeFSSpec(vRefNum, dirID, kPrefsFileName, &spec);
        if (err == fnfErr) {
            if (!createIfMissing)
                return -1;
            FSpCreateResFile(&spec, 'ArtT', 'pref', smSystemScript);
            if (ResError() != noErr)
                return -1;
        } else if (err != noErr) {
            return -1;
        }

        /* Reading (the load path) only needs read access, so it still works
           when the system volume itself is read-only; the save path asks for
           write access to match its intent. */
        return FSpOpenResFile(&spec, createIfMissing ? fsRdWrPerm : fsRdPerm);
    } else {
        /* System 6: no Preferences folder -- the file goes loose in the System
           Folder located via BootDrive. SetVol makes it the default directory
           so the by-name Resource Manager calls act there; restore the caller's
           default volume afterward. */
        short sysVRef = LMGetBootDrive();
        short savedVRef;
        short refNum;

        GetVol(NULL, &savedVRef);
        SetVol(NULL, sysVRef);

        if (createIfMissing) {
            /* Create gives the file a proper type/creator; CreateResFile adds
               the resource fork. Both are harmless (dupFNErr) if it exists. */
            Create(kPrefsFileName, sysVRef, 'ArtT', 'pref');
            CreateResFile(kPrefsFileName);
            refNum = OpenResFile(kPrefsFileName);
        } else {
            /* Read-only so loading still works on a write-locked boot volume. */
            refNum = OpenRFPerm(kPrefsFileName, sysVRef, fsRdPerm);
        }

        SetVol(NULL, savedVRef);
        return refNum;
    }
}

/*
    Loads the whole preferences record into the globals, clamping every field.
    Only fields from a record of the current version are applied; anything
    missing, short, or stale leaves the built-in default in place.
*/
void LoadPrefs(void)
{
    short savedRes = CurResFile();
    short refNum = OpenPrefsFile(false);
    Handle prefH;

    if (refNum == -1)
        return;   /* no prefs file yet -- keep the built-in defaults */

    prefH = Get1Resource(kPrefsResType, kPrefsResID);
    if (prefH != NULL && GetHandleSize(prefH) >= (long) sizeof(AtPrefs)) {
        AtPrefs p;

        HLock(prefH);
        p = *(AtPrefs *) *prefH;
        HUnlock(prefH);

        if (p.version == kPrefsVersion) {
            if (p.windowMode == kWindowModeFullScreen ||
                p.windowMode == kWindowModeWindowed)
                gWindowMode = p.windowMode;
            gHideMarkdown = (Boolean) (p.viewMode != 0);
            if (p.fontChoice >= 0 && p.fontChoice < kNumFontChoices)
                gFontChoice = p.fontChoice;
            if (p.zoomIndex >= 0 && p.zoomIndex < kNumZoomLevels)
                gZoomIndex = p.zoomIndex;
        }
    }

    CloseResFile(refNum);
    UseResFile(savedRes);
}

/* Writes the current globals back to the preferences record, creating or
   resizing it as needed. Silently does nothing on read-only media. */
void SavePrefs(void)
{
    short savedRes = CurResFile();
    short refNum = OpenPrefsFile(true);
    Handle prefH;
    AtPrefs p;

    if (refNum == -1)
        return;   /* couldn't create it (e.g. read-only disk) -- skip */

    p.version = kPrefsVersion;
    p.windowMode = gWindowMode;
    p.viewMode = gHideMarkdown ? 1 : 0;
    p.fontChoice = gFontChoice;
    p.zoomIndex = gZoomIndex;

    prefH = Get1Resource(kPrefsResType, kPrefsResID);
    if (prefH != NULL) {
        SetHandleSize(prefH, (long) sizeof(AtPrefs));
        if (MemError() == noErr) {
            HLock(prefH);
            *(AtPrefs *) *prefH = p;
            HUnlock(prefH);
            ChangedResource(prefH);
            WriteResource(prefH);
        }
    } else {
        prefH = NewHandle((long) sizeof(AtPrefs));
        if (prefH != NULL) {
            HLock(prefH);
            *(AtPrefs *) *prefH = p;
            HUnlock(prefH);
            AddResource(prefH, kPrefsResType, kPrefsResID, "\p");
            if (ResError() == noErr)
                WriteResource(prefH);
            else
                DisposeHandle(prefH);  /* not owned by the map -- free it */
        }
    }

    CloseResFile(refNum);
    UseResFile(savedRes);
}

/*
    Body-font choices for the Font preference. Times (index 0) is the default
    and the fallback for any font that isn't installed on the running system.
    Geneva and New York ship on every stock System 6/7. BodyFontNum resolves the
    current choice to a real font number, validating with GetFNum so an absent
    font degrades to Times rather than a wrong/garbage id.
*/
static const unsigned char *kFontChoiceNames[kNumFontChoices] = {
    (const unsigned char *) "\pTimes",
    (const unsigned char *) "\pGeneva",
    (const unsigned char *) "\pNew York"
};

ConstStr255Param FontChoiceName(short choice)
{
    if (choice < 0 || choice >= kNumFontChoices)
        choice = 0;
    return kFontChoiceNames[choice];
}

short BodyFontNum(void)
{
    short fontNum = 0;

    GetFNum(FontChoiceName(gFontChoice), &fontNum);
    if (fontNum == 0 && gFontChoice != 0)
        GetFNum(FontChoiceName(0), &fontNum);  /* fall back to Times */
    return fontNum;
}

/*
    Remaps any run whose size matches one of the OLD base/heading sizes
    to the corresponding NEW size, in place -- used for zoom, so it
    never re-parses markdown and can't clobber unsynced edits in
    whichever buffer isn't currently canonical.
*/
static void RescaleStyles(TEHandle te, short oldBase, short newBase)
{
    long len = (**te).teLength;
    long i = 0;
    short savedStart = (**te).selStart;
    short savedEnd = (**te).selEnd;

    while (i < len) {
        TextStyle st;
        short lh, fa;
        long runStart = i;
        short oldSize;
        short newSize;

        TEGetStyle((short) i, &st, &lh, &fa, te);
        oldSize = st.tsSize;

        while (i < len) {
            TextStyle st2;

            TEGetStyle((short) i, &st2, &lh, &fa, te);
            if (st2.tsSize != oldSize)
                break;
            i++;
        }

        if (oldSize == oldBase) newSize = newBase;
        else if (oldSize == oldBase + 12) newSize = newBase + 12;
        else if (oldSize == oldBase + 8) newSize = newBase + 8;
        else if (oldSize == oldBase + 4) newSize = newBase + 4;
        else newSize = oldSize + (newBase - oldBase);

        if (newSize != oldSize) {
            TextStyle ts;

            ts.tsSize = newSize;
            TESetSelect((short) runStart, (short) i, te);
            TESetStyle(doSize, &ts, true, te);
        }
    }

    TESetSelect(savedStart, savedEnd, te);
}

static void ApplyZoomIndex(short newIndex)
{
    short oldBase;
    short newBase;

    if (newIndex < 0 || newIndex >= kNumZoomLevels || newIndex == gZoomIndex)
        return;

    oldBase = CurrentFontSize();
    gZoomIndex = newIndex;
    newBase = CurrentFontSize();

    ClearStyles();
    RescaleStyles(gHiddenTE, oldBase, newBase);
    SavePrefs();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

void DoZoom(short direction)
{
    ApplyZoomIndex(gZoomIndex + direction);
}

void DoZoomReset(void)
{
    ApplyZoomIndex(kZoomBaselineIndex);
}

/* ------------------------------------------------------------------------- */
/* Preferences dialog (ADR 0002).                                            */
/*                                                                           */
/* Four settings, each a pop-up menu drawn as a dialog userItem and driven   */
/* by PopUpMenuSelect. This is the System-6-safe way to do pop-ups: the      */
/* standard pop-up control CDEF is a System 7 addition and would crash the   */
/* Mac SE, whereas PopUpMenuSelect is on the SE-class ROM / System 4.1+.      */
/* ------------------------------------------------------------------------- */

#define kPrefMenuBase 256   /* pop-up menu IDs; clear of the app menus (1..131) */

/* Working copy while the dialog is open: [0]=window mode, [1]=view (1=Writer),
   [2]=font choice, [3]=zoom index. The userItem draw proc reads these. */
static short gPrefVals[4];
static MenuHandle gPrefMenus[4];

static const short kPrefPopupItems[4] = {
    iPrefWindowPopup, iPrefViewPopup, iPrefFontPopup, iPrefZoomPopup
};

/* Dialog item number -> 0..3 popup index, or -1 if the item isn't a popup. */
static short PrefPopupIndex(short item)
{
    short i;
    for (i = 0; i < 4; i++)
        if (kPrefPopupItems[i] == item)
            return i;
    return -1;
}

static void PStrCopy(Str255 dst, ConstStr255Param src)
{
    BlockMove(src, dst, (long) src[0] + 1);
}

/* The display text for value `val` of popup `which`. Also supplies the menu
   item labels, so the box and its menu always agree. */
static void PrefPopupText(short which, short val, Str255 out)
{
    switch (which) {
    case 0:
        PStrCopy(out, val == kWindowModeWindowed
                      ? (ConstStr255Param) "\pWindowed"
                      : (ConstStr255Param) "\pFull Screen");
        break;
    case 1:
        PStrCopy(out, val == 1 ? (ConstStr255Param) "\pWriter"
                               : (ConstStr255Param) "\pMarkdown");
        break;
    case 2:
        PStrCopy(out, FontChoiceName(val));
        break;
    default: {  /* zoom: show the resulting point size */
        Str255 num;
        NumToString((long) (FONT_SIZE + kZoomLevels[val]), num);
        PStrCopy(out, num);
        BlockMove((Ptr) " point", (Ptr) (out + (long) out[0] + 1), 6);
        out[0] = (unsigned char) (out[0] + 6);
        break;
    }
    }
}

/* userItem proc: frame the box, draw the current choice and a pop-up marker. */
static pascal void PrefPopupDraw(DialogPtr dlg, short item)
{
    short which = PrefPopupIndex(item);
    short type, ax, ay, i;
    Handle h;
    Rect box;
    Str255 label;

    if (which < 0)
        return;
    GetDialogItem(dlg, item, &type, &h, &box);

    EraseRect(&box);
    FrameRect(&box);
    /* 1-pixel drop shadow on the right and bottom edges. */
    MoveTo(box.left + 2, box.bottom);
    LineTo(box.right, box.bottom);
    MoveTo(box.right, box.top + 2);
    LineTo(box.right, box.bottom);

    /* label is filled by PrefPopupText (via PStrCopy) before DrawString reads
       it; cppcheck can't see the write through the BlockMove and flags a false
       uninitvar, the same pattern as find.c's PStrCopy sites. */
    /* cppcheck-suppress uninitvar ; label is written by PrefPopupText */
    PrefPopupText(which, gPrefVals[which], label);
    MoveTo(box.left + 8, box.bottom - 6);
    DrawString(label);

    /* Downward triangle near the right edge. */
    ax = box.right - 16;
    ay = (box.top + box.bottom) / 2 - 2;
    for (i = 0; i < 3; i++) {
        MoveTo(ax - (2 - i), ay + i);
        LineTo(ax + (2 - i), ay + i);
    }
}

/* Return confirms, Escape / Cmd-. cancels. */
static pascal Boolean PrefFilter(DialogPtr dlg, EventRecord *ev, short *item)
{
    (void) dlg;
    if (ev->what == keyDown || ev->what == autoKey) {
        unsigned char c = (unsigned char) (ev->message & charCodeMask);
        if (c == kReturnKey || c == kEnterKey) {
            *item = iPrefOK;
            return true;
        }
        if (c == kEscapeKey ||
            ((ev->modifiers & cmdKey) && c == '.')) {
            *item = iPrefCancel;
            return true;
        }
    }
    return false;
}

void DoPreferences(void)
{
    DialogPtr dlg;
    GrafPtr savePort;
    UserItemUPP drawUPP;
    ModalFilterUPP filterUPP;
    short itemHit, i, type;
    Handle h;
    Rect box;
    Boolean fontChanged;

    /* Working copy of the current defaults. */
    gPrefVals[0] = gWindowMode;
    gPrefVals[1] = gHideMarkdown ? 1 : 0;
    gPrefVals[2] = gFontChoice;
    gPrefVals[3] = gZoomIndex;

    /* Build the four pop-up menus, then insert them in the pop-up/submenu
       portion of the menu list so PopUpMenuSelect can find them. */
    gPrefMenus[0] = NewMenu(kPrefMenuBase + 0, "\p");
    AppendMenu(gPrefMenus[0], "\pFull Screen;Windowed");
    gPrefMenus[1] = NewMenu(kPrefMenuBase + 1, "\p");
    AppendMenu(gPrefMenus[1], "\pMarkdown;Writer");
    gPrefMenus[2] = NewMenu(kPrefMenuBase + 2, "\p");
    for (i = 0; i < kNumFontChoices; i++)
        AppendMenu(gPrefMenus[2], FontChoiceName(i));
    gPrefMenus[3] = NewMenu(kPrefMenuBase + 3, "\p");
    for (i = 0; i < kNumZoomLevels; i++) {
        Str255 lbl;
        /* cppcheck-suppress uninitvar ; lbl is written by PrefPopupText */
        PrefPopupText(3, i, lbl);
        AppendMenu(gPrefMenus[3], lbl);
    }
    for (i = 0; i < 4; i++)
        InsertMenu(gPrefMenus[i], -1);

    GetPort(&savePort);
    dlg = GetNewDialog(kPrefsDialog, NULL, (WindowPtr) -1L);
    SetPort((GrafPtr) dlg);

    drawUPP = NewUserItemUPP(PrefPopupDraw);
    filterUPP = NewModalFilterUPP(PrefFilter);
    for (i = 0; i < 4; i++) {
        GetDialogItem(dlg, kPrefPopupItems[i], &type, &h, &box);
        SetDialogItem(dlg, kPrefPopupItems[i], type, (Handle) drawUPP, &box);
    }
    if (HasSystem7())
        SetDialogDefaultItem(dlg, iPrefOK);
    ShowWindow((WindowPtr) dlg);

    do {
        short which;

        ModalDialog(filterUPP, &itemHit);
        which = PrefPopupIndex(itemHit);
        if (which >= 0) {
            Point where;
            long res;

            GetDialogItem(dlg, itemHit, &type, &h, &box);
            where.h = box.left;
            where.v = box.top;
            LocalToGlobal(&where);
            res = PopUpMenuSelect(gPrefMenus[which], where.v, where.h,
                                  (short) (gPrefVals[which] + 1));
            if (LoWord(res) != 0) {
                gPrefVals[which] = (short) (LoWord(res) - 1);
                PrefPopupDraw(dlg, itemHit);
            }
        }
    } while (itemHit != iPrefOK && itemHit != iPrefCancel);

    DisposeDialog(dlg);
    DisposeUserItemUPP(drawUPP);
    DisposeModalFilterUPP(filterUPP);
    for (i = 0; i < 4; i++) {
        DeleteMenu(kPrefMenuBase + i);
        DisposeMenu(gPrefMenus[i]);
    }
    SetPort(savePort);

    if (itemHit != iPrefOK)
        return;

    /* Apply. Order lets each helper see the others' updated globals; a couple
       of settings may re-render more than once when several change at once,
       which is fine for a rarely-used dialog. */
    fontChanged = (Boolean) (gPrefVals[2] != gFontChoice);

    SetViewMode((Boolean) (gPrefVals[1] != 0));
    if (gPrefVals[3] != gZoomIndex)
        ApplyZoomIndex(gPrefVals[3]);
    gFontChoice = gPrefVals[2];
    if (gPrefVals[0] != gWindowMode)
        SetWindowMode(gPrefVals[0]);   /* full rebuild -- adopts the new font */
    else if (fontChanged)
        ApplyDocumentFont();
    SavePrefs();
}

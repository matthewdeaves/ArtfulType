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
    The zoom level persists in a small resource file in the system
    Preferences folder -- deliberately NOT in the application's own
    resource fork. ArtfulType ships from media that is frequently
    read-only (a write-locked 800K floppy, a BlueSCSI image), where a
    write back into the running application would silently fail; it is
    also simply the wrong place for user preferences. A missing or
    unreadable pref just leaves the baseline zoom untouched.
*/
#define kPrefsFileName "\pArtful Type Preferences"

/* FindFolder's "system disk" selector isn't named in the multiversal
   headers; this is its long-standing value. */
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
    short vRefNum;
    long dirID;
    FSSpec spec;

    /* FindFolder and the FSSpec resource calls below are System 7 traps.
       On System 6 (e.g. the Mac SE) they are unimplemented and crash the
       machine, so skip persistence there -- a missing pref just leaves the
       baseline zoom untouched, which is already the documented behavior. */
    if (!HasSystem7())
        return -1;

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
}

void LoadZoomPref(void)
{
    short savedRes = CurResFile();
    short refNum = OpenPrefsFile(false);
    Handle prefH;

    if (refNum == -1)
        return;   /* no prefs file yet -- keep the baseline default */

    prefH = Get1Resource(kZoomPrefType, kZoomPrefID);
    if (prefH != NULL && GetHandleSize(prefH) >= (long) sizeof(short)) {
        HLock(prefH);
        gZoomIndex = *(short *) *prefH;
        HUnlock(prefH);
        if (gZoomIndex < 0 || gZoomIndex >= kNumZoomLevels)
            gZoomIndex = kZoomBaselineIndex;
    }

    CloseResFile(refNum);
    UseResFile(savedRes);
}

static void SaveZoomPref(void)
{
    short savedRes = CurResFile();
    short refNum = OpenPrefsFile(true);
    Handle prefH;

    if (refNum == -1)
        return;   /* couldn't create it (e.g. read-only disk) -- skip */

    prefH = Get1Resource(kZoomPrefType, kZoomPrefID);
    if (prefH != NULL) {
        HLock(prefH);
        *(short *) *prefH = gZoomIndex;
        HUnlock(prefH);
        ChangedResource(prefH);
        WriteResource(prefH);
    } else {
        prefH = NewHandle(sizeof(short));
        if (prefH != NULL) {
            HLock(prefH);
            *(short *) *prefH = gZoomIndex;
            HUnlock(prefH);
            AddResource(prefH, kZoomPrefType, kZoomPrefID, "\p");
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
    SaveZoomPref();
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

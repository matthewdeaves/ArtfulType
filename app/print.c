/*
    print.c -- Page Setup and Print, via the classic Printing Manager.

    The Printing Manager has been a single ROM trap (_PrGlue) since System 4.1,
    so the same code works on the System 6 and System 7 targets; its routines
    are real trap glue, not macros (a draft that guarded them with
    #if defined(PrOpen) compiled to nothing -- that is why it only beeped). The
    only System 7-only Toolbox call here is SetDialogDefaultItem, gated on
    HasSystem7() exactly like the link dialog in markdown.c.

    Design:

    - ONE print record (THPrint) backs both commands, per Inside Macintosh II:
      Page Setup writes page/paper geometry (PrStlDialog), Print writes the job
      (PrJobDialog). It is created lazily and kept for the session only --
      ArtfulType runs from read-only media (a locked floppy / BlueSCSI image)
      where writing a PREC back to disk would silently fail, so we deliberately
      don't persist it (the same reason the zoom pref bails on System 6).

    - The canonical print loop follows IM II exactly, with PrError checked after
      every step and every PrOpenPage/PrOpenDoc balanced by its close. Spooled
      jobs are replayed with PrPicFile; draft jobs already printed in the page
      loop (we gate on prJob.bJDocLoop == bSpoolLoop).

    - The document is a TextEdit record whose lines have VARYING heights
      (headings are larger), so pagination can't be a fixed line count. We
      measure each line's height, hand the list to the pure, host-tested
      MdPaginate (mdcore) to get per-page first-line indices, then draw each
      page by sliding the TE destRect up by the height of the lines above it and
      clipping to the page rectangle (IM II / Inside Macintosh: Text). Crucially
      we keep the destRect's WIDTH equal to the on-screen text column: TEUpdate
      re-wraps to destRect width, so changing it would change the line breaks
      the pagination was measured against. The screen column (~384px on a
      compact Mac) fits inside any printer page, so we only translate the rect.

    - The user chooses at print time whether to print the formatted Writer view
      (gHiddenTE) or the raw Markdown source (gTE); each is only kept live in
      its own mode, so the other direction is rebuilt on demand.
*/
#include "app.h"
#include <Printing.h>

/* The document's print record (see file header). Session-only. */
static THPrint gPrintHdl = NULL;

/*
    Upper bound on pages we plan per job. A document is capped at kMaxTELength
    (20000) characters, so even at a few characters per line it is far short of
    this many pages; MdPaginate never clamps in practice, and if a pathological
    document ever exceeded it the tail simply prints onto the last page.
*/
#define kMaxPrintPages 256
static short gPageStart[kMaxPrintPages];

/* ---- shared helpers ---------------------------------------------------- */

/* Set a dialog radio button's on/off state. */
static void SetRadio(DialogPtr dlg, short item, Boolean on)
{
    DialogItemType type;
    Handle itemH;
    Rect box;

    GetDialogItem(dlg, item, &type, &itemH, &box);
    SetControlValue((ControlHandle) itemH, on ? 1 : 0);
}

/*
    Ensure gPrintHdl exists and is valid for the currently chosen printer. The
    caller must already have called PrOpen (PrintDefault/PrValidate need the
    Printing Manager open). Returns false only if the record can't be allocated
    or the Printing Manager reports an error.
*/
static Boolean EnsurePrintRecord(void)
{
    if (gPrintHdl == NULL) {
        gPrintHdl = (THPrint) NewHandle(sizeof(TPrint));
        if (gPrintHdl == NULL)
            return false;
        PrintDefault(gPrintHdl);
    } else {
        /* An existing record may predate a driver change -- revalidate it.
           (PrStlDialog/PrJobDialog also call PrValidate, but doing it up front
           keeps a stale record from reaching the dialogs.) */
        PrValidate(gPrintHdl);
    }
    return (Boolean) (PrError() == noErr);
}

/* ---- Page Setup -------------------------------------------------------- */

void DoPageSetup(void)
{
    GrafPtr savePort;
    Boolean ok;

    GetPort(&savePort);
    PrOpen();
    ok = (Boolean) (PrError() == noErr) && EnsurePrintRecord();
    if (ok) {
        /* PrStlDialog returns true on OK (record updated, PrValidate run) and
           false on Cancel (record untouched). Either way we keep gPrintHdl for
           the session; there is nowhere to persist it (read-only media). */
        (void) PrStlDialog(gPrintHdl);
    }
    PrClose();
    SetPort(savePort);

    if (!ok)
        ShowError("\pCan't open the printer. Choose a printer in the Chooser, then try again.");
}

/* ---- Print: option dialog --------------------------------------------- */

/* Return/Enter confirms, Escape or Command-period cancels -- the standard
   keyboard shortcuts, and (on System 6, where SetDialogDefaultItem is absent)
   the only way to drive the default/cancel buttons from the keyboard. */
static pascal Boolean PrintOptionsFilter(DialogPtr dlg, EventRecord *evt, short *item)
{
    unsigned char ch;

    (void) dlg;
    if (evt->what != keyDown && evt->what != autoKey)
        return false;

    ch = evt->message & charCodeMask;
    if (ch == kReturnKey || ch == kEnterKey) {
        *item = iPrintOptOK;
        return true;
    }
    if (ch == kEscapeKey || ((evt->modifiers & cmdKey) && ch == '.')) {
        *item = iPrintOptCancel;
        return true;
    }
    return false;
}

/*
    Ask whether to print the formatted Writer rendering or the raw Markdown
    source, defaulting to whatever the user is currently viewing. Returns true
    and sets *outFormatted on Print, false on Cancel.
*/
static Boolean ShowPrintOptions(Boolean *outFormatted)
{
    DialogPtr dlg;
    short item;
    Boolean formatted;
    ModalFilterUPP filter;

    dlg = GetNewDialog(kPrintOptionsDialog, NULL, (WindowPtr) -1L);
    if (dlg == NULL) {
        /* Can't load the dialog: fall back to "print what you see". */
        *outFormatted = gHideMarkdown;
        return true;
    }

    formatted = gHideMarkdown;              /* default = current view mode */
    SetRadio(dlg, iPrintOptFormatted, formatted);
    SetRadio(dlg, iPrintOptSource, (Boolean) !formatted);

    if (HasSystem7()) {
        SetDialogDefaultItem(dlg, iPrintOptOK);
        SetDialogCancelItem(dlg, iPrintOptCancel);
    }

    filter = NewModalFilterUPP(PrintOptionsFilter);
    do {
        ModalDialog(filter, &item);
        if (item == iPrintOptFormatted) {
            formatted = true;
            SetRadio(dlg, iPrintOptFormatted, true);
            SetRadio(dlg, iPrintOptSource, false);
        } else if (item == iPrintOptSource) {
            formatted = false;
            SetRadio(dlg, iPrintOptFormatted, false);
            SetRadio(dlg, iPrintOptSource, true);
        }
    } while (item != iPrintOptOK && item != iPrintOptCancel);
    DisposeModalFilterUPP(filter);

    DisposeDialog(dlg);
    SetPort(gWindow);

    if (item == iPrintOptOK) {
        *outFormatted = formatted;
        return true;
    }
    return false;
}

/* ---- Print: buffer preparation & pagination --------------------------- */

/*
    Return the TextEdit record to print, making sure its contents are current.
    gTE holds canonical Markdown, gHiddenTE the styled Writer view; each is only
    kept live while its own mode is active, so rebuild the other direction on
    demand (both show a watch cursor and restore it).
*/
static TEHandle PreparePrintBuffer(Boolean formatted)
{
    if (formatted) {
        if (!gHideMarkdown)
            BuildHiddenView();          /* Markdown mode: styled view is stale */
        return gHiddenTE;
    } else {
        if (gHideMarkdown)
            SyncHiddenToCanonical();    /* Writer mode: source lags edits */
        return gTE;
    }
}

/*
    Plan page breaks for `te` at its current (on-screen) layout width. Fills
    gPageStart via MdPaginate and returns the page count. Per-line heights come
    from TEGetHeight, which reads the line-height table and so is correct for
    the variable-height headings.
*/
static short PlanPages(TEHandle te)
{
    short nLines = (**te).nLines;
    short pageHeight;
    Handle hHeights;
    short *heights;
    short i, nPages;

    pageHeight = (short) ((**gPrintHdl).prInfo.rPage.bottom
                          - (**gPrintHdl).prInfo.rPage.top);
    if (nLines < 1)
        return 0;

    hHeights = NewHandle((long) nLines * (long) sizeof(short));
    if (hHeights == NULL) {
        /* Out of memory to measure: fall back to a single page. */
        gPageStart[0] = 0;
        return 1;
    }
    HLock(hHeights);
    heights = (short *) *hHeights;
    /* TEGetHeight is a pure query (no allocation), so *hHeights stays put
       across the loop; it is 1-based and inclusive, so (i,i) is one line. */
    for (i = 1; i <= nLines; i++)
        heights[i - 1] = (short) TEGetHeight(i, i, te);

    nPages = (short) MdPaginate(heights, nLines, pageHeight,
                                gPageStart, kMaxPrintPages);
    HUnlock(hHeights);
    DisposeHandle(hHeights);
    return nPages;
}

/*
    Draw one physical page (1-based `pg`) of `te` into the current printing
    grafPort. Slides the TE destRect up by the pixel height of every line above
    this page's first line and clips to the printable page rectangle, so
    TEUpdate lays the whole document down but only this page's lines paint. The
    destRect keeps its on-screen WIDTH (see file header) so no re-wrap occurs.
*/
static void DrawPage(TEHandle te, short pg)
{
    Rect rPage = (**gPrintHdl).prInfo.rPage;
    short firstLine0 = gPageStart[pg - 1];      /* 0-based first line */
    short nLines = (**te).nLines;
    long offsetAbove;
    long totalHeight;
    long origWidth;
    Rect saveDest, saveView;
    RgnHandle saveClip;

    /* Height of all lines above this page's first line (TEGetHeight is 1-based
       inclusive; the 0-based index equals the 1-based index of the last line
       above), and the whole document's height for the destRect bottom. */
    offsetAbove = (firstLine0 > 0) ? TEGetHeight(firstLine0, 1, te) : 0;
    totalHeight = TEGetHeight(nLines, 1, te);

    saveDest = (**te).destRect;
    saveView = (**te).viewRect;
    saveClip = NewRgn();
    GetClip(saveClip);

    /* PrOpenPage fully reinitializes the printing grafPort every page; TextEdit
       draws each run in its own font/size from the style record, so re-assert
       only the pen/colors we rely on. */
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);

    origWidth = (long) saveDest.right - (long) saveDest.left;
    (**te).destRect.left   = rPage.left;
    (**te).destRect.right  = (short) (rPage.left + origWidth);
    (**te).destRect.top    = (short) (rPage.top - offsetAbove);
    (**te).destRect.bottom = (short) (rPage.top - offsetAbove + totalHeight);
    (**te).viewRect = rPage;
    ClipRect(&rPage);

    TEUpdate(&rPage, te);
    /* Highlight and strikethrough aren't TextEdit faces; overpaint them exactly
       like the on-screen path (each a no-op when the buffer carries none), the
       highlight band first so a struck line lands on top of it. Highlight prints
       fine (patOr gray); strike shares its known issue #9 -- the overpaint colour
       can come out wrong on some devices -- but the geometry is correct, so it
       prints when that is fixed and, worst case, is merely invisible now. */
    DrawHighlightRuns(te);
    DrawStruckRuns(te);

    SetClip(saveClip);
    DisposeRgn(saveClip);
    (**te).destRect = saveDest;
    (**te).viewRect = saveView;
}

/* ---- Print ------------------------------------------------------------- */

void DoPrint(void)
{
    GrafPtr    savePort;
    TPPrPort   prPort;
    TPrStatus  prStatus;
    TEHandle   printTE;
    DialogPtr  statusDlg;
    Boolean    printFormatted;
    short      firstPage, lastPage, pg, nPages;
    short      err;

    /* 1. What to print (defaulting to the current view). Cancel aborts. */
    if (!ShowPrintOptions(&printFormatted))
        return;

    /* 2. Make the chosen buffer current, then bail if there is nothing to
          print. PreparePrintBuffer may raise a watch cursor; reset it. */
    printTE = PreparePrintBuffer(printFormatted);
    InitCursor();
    if (printTE == NULL || (**printTE).teLength == 0) {
        ShowError("\pThere is nothing to print.");
        return;
    }

    /* 3. Open the Printing Manager and run the standard Job dialog. */
    GetPort(&savePort);
    PrOpen();
    if (!((PrError() == noErr) && EnsurePrintRecord())) {
        PrClose();
        SetPort(savePort);
        ShowError("\pCan't open the printer. Choose a printer in the Chooser, then try again.");
        return;
    }
    if (!PrJobDialog(gPrintHdl) || PrError() != noErr) {
        PrClose();
        SetPort(savePort);
        return;                         /* user cancelled, or a dialog error */
    }

    /* 4. Take the requested page range, then tell the driver to accept pages
          1..iPrPgMax so OUR loop decides what prints (IM II, "Printing a
          Specified Range of Pages"). */
    firstPage = (**gPrintHdl).prJob.iFstPage;
    lastPage  = (**gPrintHdl).prJob.iLstPage;
    (**gPrintHdl).prJob.iFstPage = 1;
    (**gPrintHdl).prJob.iLstPage = iPrPgMax;

    /* 5. Plan pages for this buffer's actual line heights and clamp the range. */
    nPages = PlanPages(printTE);
    if (firstPage < 1) firstPage = 1;
    if (lastPage > nPages) lastPage = nPages;

    /* 6. Status window so the user knows Command-period cancels. */
    statusDlg = GetNewDialog(kPrintStatusDialog, NULL, (WindowPtr) -1L);
    if (statusDlg != NULL) {
        SetPort(statusDlg);
        DrawDialog(statusDlg);
    }

    /* 7. The print loop. Every PrOpenPage is balanced by PrClosePage and
          PrOpenDoc by PrCloseDoc, even when an error or a Command-period abort
          appears mid-way. */
    prPort = PrOpenDoc(gPrintHdl, NULL, NULL);
    if (PrError() == noErr) {
        for (pg = firstPage; pg <= lastPage; pg++) {
            if (PrError() != noErr)
                break;
            PrOpenPage(prPort, NULL);
            if (PrError() == noErr)
                DrawPage(printTE, pg);
            PrClosePage(prPort);
        }
    }
    PrCloseDoc(prPort);

    /* 8. Spool loop: if the driver spooled (rather than printed in draft during
          the page loop), replay the spool file now. */
    if ((**gPrintHdl).prJob.bJDocLoop == bSpoolLoop && PrError() == noErr)
        PrPicFile(gPrintHdl, NULL, NULL, NULL, &prStatus);

    /* 9. Tear down in the safe order: capture the error, close the Printing
          Manager, restore the caller's port (PrCloseDoc disposed the printing
          port, so thePort is dangling until now), then dispose the status
          window. A Command-period cancel returns iPrAbort -- not alert-worthy. */
    err = PrError();
    PrClose();
    SetPort(savePort);
    if (statusDlg != NULL)
        DisposeDialog(statusDlg);

    if (err != noErr && err != iPrAbort)
        ShowError("\pAn error occurred while printing.");

    /* Printing must leave the on-screen view untouched. Per Inside Macintosh
       (Imaging With QuickDraw, "The Printing Manager"), a printer driver draws
       into its own printing grafPort, not the screen -- but the modal print
       dialogs (options, job, status) do cover the document window, and a driver
       running with no real printer configured can additionally paint the page
       onto it. The Window Manager only re-exposes regions it knows were
       covered, so mark the entire content invalid and let the next DoUpdate
       erase it and redraw the current buffer at its own (restored) rects. The
       result: the Writer or Markdown rendering looks exactly as it did before
       Print. Same repaint idiom used after the modal link dialog (markdown.c). */
    SetPort(gWindow);
    InvalRect(&gWindow->portRect);
}

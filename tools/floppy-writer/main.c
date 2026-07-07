/*
    ArtfulType Floppy Writer
    ------------------------
    A tiny, self-contained classic-Mac app whose only job is to turn a blank
    800K floppy into a bootable ArtfulType (System 6.0.8) disk -- on real
    hardware, with no other software installed.

    Why it exists: LaunchAPPL (Retro68's app launcher) can send an *application*
    to a Mac over the network, but not a data file or a disk image. To make a
    physical bootable floppy on, say, a Mac SE that has nothing but a System and
    the launcher, we embed the whole 800K disk image *inside this app* as a
    resource, ship the app with LaunchAPPL, and let it write the image to an
    inserted floppy sector-for-sector.

    How it works:
      1. The 800K image (dist/ArtfulType-800K.dsk -- itself a known-bootable
         System 6.0.8 volume with ArtfulType added) is embedded as an 'ATdi'
         resource by Rez ($$read, see writer.r.in).
      2. We wait for a disk-inserted event.
      3. We DIFormat the inserted disk to its native 800K GCR layout and DIVerify
         it, then raw-write every logical block of the embedded image straight to
         the .Sony driver with PBWrite (bypassing the File Manager -- we are
         cloning a volume, not mounting one).
      4. We eject. Re-inserting the disk mounts a fresh, bootable ArtfulType
         volume.

    Everything here is System-6-safe (no WaitNextEvent, no System-7-only traps):
    the target is a real 68000 Mac SE running System 6.0.8, where an
    unimplemented trap drops into the debugger rather than no-op'ing. See the
    ArtfulType CLAUDE.md "Known gotchas" for the reasoning behind that rule.

    This is a build/deploy tool, deliberately verbose (it logs each step and
    every Toolbox error code to its window) so it can be iterated on blind, over
    the network, without a screen-share.
*/

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <Resources.h>
#include <Files.h>
#include <Devices.h>
#include <Multiverse.h>
#include <string.h>

/* --- resources (writer.r.in) ------------------------------------------- */
#define kDiskResType   'ATdi'   /* the embedded 800K disk image            */
#define kDiskResID     128
#define kInfoAlert     128      /* one-button message alert (ParamText ^0) */
#define kConfirmAlert  129      /* Erase / Cancel confirmation             */
#define kConfirmErase  1        /* item number of the "Erase" button       */

#define mApple         128
#define iAbout         1
#define mFile          129
#define iQuit          1

/* --- geometry ---------------------------------------------------------- */
#define kWinWidth      460
#define kWinHeight     300
#define kLogLines      9        /* how many log lines the window shows      */
#define kLogTop        76       /* y of the first log line                  */
#define kLogLeading    13

/* 819200 bytes / 512 = 1600 blocks. We write in 32 KB runs (a multiple of
   512) so the window can show a progress bar without a per-sector call. */
#define kChunkBytes    (512L * 64L)

/* --- globals ----------------------------------------------------------- */
static WindowPtr gWindow;
static Boolean   gDone = false;
static Str255    gLog[kLogLines];
static short     gLogCount = 0;
static Boolean   gShowProgress = false;
static short     gProgress = 0;         /* 0..100 */
static Boolean   gBusy = false;         /* re-entrancy guard for disk events */

/* ----------------------------------------------------------------------- */

static void DrawContents(void)
{
    Rect r;
    short i, y;
    Str255 pct;

    r = gWindow->portRect;
    EraseRect(&r);

    TextFont(systemFont);
    TextFace(bold);
    TextSize(0);
    MoveTo(12, 18);
    DrawString("\pArtfulType Floppy Writer");
    TextFace(normal);

    TextFont(kFontIDGeneva);
    TextSize(9);
    MoveTo(12, 38);
    DrawString("\pInsert a blank 800K floppy. It is ERASED and rewritten as a");
    MoveTo(12, 50);
    DrawString("\pbootable System 6.0.8 ArtfulType disk.");

    MoveTo(12, 60);
    LineTo(r.right - 12, 60);

    y = kLogTop;
    for (i = 0; i < gLogCount; i++) {
        MoveTo(12, y);
        DrawString(gLog[i]);
        y += kLogLeading;
    }

    if (gShowProgress) {
        Rect bar, fill;
        SetRect(&bar, 12, r.bottom - 26, r.right - 12, r.bottom - 12);
        FrameRect(&bar);
        fill = bar;
        InsetRect(&fill, 1, 1);
        fill.right = fill.left + ((long)(fill.right - fill.left) * gProgress) / 100;
        PaintRect(&fill);

        NumToString(gProgress, pct);
        pct[++pct[0]] = '%';
        MoveTo((r.right - StringWidth(pct)) / 2, bar.top - 4);
        DrawString(pct);
    }
}

/* Append one line to the on-screen log and redraw immediately. We draw
   synchronously (not via an update event) because the disk write is a long
   synchronous operation that never returns to the event loop mid-way -- an
   InvalRect would not repaint until it was all over. */
static void Log(ConstStr255Param s)
{
    short i;

    if (gLogCount == kLogLines) {
        for (i = 1; i < kLogLines; i++)
            BlockMove(gLog[i], gLog[i - 1], gLog[i][0] + 1);
        gLogCount--;
    }
    BlockMove(s, gLog[gLogCount], s[0] + 1);
    gLogCount++;

    SetPort(gWindow);
    DrawContents();
}

static void SetProgress(short pct)
{
    gShowProgress = true;
    gProgress = pct;
    SetPort(gWindow);
    DrawContents();
}

static void HideProgress(void)
{
    gShowProgress = false;
    SetPort(gWindow);
    DrawContents();
}

static void SetWatch(void)
{
    CursHandle h = GetCursor(watchCursor);
    if (h != NULL)
        SetCursor(*h);
}

/* Map the disk/device error codes we're likely to hit onto plain English, so
   a failure over the network is diagnosable without an Inside Macintosh. The
   raw number is always shown too. */
static void ExplainErr(OSErr err, Str255 out)
{
    ConstStr255Param msg;

    switch (err) {
        case noErr:        msg = "\pno error";                         break;
        case wPrErr:       msg = "\pdisk is write-protected";          break;
        case noDriveErr:   msg = "\pno such drive";                    break;
        case offLinErr:    msg = "\pdrive is off-line";                break;
        case volOnLinErr:  msg = "\pvolume still mounted";             break;
        case paramErr:     msg = "\pbad parameter";                    break;
        case controlErr:   msg = "\pdriver control error";             break;
        case statusErr:    msg = "\pdriver status error";              break;
        case readErr:      msg = "\pdriver read error";                break;
        case writErr:      msg = "\pdriver write error";               break;
        case ioErr:        msg = "\phardware I/O error";               break;
        case memFullErr:   msg = "\pout of memory";                    break;
        default:
            /* The disk-driver error range is lastDskErr(-64)..firstDskErr(-84),
               i.e. firstDskErr <= err <= lastDskErr. */
            if (err >= firstDskErr && err <= lastDskErr)
                msg = "\plow-level disk error (bad or wrong-density media?)";
            else
                msg = "\p";
            break;
    }
    BlockMove(msg, out, msg[0] + 1);
}

/* Report a failure: log it, and put up a Stop alert with the code. */
static void ReportErr(ConstStr255Param what, OSErr err)
{
    Str255 line, num, expl;

    ExplainErr(err, expl);
    NumToString(err, num);

    /* line = what + " (error " + num + ": " + expl + ")" */
    BlockMove(what, line, what[0] + 1);
    { ConstStr255Param a = "\p (error "; BlockMove(a + 1, line + 1 + line[0], a[0]); line[0] += a[0]; }
    BlockMove(num + 1, line + 1 + line[0], num[0]); line[0] += num[0];
    if (expl[0] > 0) {
        ConstStr255Param a = "\p: "; BlockMove(a + 1, line + 1 + line[0], a[0]); line[0] += a[0];
        BlockMove(expl + 1, line + 1 + line[0], expl[0]); line[0] += expl[0];
    }
    { ConstStr255Param a = "\p)"; BlockMove(a + 1, line + 1 + line[0], a[0]); line[0] += a[0]; }

    Log(line);
    InitCursor();
    ParamText(line, "\p", "\p", "\p");
    StopAlert(kInfoAlert, NULL);
}

/* Find the .Sony (or external floppy) driver refNum for a given drive number
   by walking the drive queue. The queue header lives at a fixed low-memory
   address; reading it directly needs no glue and works on every System. */
static short DriverForDrive(short drive)
{
    DrvQEl *dqe = (DrvQEl *) LMGetDrvQHdr().qHead;

    while (dqe != NULL) {
        if (dqe->dQDrive == drive)
            return dqe->dQRefNum;
        dqe = (DrvQEl *) dqe->qLink;
    }
    return 0;
}

/* The heart of it: format the inserted disk and clone the embedded image
   onto it, block for block. */
static void WriteFloppy(short drive)
{
    OSErr    err;
    short    driverRef, vRef;
    long     freeBytes, total, off, chunk;
    Handle   diskH;
    Ptr      base;
    Str255   volName;
    ParamBlockRec pb;

    /* Zero the whole block once so no stale field is ever handed to the
       driver; the loop then only has to set the fields it varies. */
    memset(&pb, 0, sizeof(pb));

    SetCursor(&qd.arrow);
    ParamText("\pThis ERASES the disk in the drive and writes a bootable "
              "ArtfulType (System 6.0.8) floppy. Everything on the disk is lost.",
              "\p", "\p", "\p");
    if (CautionAlert(kConfirmAlert, NULL) != kConfirmErase) {
        Eject(NULL, drive);
        Log("\pCancelled -- disk ejected.");
        return;
    }

    /* If a formatted volume auto-mounted on insertion, take it off-line
       before we format underneath it. A positive value in the vRefNum slot is
       treated as a drive number by the File Manager. */
    err = GetVInfo(drive, volName, &vRef, &freeBytes);
    if (err == noErr) {
        Log("\pUnmounting the existing volume...");
        err = UnmountVol(NULL, vRef);
        if (err != noErr) { ReportErr("\pCould not unmount the disk", err); return; }
    }

    driverRef = DriverForDrive(drive);
    if (driverRef == 0) {
        ReportErr("\pCould not find the floppy driver for that drive", paramErr);
        return;
    }

    diskH = GetResource(kDiskResType, kDiskResID);
    if (diskH == NULL || GetHandleSize(diskH) == 0) {
        ReportErr("\pThe embedded disk image is missing", ResError());
        return;
    }
    total = GetHandleSize(diskH);

    SetWatch();
    Log("\pFormatting the disk (800K)...");
    err = DIFormat(drive);
    if (err != noErr) { InitCursor(); ReportErr("\pFormat failed", err); Eject(NULL, drive); return; }

    Log("\pVerifying the format...");
    err = DIVerify(drive);
    if (err != noErr) { InitCursor(); ReportErr("\pFormat verify failed", err); Eject(NULL, drive); return; }

    Log("\pWriting ArtfulType to the disk...");
    MoveHHi(diskH);
    HLock(diskH);
    base = *diskH;

    SetProgress(0);
    off = 0;
    while (off < total) {
        chunk = total - off;
        if (chunk > kChunkBytes)
            chunk = kChunkBytes;

        pb.ioParam.ioCompletion = NULL;
        pb.ioParam.ioRefNum     = driverRef;
        pb.ioParam.ioVRefNum    = drive;
        pb.ioParam.ioBuffer     = base + off;
        pb.ioParam.ioReqCount   = chunk;
        pb.ioParam.ioPosMode    = fsFromStart;
        pb.ioParam.ioPosOffset  = off;

        err = PBWriteSync(&pb);
        if (err != noErr) {
            HUnlock(diskH);
            HideProgress();
            InitCursor();
            ReportErr("\pWrite failed", err);
            Eject(NULL, drive);
            return;
        }
        off += chunk;
        SetProgress((short)((off * 100L) / total));
    }

    HUnlock(diskH);
    HideProgress();
    InitCursor();

    Eject(NULL, drive);
    Log("\pDone. This disk is now a bootable ArtfulType floppy.");
    Log("\pInsert another blank disk to make more, or Quit.");
    SysBeep(20);
}

/* ----------------------------------------------------------------------- */

static void DoAbout(void)
{
    ParamText("\pArtfulType Floppy Writer\r\r"
              "Writes the embedded 800K disk image to an inserted floppy, "
              "making a bootable System 6.0.8 ArtfulType disk on real hardware.",
              "\p", "\p", "\p");
    NoteAlert(kInfoAlert, NULL);
}

static void HandleMenu(long choice)
{
    short menu = HiWord(choice);
    short item = LoWord(choice);
    Str255 daName;

    switch (menu) {
        case mApple:
            if (item == iAbout) {
                DoAbout();
            } else {
                GetMenuItemText(GetMenuHandle(mApple), item, daName);
                OpenDeskAcc(daName);
            }
            break;
        case mFile:
            if (item == iQuit)
                gDone = true;
            break;
    }
    HiliteMenu(0);
}

static void HandleMouse(EventRecord *ev)
{
    WindowPtr win;
    short part = FindWindow(ev->where, &win);

    switch (part) {
        case inMenuBar:
            HandleMenu(MenuSelect(ev->where));
            break;
        case inSysWindow:
            SystemClick(ev, win);
            break;
        case inDrag:
            DragWindow(win, ev->where, &qd.screenBits.bounds);
            break;
        case inContent:
            if (win != FrontWindow())
                SelectWindow(win);
            break;
        case inGoAway:
            if (TrackGoAway(win, ev->where))
                gDone = true;
            break;
    }
}

static void HandleKey(EventRecord *ev)
{
    /* char is signed on 68k; read the key code unsigned so a high-bit
       character never sign-extends (see ArtfulType CLAUDE.md). */
    unsigned char ch = (unsigned char)(ev->message & charCodeMask);
    if (ev->modifiers & cmdKey)
        HandleMenu(MenuKey(ch));
}

static void EventLoop(void)
{
    EventRecord ev;

    while (!gDone) {
        SystemTask();
        if (GetNextEvent(everyEvent, &ev)) {
            switch (ev.what) {
                case mouseDown:
                    HandleMouse(&ev);
                    break;
                case keyDown:
                case autoKey:
                    HandleKey(&ev);
                    break;
                case updateEvt:
                    if ((WindowPtr)ev.message == gWindow) {
                        BeginUpdate(gWindow);
                        SetPort(gWindow);
                        DrawContents();
                        EndUpdate(gWindow);
                    }
                    break;
                case diskEvt:
                    if (!gBusy) {
                        gBusy = true;
                        /* HiWord(message) != 0 means the disk did not mount
                           (blank/foreign) -- the common case. Either way,
                           WriteFloppy handles it. */
                        WriteFloppy(LoWord(ev.message));
                        gBusy = false;
                    }
                    break;
            }
        }
    }
}

static void SetUpMenus(void)
{
    MenuHandle apple, file;

    apple = NewMenu(mApple, "\p\024");   /* 0x14 = apple-logo char */
    AppendMenu(apple, "\pAbout ArtfulType Floppy Writer;(-");
    AppendResMenu(apple, 'DRVR');
    InsertMenu(apple, 0);

    file = NewMenu(mFile, "\pFile");
    AppendMenu(file, "\pQuit/Q");
    InsertMenu(file, 0);

    DrawMenuBar();
}

static void MakeWindow(void)
{
    Rect bounds;
    short left, top;

    left = (qd.screenBits.bounds.right - kWinWidth) / 2;
    top  = qd.screenBits.bounds.top + 60;
    SetRect(&bounds, left, top, left + kWinWidth, top + kWinHeight);

    gWindow = NewWindow(NULL, &bounds, "\pArtfulType Floppy Writer",
                        true, noGrowDocProc, (WindowPtr)-1L, true, 0);
    SetPort(gWindow);
}

static void InitToolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
}

int main(void)
{
    InitToolbox();
    SetUpMenus();
    MakeWindow();
    DILoad();

    Log("\pReady. Insert a blank 800K floppy to begin.");

    EventLoop();

    DIUnload();
    return 0;
}

/*
    Milestone 2: a real distraction-free Markdown editor.
    Full-screen window, wide margins, 14pt Times, File menu with
    Save/Open backed by the classic File Manager. Saving straight to
    the BlueSCSI SD card (bypassing this disk's HFS volume) is a
    later milestone -- this still saves onto the boot disk itself.
*/

#include "app.h"

WindowPtr gWindow;
TEHandle gTE;
TEHandle gHiddenTE;
TEHandle gActiveTE;
ControlHandle gScrollBar;
Boolean gScrollBarVisible = false;
Boolean gDone = false;
Boolean gHaveFile = false;
Boolean gDirty = false;
Str255 gFileName;
short gVRefNum;
MenuHandle gViewMenu;
MenuHandle gEditMenu;
Boolean gHideMarkdown = true;
short gZoomIndex = kZoomBaselineIndex;
short gWindowMode = kWindowModeFullScreen;
short gFontChoice = 0;

UndoSnapshot gUndoStack[MAX_UNDO_LEVELS];
short gUndoCount = 0;
UndoSnapshot gRedoStack[MAX_UNDO_LEVELS];
short gRedoCount = 0;
Boolean gTypingRunActive = false;

Str255 gLinkURLs[MAX_LINKS + 1];
short gLinkCount = 0;

static void Init(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void MakeMenu(void)
{
    MenuHandle appleMenu;
    MenuHandle fileMenu;
    MenuHandle styleMenu;

    /* Inserted first so it sits leftmost. "\024" is the apple glyph in the
       system font. AppendResMenu('DRVR') fills in the installed desk
       accessories below the About item, the standard Apple-menu contents. */
    appleMenu = NewMenu(mApple, "\p\024");
    AppendMenu(appleMenu, "\pAbout The Artful Type...;(-");
    AppendResMenu(appleMenu, 'DRVR');
    InsertMenu(appleMenu, 0);

    fileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(fileMenu, "\pNew/N;Open.../O;Save/S;Save As...;(-;Page Setup;Print/P;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

    /* No "/" shortcut on Redo -- it would register as a second cmd-key
       equivalent for the same letter as Undo, ambiguous to MenuKey.
       Cmd-Shift-Z for Redo is instead handled directly in EventLoop,
       intercepted before MenuKey ever sees it. */
    gEditMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(gEditMenu, "\pUndo/Z;Redo;(-;Cut/X;Copy/C;Paste/V;(-;Select All/A;(-;Find & Replace.../F;Find Again/G;Word Count;(-;Preferences...");
    InsertMenu(gEditMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gEditMenu, iRedo);

    styleMenu = NewMenu(mStyle, "\pStyle");
    AppendMenu(styleMenu, "\pBold/B;Italic/I;Code/K;Strikethrough/T;Highlight/H;(-;Heading 1/1;Heading 2/2;Heading 3/3;(-;Link/L;(-;None");
    InsertMenu(styleMenu, 0);

    gViewMenu = NewMenu(mView, "\pView");
    AppendMenu(gViewMenu, "\pMarkdown;Writer;(-;Zoom In/=;Zoom Out/-;Default Size/0;(-;Full Screen");
    InsertMenu(gViewMenu, 0);
    CheckItem(gViewMenu, iMarkdownView, !gHideMarkdown);
    CheckItem(gViewMenu, iWriterView, gHideMarkdown);
    CheckItem(gViewMenu, iFullScreen, gWindowMode == kWindowModeFullScreen);

    DrawMenuBar();

    /*
        Drop the Help ("?") menu for a cleaner, distraction-free bar. On
        System 7 the Menu Manager's MBDF auto-inserts up to three system menus
        at the right -- the Application (switcher) menu, the Help menu, and,
        only when more than one script system is installed, a Keyboard menu.
        The first DrawMenuBar above is what makes the MBDF's "calc" routine add
        them (it does so whenever an Apple menu is present and no system menus
        are yet in the list). DeleteMenu then removes the Help menu; the second
        DrawMenuBar repaints without it. It stays gone because the Application
        menu -- which Inside Macintosh VI says is "always displayed" and cannot
        be removed -- keeps a system menu present, so "calc" never re-adds the
        full set. We deliberately keep the Application menu (the user wants the
        switcher) and the Apple menu. DeleteMenu/DrawMenuBar are original traps,
        safe on every target; gate on System 7 only because System 6 has no Help
        menu to remove (see HasSystem7). The menu-bar clock (System 7.5+) is not
        a menu -- it is redrawn on a timer by the Date & Time control panel and
        has no Toolbox off-switch, so it can only be turned off there.
    */
    if (HasSystem7()) {
        DeleteMenu(kHMHelpMenuID);
        DrawMenuBar();
    }
}

/* The text view rectangle for the current window: the content area inset by
   the wide distraction-free margins. In windowed mode leave room at the
   bottom-right for the grow icon so the scrollbar doesn't overlap it. */
static void ComputeViewRect(Rect *r)
{
    *r = gWindow->portRect;
    r->left += MARGIN_H;
    r->right -= MARGIN_H;
    r->top += MARGIN_TOP;
    r->bottom -= MARGIN_BOTTOM;
    if (gWindowMode == kWindowModeWindowed)
        r->bottom -= kGrowIconSize;
}

/* The vertical scrollbar sits centered in the right-hand margin. Derived from
   the view rect so it always tracks the text column. */
static void ComputeScrollbarRect(const Rect *viewRect, Rect *sbRect)
{
    *sbRect = *viewRect;
    sbRect->left = viewRect->right + (MARGIN_H - SCROLLBAR_WIDTH) / 2;
    sbRect->right = sbRect->left + SCROLLBAR_WIDTH;
    sbRect->top -= 1;
    sbRect->bottom += 1;
}

/*
    Re-fit the two TextEdit records and the scrollbar to the window's current
    portRect. Shared by creation, a live resize (grow box), and a window-mode
    rebuild. Each record keeps its current vertical scroll offset (destRect.top)
    while re-wrapping to the new column width via TECalText.
*/
static void LayoutWindow(void)
{
    Rect viewRect, sbRect;

    ComputeViewRect(&viewRect);

    (**gTE).viewRect = viewRect;
    (**gTE).destRect.left = viewRect.left;
    (**gTE).destRect.right = viewRect.right;
    TECalText(gTE);

    (**gHiddenTE).viewRect = viewRect;
    (**gHiddenTE).destRect.left = viewRect.left;
    (**gHiddenTE).destRect.right = viewRect.right;
    TECalText(gHiddenTE);

    ComputeScrollbarRect(&viewRect, &sbRect);
    MoveControl(gScrollBar, sbRect.left, sbRect.top);
    SizeControl(gScrollBar, sbRect.right - sbRect.left, sbRect.bottom - sbRect.top);

    InvalidateHeightCache();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

/* Set the windowed-mode title bar to the current document name. A no-op in
   full-screen mode (plainDBox has no title bar). Called on mode switch and
   whenever the document identity changes (New/Open/Save). */
void UpdateWindowTitle(void)
{
    if (gWindowMode == kWindowModeWindowed)
        SetWTitle(gWindow, gHaveFile ? (ConstStr255Param) gFileName
                                     : (ConstStr255Param) "\pUntitled");
}

/* Create the document window with the proc for the current mode: a borderless
   full-screen plainDBox, or a titled/draggable/resizable documentProc that
   opens maximized (content just below the menu bar + title bar, a few pixels of
   desktop showing so it reads as a real window). No go-away or zoom box, by
   design (ADR 0002). */
static void CreateDocWindow(void)
{
    Rect bounds = qd.screenBits.bounds;

    if (gWindowMode == kWindowModeWindowed) {
        bounds.top += MENU_BAR_HEIGHT + kTitleBarHeight;
        bounds.left += kWindowInset;
        bounds.right -= kWindowInset;
        bounds.bottom -= kWindowInset;
        gWindow = NewWindow(NULL, &bounds, "\pUntitled", true, documentProc,
                            (WindowPtr) -1L, false, 0);
    } else {
        bounds.top += MENU_BAR_HEIGHT;
        gWindow = NewWindow(NULL, &bounds, "\p", true, plainDBox,
                            (WindowPtr) -1L, false, 0);
    }
    SetPort(gWindow);
}

/* Create the two TextEdit records and the scrollbar in the current window. The
   base font must be set on the port before TEStyleNew so each record adopts it
   as its default style. Callers set gActiveTE and lay out afterward. */
static void CreateDocContents(void)
{
    Rect viewRect, sbRect;
    short fontNum = BodyFontNum();

    TextFont(fontNum);
    TextSize(CurrentFontSize());

    ComputeViewRect(&viewRect);
    gTE = TEStyleNew(&viewRect, &viewRect);
    gHiddenTE = TEStyleNew(&viewRect, &viewRect);

    ComputeScrollbarRect(&viewRect, &sbRect);
    gScrollBar = NewControl(gWindow, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
    /* A freshly created control is hidden; keep the visibility flag in step so
       UpdateScrollbarRange re-shows it when the (reloaded) document needs it. */
    gScrollBarVisible = false;
}

static void MakeWindow(void)
{
    CreateDocWindow();
    CreateDocContents();
    gActiveTE = gHideMarkdown ? gHiddenTE : gTE;
    TEActivate(gActiveTE);
    UpdateWindowTitle();
    LayoutWindow();
}

/*
    Rebuild the document from its canonical Markdown text. A live window's
    definition proc can't change (full-screen<->windowed), and a TextEdit
    record's base font is fixed at creation (a body-font change) -- so both
    cases recreate the TE records, and both reconstruct the view through the
    same canonical path open/undo use (TEInsert + BuildHiddenView), avoiding any
    fragile re-pointing of TextEdit's port. Undo history survives (its snapshots
    hold canonical text, independent of these records). ADR 0002.

    newMode >= 0 and different from the current mode rebuilds the whole window;
    newMode < 0 keeps the window and rebuilds only the contents (font change).
*/
static void RebuildDocument(short newMode)
{
    Handle canonH;
    long canonLen;
    short selStart, selEnd;
    Boolean rebuildWindow = (Boolean) (newMode >= 0 && newMode != gWindowMode);

    /* Canonicalize to gTE, then copy its text out before we dispose it. */
    if (gHideMarkdown)
        SyncHiddenToCanonical();
    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    canonLen = (**gTE).teLength;
    canonH = (**gTE).hText;
    if (HandToHand(&canonH) != noErr)
        return;   /* out of memory -- leave the document as it is */

    /* The TE records are independent handles, freed explicitly either way. */
    TEDispose(gTE);
    TEDispose(gHiddenTE);
    if (rebuildWindow) {
        /* DisposeWindow also disposes its scrollbar control. */
        DisposeWindow(gWindow);
        gWindowMode = newMode;
        CreateDocWindow();
        CreateDocContents();
    } else {
        /* Same window; replace just the contents (CreateDocContents remakes the
           scrollbar, so free the old one first). */
        DisposeControl(gScrollBar);
        CreateDocContents();
    }

    /* Point gActiveTE at a live new record before any TE call, so nothing runs
       against the just-disposed one. */
    gActiveTE = gHideMarkdown ? gHiddenTE : gTE;

    HLock(canonH);
    TESetSelect(0, 0, gTE);
    TEInsert(*canonH, canonLen, gTE);
    HUnlock(canonH);
    DisposeHandle(canonH);

    if (gHideMarkdown) {
        BuildHiddenView();
    } else {
        if (selEnd > (**gTE).teLength) selEnd = (**gTE).teLength;
        if (selStart > selEnd) selStart = selEnd;
        TESetSelect(selStart, selEnd, gTE);
    }
    TEActivate(gActiveTE);

    UpdateWindowTitle();
    LayoutWindow();
    ScrollCaretIntoView();
}

/* Re-render the document in the currently chosen body font (gFontChoice),
   keeping the window. Called from the Preferences dialog. */
void ApplyDocumentFont(void)
{
    RebuildDocument(-1);
}

/* Switch presentation mode (no-op if already there) and keep the View-menu
   checkmark in step. Does not persist -- callers decide when to SavePrefs. */
void SetWindowMode(short newMode)
{
    if (newMode == gWindowMode)
        return;
    RebuildDocument(newMode);
    CheckItem(gViewMenu, iFullScreen, gWindowMode == kWindowModeFullScreen);
}

static void DoToggleWindowMode(void)
{
    SetWindowMode(gWindowMode == kWindowModeFullScreen
                  ? kWindowModeWindowed : kWindowModeFullScreen);
    SavePrefs();
}

static void DoUpdate(WindowPtr w)
{
    BeginUpdate(w);
    EraseRect(&w->portRect);
    TEUpdate(&w->portRect, gActiveTE);
    /* Block/inline features TextEdit can't draw (code-block and highlight
       backgrounds, blockquote bars, list markers, strike lines, horizontal
       rules) are overpainted now that the text is down. Only in Writer mode:
       gActiveTE is gHiddenTE there, and gTE (Markdown mode) shows raw text with
       none of these, so this would be a no-op sweep otherwise. */
    if (gHideMarkdown)
        DrawWriterOverlays(gActiveTE, true);
    DrawControls(w);
    /* In windowed mode paint the grow box. Clip to the 15x15 corner so
       DrawGrowIcon draws only the size box, not its scrollbar-delimiter lines
       (our scrollbar lives in the margin, not flush to the window edges). */
    if (gWindowMode == kWindowModeWindowed) {
        RgnHandle savedClip = NewRgn();

        if (savedClip != NULL) {
            Rect growRect = w->portRect;

            GetClip(savedClip);
            growRect.left = growRect.right - kGrowIconSize;
            growRect.top = growRect.bottom - kGrowIconSize;
            ClipRect(&growRect);
            DrawGrowIcon(w);
            SetClip(savedClip);
            DisposeRgn(savedClip);
        }
    }
    EndUpdate(w);
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short menuItem = LoWord(menuResult);

    if (menuID == mApple) {
        if (menuItem == iAbout) {
            ShowAboutBox();
        } else {
            /* Any other Apple-menu item is a desk accessory: hand its
               name to the system to open it. */
            Str255 daName;
            GetMenuItemText(GetMenuHandle(mApple), menuItem, daName);
            OpenDeskAcc(daName);
        }
    } else if (menuID == mFile) {
        switch (menuItem) {
            case iNew:
                if (ConfirmDiscardChanges())
                    DoNewFile();
                break;
            case iOpen:
                if (ConfirmDiscardChanges())
                    DoOpenFile();
                break;
            case iSave:   DoSave(); break;
            case iSaveAs: DoSaveAs(); break;
            case iPageSetup: DoPageSetup(); break;
            case iPrint:     DoPrint(); break;
            case iQuit:
                if (ConfirmDiscardChanges())
                    gDone = true;
                break;
        }
    } else if (menuID == mEdit) {
        /* If a desk accessory is frontmost, give it first refusal on the
           standard editing commands (it acts on its own selection). Our
           Edit menu is non-standard, so map items to editCmd explicitly;
           Redo and Select All have no SystemEdit equivalent. */
        short editCmd = -1;

        if (FrontWindow() != gWindow) {
            switch (menuItem) {
                case iUndo:  editCmd = undoCmd;  break;
                case iCut:   editCmd = cutCmd;   break;
                case iCopy:  editCmd = copyCmd;  break;
                case iPaste: editCmd = pasteCmd; break;
            }
        }

        if (editCmd < 0 || !SystemEdit(editCmd)) {
            switch (menuItem) {
                case iUndo:      DoUndo(); break;
                case iRedo:      DoRedo(); break;
                case iCut:       DoCut(); break;
                case iCopy:      DoCopy(); break;
                case iPaste:     DoPaste(); break;
                case iSelectAll: DoSelectAll(); break;
                case iFindReplace: DoFindReplace(); break;
                case iFindAgain:   DoFindAgain(); break;
                case iWordCount:   DoWordCount(); break;
                case iPreferences: DoPreferences(); break;
            }
        }
    } else if (menuID == mStyle) {
        DoStyleCommand(menuItem);
    } else if (menuID == mView) {
        switch (menuItem) {
            case iMarkdownView: SetViewMode(false); break;
            case iWriterView:   SetViewMode(true); break;
            case iZoomIn:       DoZoom(1); break;
            case iZoomOut:      DoZoom(-1); break;
            case iZoomDefault:  DoZoomReset(); break;
            case iFullScreen:   DoToggleWindowMode(); break;
        }
    }
    HiliteMenu(0);
}

/*
    Bring the document window's TextEdit and scrollbar into the active or
    inactive look together. Shared by activate events (clicking between our
    window and a desk accessory) and MultiFinder suspend/resume, so a
    backgrounded app dims its scrollbar and drops its caret exactly as it
    would when a DA comes forward.
*/
static void SetWindowActive(Boolean active)
{
    if (active)
        TEActivate(gActiveTE);
    else
        TEDeactivate(gActiveTE);
    HiliteControl(gScrollBar, active ? 0 : 255);
}

/* Two-digit zero-padded write into dst[0..1]. */
static void TwoDigits(unsigned char *dst, short v)
{
    dst[0] = (unsigned char) ('0' + (v / 10) % 10);
    dst[1] = (unsigned char) ('0' + v % 10);
}

/* True if the '@' at buf[atPos] starts a word -- at the buffer start, or with a
   non-alphanumeric character before it. Keeps @today/@time from firing inside
   things like an email address (bob@time...) or a longer @word. */
static Boolean AtKeywordBoundary(const char *buf, long atPos)
{
    unsigned char prev;
    if (atPos <= 0)
        return true;
    prev = (unsigned char) buf[atPos - 1];
    return (Boolean) !((prev >= 'A' && prev <= 'Z') ||
                       (prev >= 'a' && prev <= 'z') ||
                       (prev >= '0' && prev <= '9'));
}

/*
    Expand a just-completed @today / @time keyword into the current date/time
    (YYYY-MM-DD / HH:MM). Called from the typing path; the justTyped filter ('y'
    ends "@today", 'e' ends "@time") keeps this off the hot path for every other
    key. Replaces the keyword in place and works in either view. ADR 0003.
*/
static void ExpandDateKeyword(unsigned char justTyped)
{
    TEHandle te = gActiveTE;
    long caret = (**te).selEnd;
    Handle h = (**te).hText;
    short kwLen = 0;
    Boolean isTime = false;
    Str255 repl;
    unsigned long secs;
    DateTimeRec dt;
    short net;

    if (justTyped == 'y' && caret >= 6) {
        HLock(h);
        if (memcmp((char *) *h + caret - 6, "@today", 6) == 0 &&
            AtKeywordBoundary(*h, caret - 6))
            kwLen = 6;
        HUnlock(h);
    } else if (justTyped == 'e' && caret >= 5) {
        HLock(h);
        if (memcmp((char *) *h + caret - 5, "@time", 5) == 0 &&
            AtKeywordBoundary(*h, caret - 5)) {
            kwLen = 5;
            isTime = true;
        }
        HUnlock(h);
    }
    if (kwLen == 0)
        return;

    GetDateTime(&secs);
    SecondsToDate(secs, &dt);
    if (isTime) {
        repl[0] = 5;                        /* HH:MM */
        TwoDigits(&repl[1], dt.hour);
        repl[3] = ':';
        TwoDigits(&repl[4], dt.minute);
    } else {
        repl[0] = 10;                       /* YYYY-MM-DD */
        repl[1] = (unsigned char) ('0' + (dt.year / 1000) % 10);
        repl[2] = (unsigned char) ('0' + (dt.year / 100) % 10);
        repl[3] = (unsigned char) ('0' + (dt.year / 10) % 10);
        repl[4] = (unsigned char) ('0' + dt.year % 10);
        repl[5] = '-';
        TwoDigits(&repl[6], dt.month);
        repl[8] = '-';
        TwoDigits(&repl[9], dt.day);
    }

    net = (short) repl[0] - kwLen;
    if (net > 0 && !DocCanGrowBy(te, net))
        return;

    TESetSelect((short) (caret - kwLen), (short) caret, te);
    TEDelete(te);
    TEInsert(&repl[1], (long) repl[0], te);
    gDirty = true;
}

static void EventLoop(void)
{
    EventRecord event;
    WindowPtr w;
    short part;

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, 15, NULL)) {
            /* Disposing a dialog/window doesn't restore the caller's port
               -- cheap insurance against any path (found or not) leaving
               thePort dangling at a freed window's memory. */
            SetPort(gWindow);
            switch (event.what) {
                case updateEvt:
                    /* Only our own window's updates are ours to paint; a
                       desk accessory redraws its own window. */
                    if ((WindowPtr) event.message == gWindow)
                        DoUpdate(gWindow);
                    break;

                case mouseDown:
                    part = FindWindow(event.where, &w);
                    if (part == inMenuBar) {
                        UpdateEditMenuState();
                        DoMenuCommand(MenuSelect(event.where));
                    } else if (part == inSysWindow) {
                        /* A click in a desk accessory's window -- let the
                           system route it to the DA. */
                        SystemClick(&event, w);
                    } else if (part == inDrag && w == gWindow) {
                        /* Windowed mode only (plainDBox has no drag region):
                           drag the window anywhere on the desktop. */
                        Rect dragBounds = (**GetGrayRgn()).rgnBBox;
                        DragWindow(w, event.where, &dragBounds);
                    } else if (part == inGrow && w == gWindow) {
                        /* Windowed mode only: resize, then re-fit the text and
                           scrollbar to the new size. */
                        Rect limit;
                        long grow;

                        SetRect(&limit, kMinWindowWidth, kMinWindowHeight,
                                qd.screenBits.bounds.right,
                                qd.screenBits.bounds.bottom);
                        grow = GrowWindow(w, event.where, &limit);
                        if (grow != 0) {
                            SizeWindow(w, LoWord(grow), HiWord(grow), true);
                            LayoutWindow();
                        }
                    } else if (part == inContent) {
                        if (w != FrontWindow()) {
                            /* Our window is behind a desk accessory: a click
                               brings it forward rather than editing through
                               the DA. */
                            SelectWindow(w);
                        } else {
                            ControlHandle hitControl;

                            SetPort(w);
                            GlobalToLocal(&event.where);
                            if (FindControl(event.where, w, &hitControl) != 0 && hitControl == gScrollBar)
                                DoScrollClick(event.where);
                            else {
                                gTypingRunActive = false;
                                TEClick(event.where, (event.modifiers & shiftKey) != 0, gActiveTE);
                                /* TEClick redraws any text whose selection state
                                   changed (e.g. a run being deselected) but not
                                   our hand-drawn Writer overlays, and posts no
                                   update event -- so a click that deselects a
                                   struck/highlighted run erases its overlay until
                                   the next repaint. Repaint them now, exactly as
                                   the scroll and keystroke paths do. */
                                if (gHideMarkdown)
                                    DrawWriterOverlays(gActiveTE, true);
                            }
                        }
                    }
                    break;

                case keyDown:
                case autoKey: {
                    /* Unsigned: high-bit characters (option-accented letters,
                       etc.) must stay 0x80..0xFF, not sign-extend to negative
                       and read as control keys below. */
                    unsigned char key = event.message & charCodeMask;
                    Boolean isContentKey = (key < 0x1C || key > 0x1F);

                    if (event.modifiers & cmdKey) {
                        if (event.what == keyDown) {
                            if ((key == 'z' || key == 'Z') && (event.modifiers & shiftKey))
                                DoRedo();
                            else {
                                UpdateEditMenuState();
                                DoMenuCommand(MenuKey(key));
                            }
                        }
                    } else if (FrontWindow() != gWindow) {
                        /* A desk accessory is frontmost: it handles its own
                           typing; we must not consume the key into our TE. */
                    } else if (ScrollByKey(key)) {
                        /* Home/End/Page Up/Page Down scrolled the view; there is
                           nothing to insert. */
                    } else if (isContentKey && key != kBackspaceKey &&
                               !DocCanGrowBy(gActiveTE, 1)) {
                        /* At the document-size cap: refuse further inserted
                           characters (backspace/arrows still work so the
                           user can get back under the limit). */
                        SysBeep(1);
                    } else {
                        if (isContentKey) {
                            if (!gTypingRunActive) {
                                PushUndoSnapshot();
                                gTypingRunActive = true;
                            }
                        } else {
                            gTypingRunActive = false;
                        }

                        TEKey(key, gActiveTE);
                        if (isContentKey) {
                            gDirty = true;
                            if (gHideMarkdown)
                                DetectInlineMarkdown(key);
                            ExpandDateKeyword(key);
                        }
                        {
                            /* TextEdit redrew the edited line(s) but not the
                               block and non-face overlays; repaint them on the
                               visible runs (see DrawWriterOverlays). When the
                               caret scrolls, ScrollCaretIntoView re-lays the view
                               and repaints the overlays cleanly itself (a scroll
                               block-copies the stipple, which must not be
                               re-OR'd), so only repaint here when it did not. */
                            Boolean scrolled;
                            /* Typing the closing ``` of a fence: re-render it
                               into a clean Monaco code span the moment it
                               balances (markers stripped), exactly like a
                               Markdown<->Writer toggle does -- otherwise the
                               block only renders right after a manual toggle. */
                            if (gHideMarkdown && key == '`' && CodeFencesBalanced())
                                RerenderWriterView();
                            scrolled = ScrollCaretIntoView();
                            UpdateScrollbarRange();
                            if (gHideMarkdown) {
                                /* A backtick changes fence classification for a
                                   whole region, so re-lay the view cleanly rather
                                   than incrementally patOr-ing over it. */
                                if (key == '`')
                                    RepaintWriterViewForced();
                                else if (!scrolled)
                                    DrawWriterOverlays(gActiveTE, true);
                            }
                        }
                    }
                    break;
                }

                case activateEvt:
                    /* Only our own window's activation drives our TE and
                       scrollbar; a DA window's activate events are its own. */
                    if ((WindowPtr) event.message == gWindow)
                        SetWindowActive((event.modifiers & activeFlag) != 0);
                    break;

                case osEvt:
                    /* MultiFinder suspend/resume. The message's high byte
                       tags it; bit 0 (RESUME) distinguishes resume from
                       suspend. Our document window fills the whole screen
                       (plainDBox over qd.screenBits.bounds), so a suspended
                       app left on screen would keep hiding the Finder desktop
                       and every other app behind it. So don't just dim the
                       caret/scrollbar -- hide the window on suspend and show
                       it again on resume, so switching to the Finder actually
                       reveals the Finder. ShowWindow/HideWindow are original
                       traps, and this osEvt only arrives under MultiFinder. */
                    if ((((unsigned long) event.message >> 24) & 0xFF) ==
                            suspendResumeMessage) {
                        if (event.message & resumeFlag) {
                            ShowWindow(gWindow);
                            SetWindowActive(true);
                        } else {
                            SetWindowActive(false);
                            HideWindow(gWindow);
                        }
                    }
                    break;
            }
        }
        TEIdle(gActiveTE);
    }
}

/*
    True on System 7.0 and later. A handful of Toolbox routines this app
    uses are System 7 additions -- FindFolder and the FSSpec resource
    calls (the System 7 branch of OpenPrefsFile in zoom.c; System 6 uses
    BootDrive instead), and SetDialogDefaultItem (the link and Preferences
    dialogs). On System 6 those are unimplemented traps and a 68000 Mac
    executes the A-line as a bad instruction and drops into the debugger --
    confirmed live: an "unimplemented trap" crash at startup on a real Mac SE
    running 6.0.8, from FindFolder in the old prefs loader. Gate every such call on this so the
    app still runs on the System 6 compact Macs it targets. SysEnvirons is
    the pre-Gestalt environment call and is itself safe all the way back,
    so it's the right probe. The answer can't change during a run; cache it.
*/
Boolean HasSystem7(void)
{
    static short cached = -1;   /* -1 = not yet probed, 0 = no, 1 = yes */

    if (cached < 0) {
        SysEnvRec env;
        cached = (SysEnvirons(1, &env) == noErr &&
                  env.systemVersion >= 0x0700) ? 1 : 0;
    }
    return (Boolean) (cached != 0);
}

int main(void)
{
    short message, count;

    Init();
    LoadPrefs();
    MakeMenu();
    MakeWindow();

    /* A newly-created visible window has its whole content area marked
       invalid automatically, but the splash dialog appears before the
       event loop ever gets a chance to dequeue and process that update
       event -- force the real BeginUpdate/TEUpdate/EndUpdate cycle to
       happen now, so the window has gone through one proper paint before
       the user can type anything. Without this, the very first line typed
       (before any other update has occurred) doesn't render reliably. */
    DoUpdate(gWindow);

    CountAppFiles(&message, &count);
    if (count >= 1 && message == appOpen)
        DoStartupOpen();
    else
        ShowSplashScreen();

    EventLoop();
    return 0;
}

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
    AppendMenu(fileMenu, "\pNew/N;Open.../O;Save/S;Save As...;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

    /* No "/" shortcut on Redo -- it would register as a second cmd-key
       equivalent for the same letter as Undo, ambiguous to MenuKey.
       Cmd-Shift-Z for Redo is instead handled directly in EventLoop,
       intercepted before MenuKey ever sees it. */
    gEditMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(gEditMenu, "\pUndo/Z;Redo;(-;Cut/X;Copy/C;Paste/V;(-;Select All/A");
    InsertMenu(gEditMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gEditMenu, iRedo);

    styleMenu = NewMenu(mStyle, "\pStyle");
    AppendMenu(styleMenu, "\pBold/B;Italic/I;Code/K;Strikethrough;(-;Heading 1/1;Heading 2/2;Heading 3/3;(-;Link/L;(-;None");
    InsertMenu(styleMenu, 0);

    gViewMenu = NewMenu(mView, "\pView");
    AppendMenu(gViewMenu, "\pMarkdown;Writer;(-;Zoom In/=;Zoom Out/-;Default Size/0");
    InsertMenu(gViewMenu, 0);
    CheckItem(gViewMenu, iWriterView, true);

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

static void MakeWindow(void)
{
    Rect bounds;
    Rect viewRect;
    Rect sbRect;
    short fontNum;

    bounds = qd.screenBits.bounds;
    bounds.top += MENU_BAR_HEIGHT;

    gWindow = NewWindow(NULL, &bounds, "\p", true, plainDBox,
                         (WindowPtr) -1L, false, 0);
    SetPort(gWindow);

    GetFNum("\pTimes", &fontNum);
    TextFont(fontNum);
    TextSize(CurrentFontSize());

    viewRect = gWindow->portRect;
    viewRect.left += MARGIN_H;
    viewRect.right -= MARGIN_H;
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= MARGIN_BOTTOM;

    gTE = TEStyleNew(&viewRect, &viewRect);
    gHiddenTE = TEStyleNew(&viewRect, &viewRect);
    gActiveTE = gHideMarkdown ? gHiddenTE : gTE;
    TEActivate(gActiveTE);

    sbRect = viewRect;
    sbRect.left = viewRect.right + (MARGIN_H - SCROLLBAR_WIDTH) / 2;
    sbRect.right = sbRect.left + SCROLLBAR_WIDTH;
    sbRect.top -= 1;
    sbRect.bottom += 1;
    gScrollBar = NewControl(gWindow, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
}

static void DoUpdate(WindowPtr w)
{
    BeginUpdate(w);
    EraseRect(&w->portRect);
    TEUpdate(&w->portRect, gActiveTE);
    DrawControls(w);
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
            }
        }
    } else if (menuID == mStyle) {
        gDirty = true;
        PushUndoSnapshot();
        gTypingRunActive = false;
        if (gHideMarkdown) {
            switch (menuItem) {
                case iBold:   ToggleFace(bold); break;
                case iItalic: ToggleFace(italic); break;
                case iCode:   ToggleCode(); break;
                case iStrike: break; /* no native strikethrough on classic Mac text styles */
                case iH1:     ToggleHeadingHidden(1); break;
                case iH2:     ToggleHeadingHidden(2); break;
                case iH3:     ToggleHeadingHidden(3); break;
                case iLink:   DoLinkHidden(); break;
                case iNone:   ClearSelectionStyleHidden(); break;
            }
        } else {
            switch (menuItem) {
                case iBold:   WrapSelection("**", "**"); break;
                case iItalic: WrapSelection("*", "*"); break;
                case iCode:   WrapSelection("`", "`"); break;
                case iStrike: WrapSelection("~~", "~~"); break;
                case iH1:     ApplyHeading(1); break;
                case iH2:     ApplyHeading(2); break;
                case iH3:     ApplyHeading(3); break;
                case iLink:   DoLink(); break;
                case iNone:   ClearMarkdownInSelection(); break;
            }
            ClearStyles();
        }
        AdjustScrollbar();
    } else if (menuID == mView) {
        switch (menuItem) {
            case iMarkdownView: SetViewMode(false); break;
            case iWriterView:   SetViewMode(true); break;
            case iZoomIn:       DoZoom(1); break;
            case iZoomOut:      DoZoom(-1); break;
            case iZoomDefault:  DoZoomReset(); break;
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
                        }
                        ScrollCaretIntoView();
                        UpdateScrollbarRange();
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
                       suspend. Handled the same as activate/deactivate so
                       the caret and scrollbar track foreground state. */
                    if (((unsigned long) event.message & 0xFF000000UL) ==
                            SUSPENDRESUMEBITS)
                        SetWindowActive((event.message & RESUME) != 0);
                    break;
            }
        }
        TEIdle(gActiveTE);
    }
}

/*
    True on System 7.0 and later. A handful of Toolbox routines this app
    uses are System 7 additions -- FindFolder and the FSSpec resource
    calls (the Preferences-folder path in zoom.c), and
    SetDialogDefaultItem/SetDialogCancelItem (the link dialog). On System 6
    those are unimplemented traps and a 68000 Mac executes the A-line as a
    bad instruction and drops into the debugger -- confirmed live: an
    "unimplemented trap" crash at startup on a real Mac SE running 6.0.8,
    from FindFolder in LoadZoomPref. Gate every such call on this so the
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
    LoadZoomPref();
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

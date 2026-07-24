#ifndef ARTFULTYPE_APP_H
#define ARTFULTYPE_APP_H

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <Files.h>
#include <StandardFile.h>
#include <SegLoad.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Sound.h>
#include <Scrap.h>
#include <Resources.h>
#include <Devices.h>
#include <Folders.h>
#include <Gestalt.h>
#include <Errors.h>
#include <LowMem.h>
#include <DateTimeUtils.h>
#include <string.h>
#include "mdcore.h"

/* Standard desk-accessory Edit-menu command numbers passed to SystemEdit when a
   DA is frontmost. Apple's Universal Interfaces don't define these (the
   multiversal interfaces did), so provide them here. */
#ifndef undoCmd
#define undoCmd  1
#define cutCmd   3
#define copyCmd  4
#define pasteCmd 5
#endif

#define MARGIN_H     64
#define MARGIN_TOP   32
#define MARGIN_BOTTOM 24
#define MENU_BAR_HEIGHT 20
#define FONT_SIZE 18

/* Document-window presentation mode (ADR 0002). Full screen is a borderless
   plainDBox filling the screen (the default, distraction-free look); windowed
   is a titled, draggable, resizable documentProc window. kGrowIconSize is the
   15x15 grow-box the windowed proc reserves at the bottom-right, which the text
   column and scrollbar keep clear of. */
#define kWindowModeFullScreen 0
#define kWindowModeWindowed   1
#define kGrowIconSize         15
#define kTitleBarHeight       20   /* room a documentProc title bar occupies   */
#define kWindowInset           3   /* desktop showing around a windowed window  */
#define kMinWindowWidth      240   /* GrowWindow lower bounds                    */
#define kMinWindowHeight     160
/* Generous upper bound on a single line's pixel height (the largest heading,
   H1, is ~30pt); used only as visibility slack when culling lines in
   DrawStruckRuns, so it just has to be >= any real line height. */
#define MAX_LINE_HEIGHT 64
#define SCROLLBAR_WIDTH 16
#define kBackspaceKey 0x08
#define kReturnKey    0x0D
#define kEnterKey     0x03
#define kEscapeKey    0x1B
/* Navigation keys (charCodes), for the scroll shortcuts in ScrollByKey. */
#define kHomeKey      0x01
#define kEndKey       0x04
#define kPageUpKey    0x0B
#define kPageDownKey  0x0C

#define mFile      128
#define iNew       1
#define iOpen      2
#define iSave      3
#define iSaveAs    4
/* item 5 is a separator */
#define iPageSetup 6
#define iPrint     7
/* item 8 is a separator */
#define iQuit      9

#define mEdit    131
#define iUndo    1
#define iRedo    2
#define iCut     4
#define iCopy    5
#define iPaste   6
#define iSelectAll 8
/* item 9 is a separator */
#define iFindReplace 10
#define iFindAgain   11
#define iWordCount   12
/* item 13 is a separator */
#define iPreferences 14

/* Find & Replace dialog (ADR 0003). */
#define kFindDialog     138
#define iFindBtn        1
#define iFindCancel     2
#define iReplaceAllBtn  3
#define iFindField      5
#define iReplaceField   7
#define iFindCaseChk    8

#define mStyle    129
#define iBold      1
#define iItalic    2
#define iCode      3
#define iStrike    4
#define iHighlight 5
/* item 6 is a separator */
#define iH1        7
#define iH2        8
#define iH3        9
/* item 10 is a separator */
#define iLink      11
/* item 12 is a separator */
#define iNone      13

#define kSaveChangesAlert 130
#define kSaveBtn          1
#define kCancelBtn        2
#define kDontSaveBtn      3

#define kSplashDialog 131
#define iSplashNew    1
#define iSplashOpen   2
#define iSplashTitle  3

#define kLinkDialog  132
#define iLinkOK      1
#define iLinkCancel  2
#define iLinkField   4

#define kAboutDialog 133
#define iAboutOK     1
#define iAboutTitle  2

#define kErrorAlert  134

/* Print Options: choose the formatted Writer rendering or the raw Markdown
   source before the standard Job dialog (see DoPrint). */
#define kPrintOptionsDialog 135
#define iPrintOptOK        1
#define iPrintOptCancel    2
#define iPrintOptFormatted 3
#define iPrintOptSource    4

/* Modeless "Printing..." status window, shown while the print loop runs so the
   user knows Command-period cancels (Inside Macintosh II). */
#define kPrintStatusDialog  136

/* Preferences dialog (ADR 0002). Four pop-up menus rendered as userItems. */
#define kPrefsDialog     137
#define iPrefOK          1
#define iPrefCancel      2
#define iPrefWindowPopup 5
#define iPrefViewPopup   7
#define iPrefFontPopup   9
#define iPrefZoomPopup   11

#define mView        130
#define iMarkdownView 1
#define iWriterView  2
#define iZoomIn      4
#define iZoomOut     5
#define iZoomDefault 6
/* item 7 is a separator */
#define iFullScreen  8

/* The Apple menu (id 1, drawn leftmost) carries About + the desk
   accessories. About lives here, its conventional classic-Mac home,
   rather than in a separate Help menu. */
#define mApple   1
#define iAbout   1

/* System 7's Menu Manager auto-inserts a Help ("?") system menu at the right
   end of the bar. This is its well-known menu ID; we DeleteMenu it for a
   cleaner, distraction-free bar (see MakeMenu). Not defined by the multiversal
   headers, so it lives here. */
#define kHMHelpMenuID (-16490)

/* The pure core (mdcore.h) owns these caps; alias the app-side names to
   the single source of truth so the two never drift apart. */
#define MAX_STYLE_OPS MD_MAX_SPANS

/*
    The active document is bounded to this many characters. TextEdit's hard
    limit is 32767 bytes and TEInsert does not enforce it -- the caller must
    (Inside Macintosh -- Text, Listing 2-8: kMaxTELength). This bound is on
    the *visible* buffer; switching to Markdown mode re-adds delimiter
    characters (#, **, `, [](), ...), so the canonical text is somewhat
    longer. 20000 leaves generous headroom for that delimiter overhead so a
    realistic prose document stays within TextEdit's limit through a save or
    mode-switch round-trip. (A pathologically over-styled document -- many
    thousands of single-character styled runs -- is not fully covered; noted
    as a known limitation.)
*/
#define kMaxTELength 20000L

#define kNumZoomLevels 5
#define kZoomBaselineIndex 2

/* Body-font choices for the Font preference (ADR 0002). Index 0 (Times) is the
   built-in default and the fallback whenever a chosen font isn't installed. The
   name table and GetFNum validation live in zoom.c. */
#define kNumFontChoices 3

/*
    Preferences (ADR 0002). One versioned record holds every persisted setting.
    It lives in the System Folder: the Preferences folder on System 7 (via
    FindFolder), or loose in the System Folder on System 6 (located through the
    BootDrive low-memory global) -- see OpenPrefsFile in zoom.c. A missing or
    stale record just leaves the built-in defaults untouched. viewMode mirrors
    gHideMarkdown (1 = Writer, 0 = Markdown).
*/
#define kPrefsVersion 1
#define kPrefsResType 'Pref'
#define kPrefsResID   128
#define kPrefsFileName "\pArtful Type Preferences"

typedef struct {
    short version;      /* kPrefsVersion                                  */
    short windowMode;   /* kWindowModeFullScreen / kWindowModeWindowed    */
    short viewMode;     /* 1 = Writer (gHideMarkdown), 0 = Markdown        */
    short fontChoice;   /* index into the body-font table (0 = Times)      */
    short zoomIndex;    /* 0..kNumZoomLevels-1                             */
} AtPrefs;

/*
    Undo/redo snapshots store the *canonical markdown text* regardless
    of which mode is active, not gActiveTE's raw buffer -- gHiddenTE's
    styling (bold/heading/link runs) has no simple "get it all, restore
    it all" API in classic styled TextEdit, but canonical markdown text
    already round-trips styling correctly through the existing
    BuildHiddenView/SyncHiddenToCanonical machinery. So: push a
    snapshot by syncing to canonical first (if in Writer mode) and
    copying gTE's text; restore one by replacing gTE's text and, if in
    Writer mode, rebuilding gHiddenTE from it. Both syncing and
    rebuilding are full-document operations, but they only happen at
    undo/redo-relevant moments (pushes are coalesced per typing run,
    not per keystroke), never per character.

    Undo history is intentionally cleared on every view-mode switch
    and on new/open -- simpler and more predictable than trying to
    make snapshots meaningful across two independently-edited buffers.
*/
#define MAX_UNDO_LEVELS 15

typedef struct {
    Handle textH;
    long length;
    short selStart, selEnd;
} UndoSnapshot;

/*
    Link URLs in Writer mode live here, keyed by a small ID (1-based;
    0 means "no link"). The ID rides along in each run's otherwise-unused
    tsColor.red -- TextEdit already tracks style-run boundaries through
    every insert/delete, so the ID (and therefore the URL) follows the
    linked text automatically with no manual range bookkeeping. Reset
    (gLinkCount = 0) at the start of every BuildHiddenView, since that's
    a full reparse of gTE and re-derives whichever links currently exist.
*/
#define MAX_LINKS MD_MAX_LINKS

/* Global state -- actual storage lives in main.c */
extern WindowPtr gWindow;
extern TEHandle gTE;
extern TEHandle gHiddenTE;
extern TEHandle gActiveTE;
extern ControlHandle gScrollBar;
extern Boolean gScrollBarVisible;
extern Boolean gDone;
extern Boolean gHaveFile;
extern Boolean gDirty;
extern Str255 gFileName;
extern short gVRefNum;
extern MenuHandle gViewMenu;
extern MenuHandle gEditMenu;
extern Boolean gHideMarkdown;
extern short gZoomIndex;
extern short gWindowMode;
extern short gFontChoice;

extern UndoSnapshot gUndoStack[MAX_UNDO_LEVELS];
extern short gUndoCount;
extern UndoSnapshot gRedoStack[MAX_UNDO_LEVELS];
extern short gRedoCount;
extern Boolean gTypingRunActive;

extern Str255 gLinkURLs[MAX_LINKS + 1];
extern short gLinkCount;

/* main.c */
Boolean HasSystem7(void);
void UpdateWindowTitle(void);
void ApplyDocumentFont(void);
void SetWindowMode(short newMode);

/* scrolling.c */
void UpdateScrollbarRange(void);
void AdjustScrollbar(void);
Boolean ScrollCaretIntoView(void);
void RepaintWriterViewForced(void);
void DoScrollClick(Point pt);
void InvalidateHeightCache(void);
Boolean ScrollByKey(unsigned char key);

/* markdown.c */
void ClearStyles(void);
void SuppressDrawing(TEHandle te, Rect *saved);
void RestoreDrawing(TEHandle te, const Rect *saved);
void BuildHiddenView(void);
void SyncHiddenToCanonical(void);
Handle EncodeSelectionAsMarkdown(short start, short end, TEHandle te);
void InsertMarkdownAsStyled(Handle srcH, long srcLen, TEHandle te);
void WrapSelection(char *prefix, char *suffix);
void ApplyHeading(short level);
void DoLink(void);
void ToggleFace(Style face);
void DoLinkHidden(void);
void ToggleCode(void);
void ToggleStrike(void);
void ToggleHighlight(void);
void ToggleHeadingHidden(short level);
void DetectInlineMarkdown(char justTyped);
void DoStyleCommand(short menuItem);
void DrawWriterOverlays(TEHandle te, Boolean revealActive);
void DrawWriterText(TEHandle te);
Boolean WriterHasStippleBackground(void);
Boolean CodeFencesBalanced(void);
void RerenderWriterView(void);
void ClearSelectionStyleHidden(void);
void ClearMarkdownInSelection(void);

/* undo.c */
void ClearUndoRedoStacks(void);
void UpdateEditMenuState(void);
void PushUndoSnapshot(void);
void DoUndo(void);
void DoRedo(void);
void DoCut(void);
void DoCopy(void);
void DoPaste(void);
void DoSelectAll(void);

/* zoom.c */
short CurrentFontSize(void);
void LoadPrefs(void);
void SavePrefs(void);
void DoZoom(short direction);
void DoZoomReset(void);
short BodyFontNum(void);
ConstStr255Param FontChoiceName(short choice);
void DoPreferences(void);

/* file.c */
void ShowError(StringPtr msg);
Boolean DocCanGrowBy(TEHandle te, long addLen);
void SetViewMode(Boolean hideMarkdown);
void DoStartupOpen(void);
Boolean DoSaveAs(void);
Boolean DoSave(void);
Boolean ConfirmDiscardChanges(void);
Boolean DoOpenFile(void);
void DoNewFile(void);

/* print.c */
void DoPageSetup(void);
void DoPrint(void);

/* find.c */
void DoFindReplace(void);
void DoFindAgain(void);
void DoWordCount(void);

/* splash.c */
void ShowSplashScreen(void);
void ShowAboutBox(void);

#endif

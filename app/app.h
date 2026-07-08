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
#include <Multiverse.h>
#include <string.h>
#include "mdcore.h"

#define MARGIN_H     64
#define MARGIN_TOP   32
#define MARGIN_BOTTOM 24
#define MENU_BAR_HEIGHT 20
#define FONT_SIZE 18
#define SCROLLBAR_WIDTH 16
#define kBackspaceKey 0x08
#define kReturnKey    0x0D
#define kEnterKey     0x03
#define kEscapeKey    0x1B

#define mFile    128
#define iNew     1
#define iOpen    2
#define iSave    3
#define iSaveAs  4
#define iQuit    6

#define mEdit    131
#define iUndo    1
#define iRedo    2
#define iCut     4
#define iCopy    5
#define iPaste   6
#define iSelectAll 8

#define mStyle   129
#define iBold    1
#define iItalic  2
#define iCode    3
#define iStrike  4
#define iH1      6
#define iH2      7
#define iH3      8
#define iLink    10
#define iNone    12

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

#define mView        130
#define iMarkdownView 1
#define iWriterView  2
#define iZoomIn      4
#define iZoomOut     5
#define iZoomDefault 6

/* The Apple menu (id 1, drawn leftmost) carries About + the desk
   accessories. About lives here, its conventional classic-Mac home,
   rather than in a separate Help menu. */
#define mApple   1
#define iAbout   1

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

#define kZoomPrefType 'ZLvl'
#define kZoomPrefID   128

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

extern UndoSnapshot gUndoStack[MAX_UNDO_LEVELS];
extern short gUndoCount;
extern UndoSnapshot gRedoStack[MAX_UNDO_LEVELS];
extern short gRedoCount;
extern Boolean gTypingRunActive;

extern Str255 gLinkURLs[MAX_LINKS + 1];
extern short gLinkCount;

/* main.c */
void UpdateMenuBarLook(void);
Boolean HasSystem7(void);

/* scrolling.c */
void UpdateScrollbarRange(void);
void AdjustScrollbar(void);
void ScrollCaretIntoView(void);
void DoScrollClick(Point pt);
void InvalidateHeightCache(void);

/* markdown.c */
void ClearStyles(void);
void SuppressDrawing(TEHandle te, Rect *saved);
void RestoreDrawing(TEHandle te, Rect *saved);
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
void ToggleHeadingHidden(short level);
void DetectInlineMarkdown(char justTyped);
void ClearSelectionStyleHidden(void);
void ClearMarkdownInSelection(void);
short AddLinkURL(const unsigned char *url);

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
void LoadZoomPref(void);
void DoZoom(short direction);
void DoZoomReset(void);

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

/* splash.c */
void ShowSplashScreen(void);
void ShowAboutBox(void);

#endif

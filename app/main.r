#include "Types.r"       /* 'STR ' template, for the signature resource */
#include "Finder.r"
#include "Dialogs.r"
#include "Processes.r"   /* SIZE resource type */
#include "Icons.r"       /* ICN# resource type */

/* MultiFinder/System 7 sizing and cooperation flags. acceptSuspendResume
   lets the app dim/undim as it moves between foreground and background
   (see the osEvt handler in main.c). Marked notHighLevelEventAware on
   purpose: document opening uses the classic CountAppFiles mechanism, not
   Apple Events, so the Finder must fall back to that rather than sending
   an unanswered 'odoc'. */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    2048 * 1024,
    1024 * 1024
};

resource 'DITL' (130) {
    {
        {80, 204, 100, 284}, Button { enabled, "Save" },
        {80, 114, 100, 194}, Button { enabled, "Cancel" },
        {80, 14, 100, 104}, Button { enabled, "Don't Save" },
        {10, 14, 70, 284}, StaticText { disabled, "Save changes to \"^0\" before continuing?" }
    }
};

resource 'ALRT' (130) {
    {116, 106, 226, 406},
    130,
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    },
    alertPositionMainScreen
};

resource 'DITL' (131) {
    {
        {206, 60, 228, 180}, Button { enabled, "New Document" },
        {206, 200, 228, 320}, Button { enabled, "Open Document" },
        {12, 15, 196, 365}, UserItem { disabled }
    }
};

resource 'DLOG' (131) {
    {61, 66, 301, 446},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    131,
    "",
    noAutoCenter
};

resource 'DITL' (132) {
    {
        {75, 165, 97, 245}, Button { enabled, "OK" },
        {75, 75, 97, 155}, Button { enabled, "Cancel" },
        {15, 20, 33, 300}, StaticText { disabled, "Link URL:" },
        {38, 20, 58, 300}, EditText { enabled, "" }
    }
};

resource 'DLOG' (132) {
    {126, 96, 236, 416},
    dBoxProc,
    visible,
    noGoAway,
    0,
    132,
    "",
    noAutoCenter
};

resource 'DITL' (133) {
    {
        {206, 140, 228, 240}, Button { enabled, "OK" },
        {12, 15, 196, 365}, UserItem { disabled }
    }
};

resource 'DLOG' (133) {
    {61, 66, 301, 446},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    133,
    "",
    noAutoCenter
};

resource 'DITL' (134) {
    {
        {80, 210, 100, 290}, Button { enabled, "OK" },
        {12, 20, 68, 290}, StaticText { disabled, "^0" }
    }
};

resource 'ALRT' (134) {
    {100, 100, 220, 400},
    134,
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    },
    alertPositionMainScreen
};

/* Print Options: pick the formatted Writer rendering or the raw Markdown
   source before the standard Job dialog. Two radio buttons (managed in
   ShowPrintOptions), a Print default button and a Cancel button. */
resource 'DITL' (135) {
    {
        {96, 210, 116, 290}, Button { enabled, "Print" },
        {96, 120, 116, 200}, Button { enabled, "Cancel" },
        {40, 30, 60, 285},  RadioButton { enabled, "Formatted (Writer view)" },
        {66, 30, 86, 285},  RadioButton { enabled, "Markdown source" },
        {12, 20, 32, 285},  StaticText { disabled, "Print document as:" }
    }
};

resource 'DLOG' (135) {
    {80, 90, 210, 400},
    dBoxProc,
    visible,
    noGoAway,
    0,
    135,
    "",
    noAutoCenter
};

/* Modeless status window shown while the print loop runs. The Printing
   Manager's default idle procedure cancels the job on Command-period; Inside
   Macintosh II says to tell the user that option is available. */
resource 'DITL' (136) {
    {
        {24, 20, 60, 300}, StaticText { disabled, "Printing.  Hold Command-period to cancel." }
    }
};

resource 'DLOG' (136) {
    {100, 106, 184, 426},
    dBoxProc,
    visible,
    noGoAway,
    0,
    136,
    "",
    noAutoCenter
};

/* Preferences: startup defaults for new documents. Four pop-up menus, drawn as
   userItems and driven by PopUpMenuSelect (System-6-safe -- the pop-up control
   CDEF is System 7 only). See DoPreferences in zoom.c. */
resource 'DITL' (137) {
    {
        {158, 215, 178, 285}, Button { enabled, "OK" },
        {158, 130, 178, 200}, Button { enabled, "Cancel" },
        {12, 20, 30, 285},   StaticText { disabled, "Set the defaults for new documents:" },
        {46, 20, 64, 105},   StaticText { disabled, "Open in:" },
        {44, 110, 64, 285},  UserItem { enabled },
        {74, 20, 92, 105},   StaticText { disabled, "View:" },
        {72, 110, 92, 285},  UserItem { enabled },
        {102, 20, 120, 105}, StaticText { disabled, "Font:" },
        {100, 110, 120, 285},UserItem { enabled },
        {130, 20, 148, 105}, StaticText { disabled, "Zoom:" },
        {128, 110, 148, 285},UserItem { enabled }
    }
};

resource 'DLOG' (137) {
    {70, 90, 260, 390},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    137,
    "",
    noAutoCenter
};

/* Find & Replace (ADR 0003). One modal dialog: Find selects the first match
   from the caret; Replace All rewrites every match in the active view. Both use
   the pure MdFind scan -- no System 7 traps. See find.c. */
resource 'DITL' (138) {
    {
        {115, 245, 135, 305}, Button { enabled, "Find" },
        {115, 20, 135, 90},   Button { enabled, "Cancel" },
        {115, 120, 135, 235}, Button { enabled, "Replace All" },
        {15, 15, 33, 90},     StaticText { disabled, "Find:" },
        {13, 95, 33, 300},    EditText { enabled, "" },
        {48, 15, 66, 100},    StaticText { disabled, "Replace with:" },
        {46, 105, 66, 300},   EditText { enabled, "" },
        {82, 95, 100, 260},   CheckBox { enabled, "Case sensitive" }
    }
};

resource 'DLOG' (138) {
    {80, 80, 230, 400},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    138,
    "",
    noAutoCenter
};

resource 'ICN#' (128) {
    {
        $"00000000000000000000000000000000"
        $"003FFF00004000800180008003FFFFC0"
        $"00200040002FFE8000200080004FFC80"
        $"004001000080010007FFFFE03FFFFFFC"
        $"2E0000742EFFFF743EFFFF7C07FFFFE0"
        $"044444201FFFFFF83FFFFFFC3000000C"
        $"3FFFFFFC1FFFFFF80000000000000000"
        $"00000000000000000000000000000000",
        $"00000000000000000000000000000000"
        $"003FFF00007FFF8001FFFF8003FFFFC0"
        $"003FFFC0003FFF80003FFF80007FFF80"
        $"007FFF0000FFFF000FFFFFF03FFFFFFC"
        $"3FFFFFFC3FFFFFFC3FFFFFFC0FFFFFF0"
        $"0FFFFFF03FFFFFFC3FFFFFFC3FFFFFFC"
        $"3FFFFFFC3FFFFFFC0000000000000000"
        $"00000000000000000000000000000000"
    }
};

/* The same typewriter icon, duplicated at the well-known custom-icon ID
   (kCustomIconResource = -16455). With the file's hasCustomIcon Finder flag
   set (done in build-boot-images.sh), the Finder draws this directly off the
   file, without needing the volume's Desktop database to have the app's
   bundle installed -- which a hermetic, Mac-less disk build can't do. */
resource 'ICN#' (-16455) {
    {
        $"00000000000000000000000000000000"
        $"003FFF00004000800180008003FFFFC0"
        $"00200040002FFE8000200080004FFC80"
        $"004001000080010007FFFFE03FFFFFFC"
        $"2E0000742EFFFF743EFFFF7C07FFFFE0"
        $"044444201FFFFFF83FFFFFFC3000000C"
        $"3FFFFFFC1FFFFFF80000000000000000"
        $"00000000000000000000000000000000",
        $"00000000000000000000000000000000"
        $"003FFF00007FFF8001FFFF8003FFFFC0"
        $"003FFFC0003FFF80003FFF80007FFF80"
        $"007FFF0000FFFF000FFFFFF03FFFFFFC"
        $"3FFFFFFC3FFFFFFC3FFFFFFC0FFFFFF0"
        $"0FFFFFF03FFFFFFC3FFFFFFC3FFFFFFC"
        $"3FFFFFFC3FFFFFFC0000000000000000"
        $"00000000000000000000000000000000"
    }
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'FREF' (129) {
    'TEXT', 0, ""
};

resource 'BNDL' (128) {
    'ArtT', 0,
    {
        'ICN#', {
            0, 128
        },
        'FREF', {
            0, 128,
            1, 129
        }
    }
};

/* Signature (creator) resource. The Finder's Desktop Manager binds an app's
   BNDL through a resource whose TYPE is the app's creator code ('ArtT'), at
   ID 0 -- without it the BNDL never registers and the icon stays generic, no
   matter that hasBundle is set (Inside Macintosh: Finder Interface, "Creating
   a Signature Resource"). This is the route the System 6 boot floppy needs:
   System 6 ignores the hasCustomIcon flag entirely (a System 7 Finder
   feature), so the -16455 custom icon above only helps on System 7 -- the
   Desktop-database/BNDL path is the only one that shows the typewriter icon on
   the compact-Mac targets. Conventionally the human-readable version string. */
type 'ArtT' as 'STR ';
resource 'ArtT' (0, purgeable) {
    "ArtfulType 0.5.4-alpha"
};

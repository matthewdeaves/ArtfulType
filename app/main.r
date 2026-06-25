#include "Finder.r"
#include "Dialogs.r"

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

data 'ZLvl' (128) {
    $"0002"
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
    visible,
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

resource 'ICN#' (128) {
    {
        $"000000000000000000007F8000FF8080"
        $"038000800F9FFE803F91FE403F93FE40"
        $"3FD7FE403FDFFE403FCFFE403FCFFF40"
        $"1FCFFD401FCFF9201FCFF1201FEFFF20"
        $"1FE000201FE000201FE000201FE01FA0"
        $"1FE000101FE000101FF000101FF001F0"
        $"0FF1FF800FFFFF800FFFFF800FFFFF80"
        $"0FFFFF800FFFE0000F80000000000000",
        $"000000000000000000007F8000FFFF80"
        $"03FFFF800FFFFF803FFFFFC03FFFFFC0"
        $"3FFFFFC03FFFFFC03FFFFFC03FFFFFC0"
        $"1FFFFFC01FFFFFE01FFFFFE01FFFFFE0"
        $"1FFFFFE01FFFFFE01FFFFFE01FFFFFE0"
        $"1FFFFFF01FFFFFF01FFFFFF01FFFFFF0"
        $"0FFFFF800FFFFF800FFFFF800FFFFF80"
        $"0FFFFF800FFFE0000F80000000000000"
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

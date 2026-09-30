/*
 * debug.c - the on-screen debug message log: up to 30 lines of text drawn over the bottom left of
 * the frame, newest at the bottom, each shown for 600 frames.  Messages are only recorded when the
 * game was started from the editor for test play (giAppmode != 0).  File loading, the story and
 * round logic, script errors, the hit-judge / status display toggles and the netplay code post to
 * it.
 */
#include "kgt.h"

typedef struct {
    int iUnk00;                 /* 0x00 never read or written */
    char szText[64];            /* 0x04 the status text */
} DEBUG_B;                      /* size 0x44 */

/* ---- externs not in globals.h / protos.h ---- */
#define gkgtDebugLog BSS(kgt_debug_a, gkgtDebugLog)  /* 0x46f6c0: on-screen debug messages (test play only) */
#define gDebugStatus BSS(DEBUG_B, gDebugStatus)    /* 0x425a00: debug status line */
#define gbShowDebugStatus BSS(int, gbShowDebugStatus)  /* 0x42471c: draw gDebugStatus at the bottom of the screen; never set by the code */
#define ghFrameDc BSS(HDC, ghFrameDc)              /* 0x4246c0: memory DC of the frame buffer DIB */

void vMemzeroDebugStructs(void);
void vTickDebugEventTimers(void);
void vDrawDebugInfo(void);
/* ---- */

/*
 * Clears the debug message log and the status line.
 * Globals: changes gkgtDebugLog, gDebugStatus.
 */
void vMemzeroDebugStructs(void)
{
    vMemzero(&gkgtDebugLog, sizeof(gkgtDebugLog));
    vMemzero(&gDebugStatus, sizeof(gDebugStatus));
}

/*
 * Adds a message to the debug log (test play only): the lines scroll up by one (line 29, the
 * oldest, is lost) and the text goes into line 0 with a 600-frame timer.
 * szText: the message (at most 63 characters fit; sprintf does not check); iColor: its COLORREF
 * (0x00bbggrr).
 * Returns 0.
 * Globals: reads giAppmode; changes gkgtDebugLog.
 */
int iSetDebugInfo(char *szText, int iColor)
{
    int iLine;

    if (giAppmode == 0)
        return 0;
    /* scroll: line i -> line i + 1, from the oldest down */
    for (iLine = 28; iLine >= 0; iLine--) {
        sprintf(gkgtDebugLog.kgtLines[iLine + 1].szText, "%s", gkgtDebugLog.kgtLines[iLine].szText);
        gkgtDebugLog.kgtLines[iLine + 1].dwColor = gkgtDebugLog.kgtLines[iLine].dwColor;
        gkgtDebugLog.kgtLines[iLine + 1].iTimer = gkgtDebugLog.kgtLines[iLine].iTimer;
    }
    /* the new message, shown for 600 frames (24 s at 25 fps) */
    sprintf(gkgtDebugLog.kgtLines[0].szText, "%s", szText);
    gkgtDebugLog.kgtLines[0].dwColor = iColor;
    gkgtDebugLog.kgtLines[0].iTimer = 600;
    return 0;
}

/*
 * Counts down the display timers of the 30 debug lines (once per frame).
 * Globals: changes gkgtDebugLog.kgtLines[].iTimer.
 */
void vTickDebugEventTimers(void)
{
    int iLine;

    for (iLine = 0; iLine < 30; iLine++) {
        if (gkgtDebugLog.kgtLines[iLine].iTimer)
            gkgtDebugLog.kgtLines[iLine].iTimer--;
    }
}

/*
 * Draws the debug text into the frame buffer DIB with GDI: the status line (if gbShowDebugStatus,
 * which nothing sets) and every debug line whose timer runs, from y = 448 upwards (line 0, the
 * newest, lowest), 16 pixels apart, in its colour with a black outline.  main.c calls it only
 * while the status display (gkgtGameState.uStatusDisplay) is on.
 * Globals: reads ghFrameDc, gbShowDebugStatus, gDebugStatus, gkgtDebugLog.
 */
void vDrawDebugInfo(void)
{
    HDC hDc;
    HFONT hFont;
    HGDIOBJ hOldFont;
    int iLine, iY;
    char szLine[256];

    /* 14x6 "MS PGothic" (Shift-JIS charset) */
    hDc = ghFrameDc;
    hFont = CreateFontA(14, 6, 0, 0, 0, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, 0, DEFAULT_PITCH | FF_MODERN,
                        "\202l\202r \202o\203S\203V\203b\203N");  /* ＭＳ Ｐゴシック (MS PGothic) */
    hOldFont = SelectObject(hDc, hFont);
    SetBkMode(hDc, TRANSPARENT);
    /* status line: blue shadow (COLORREF 0xff0000) one pixel down-right, yellow (0x00ffff) text */
    if (gbShowDebugStatus) {
        SetTextColor(hDc, 0xff0000);
        TextOutA(hDc, 9, 465, gDebugStatus.szText, lstrlenA(gDebugStatus.szText));
        SetTextColor(hDc, 0xffff);
        TextOutA(hDc, 8, 464, gDebugStatus.szText, lstrlenA(gDebugStatus.szText));
    }
    /* the log, bottom up */
    iY = 448;
    for (iLine = 0; iLine < 30; iLine++) {
        if (gkgtDebugLog.kgtLines[iLine].iTimer) {
            sprintf(szLine, "%s", gkgtDebugLog.kgtLines[iLine].szText);
            /* black outline: the text drawn 1 pixel below, right, above and left */
            SetTextColor(hDc, 0);
            TextOutA(hDc, 0, iY + 1, szLine, lstrlenA(szLine));
            TextOutA(hDc, 1, iY, szLine, lstrlenA(szLine));
            TextOutA(hDc, 0, iY - 1, szLine, lstrlenA(szLine));
            TextOutA(hDc, -1, iY, szLine, lstrlenA(szLine));
            /* then the text in its colour */
            SetTextColor(hDc, gkgtDebugLog.kgtLines[iLine].dwColor);
            TextOutA(hDc, 0, iY, szLine, lstrlenA(szLine));
            iY -= 16;
        }
    }
    SelectObject(hDc, hOldFont);
    DeleteObject(hFont);
}

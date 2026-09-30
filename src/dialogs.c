/*
 * dialogs.c - the Win32 user interface around the game window: the game settings dialog, the
 * keyboard and joystick setup dialogs with their key/button capture windows, the about box and the
 * main window's menu commands (the last object of the game, 0x4160f0-0x417850).
 *
 * The dialog templates are in rsrc/kgt2nd.rc (compiled in by src/resources.c); the numbers of
 * their controls are used directly in the code (commented where they appear).  The setup dialogs
 * edit a copy of the layout
 * (gKeyConfigEdit / gJoyConfigEdit): a click on an entry opens a small popup window that waits for
 * a key or joystick button, stores it into the copy and sends WM_COMMAND 10 to the dialog, which
 * then refreshes its button captions; OK copies the layout back.
 */
#include "kgt.h"
#include <commctrl.h>

/* key bindings as edited by the setup dialogs: [player][entry] */
typedef struct { BYTE cKeys[2][17]; } KEYCONFIG;        /* virtual-key codes: up, left, down, right, A-F, pause (bytes 11-16 unused) */
typedef struct { BYTE cButtons[2][7]; } JOYCONFIG;      /* joystick button numbers (from 0) of A-F, pause */

/* ---- externs not (yet) in globals.h / protos.h ---- */
#define giConfigTestplayPlayer0Cpu BSS(int, giConfigTestplayPlayer0Cpu)  /* 0x4300e4: test play: player 0 CPU mode */
#define giConfigTestplayPlayer1Cpu BSS(int, giConfigTestplayPlayer1Cpu)  /* 0x4300f4: test play: player 1 CPU mode */
#define giConfigTestplayHitjudge BSS(int, giConfigTestplayHitjudge)  /* 0x430100: test play: show hit boxes */
#define giConfigTestplayGamespeed BSS(int, giConfigTestplayGamespeed)  /* 0x430104: test play: game speed */
#define giConfigTestplayGameinfo BSS(int, giConfigTestplayGameinfo)  /* 0x430108: test play: show game information */
#define giConfigTestplayStageNb BSS(int, giConfigTestplayStageNb)  /* 0x43010c: test play: stage */
#define giConfigTestplayJoystick BSS(int, giConfigTestplayJoystick)  /* 0x430110: use the joysticks */
#define giConfigTestplayTime BSS(int, giConfigTestplayTime)  /* 0x430114: round time setting */
#define giConfigNumberOfRounds BSS(int, giConfigNumberOfRounds)  /* 0x430124: rounds of a single game */
#define giConfigNumberOfRoundsTeamVs BSS(int, giConfigNumberOfRoundsTeamVs)  /* 0x430128: rounds of a team game */
#define ghTrackbarTime BSS(HWND, ghTrackbarTime)   /* 0x424798: settings dialog: round time trackbar */
#define giTrackbarTimePos BSS(LRESULT, giTrackbarTimePos)  /* 0x4247b0: position of ghTrackbarTime */
#define ghTrackbarRounds BSS(HWND, ghTrackbarRounds)  /* 0x4247b4: settings dialog: rounds trackbar */
#define ghTrackbarRoundsTeamVs BSS(HWND, ghTrackbarRoundsTeamVs)  /* 0x4247b8: settings dialog: team rounds trackbar */
#define giTrackbarSpeedPos BSS(LRESULT, giTrackbarSpeedPos)  /* 0x4247bc: position of ghTrackbarSpeed */
#define giTrackbarRoundsPos BSS(LRESULT, giTrackbarRoundsPos)  /* 0x4247c0: position of ghTrackbarRounds */
#define giTrackbarRoundsTeamVsPos BSS(LRESULT, giTrackbarRoundsTeamVsPos)  /* 0x4247f8: position of ghTrackbarRoundsTeamVs */
#define ghTrackbarSpeed BSS(HWND, ghTrackbarSpeed)  /* 0x4247fc: settings dialog: game speed trackbar */
#define gKeyConfigEdit BSS(KEYCONFIG, gKeyConfigEdit)  /* 0x4247c4: key layout being edited in the key dialog */
#define gJoyConfigEdit BSS(JOYCONFIG, gJoyConfigEdit)  /* 0x4247e8: joystick buttons being edited */
/* 0x424800  timer polling the joystick (in the original the last dword of the game's plain .bss,
   whose slot include/game_bss.h keeps as filler; a static here, nothing relies on its place) */
static struct { WORD wId; WORD wUnused; } giJoystickInputTimer;
#define gpJoyInputButton BSS(BYTE *, gpJoyInputButton)  /* 0x4249b0: gJoyConfigEdit entry waiting for a button */
#define ghJoyWindow BSS(HWND, ghJoyWindow)         /* 0x4249b4: joystick input dialog */
#define gpKeyInputTarget BSS(BYTE *, gpKeyInputTarget)  /* 0x4249b8: gKeyConfigEdit entry waiting for a key */
#define giJoyInputPad BSS(int, giJoyInputPad)      /* 0x4249bc: joystick (0/1) being configured */
#define ghKeyInputWindow BSS(HWND, ghKeyInputWindow)  /* 0x4249c0: key input dialog */
#define gcKeyboardControlsSets BSS(KEYCONFIG, gcKeyboardControlsSets)  /* 0x425980: key layout per player: up, left, down, right, A-F, pause (ini "PlayerN KEY ...") */
#define gcJoystickButtons BSS(JOYCONFIG, gcJoystickButtons)  /* 0x445710: joystick button per input A-F, pause, per player (ini "PlayerN JOY ...") */
extern KEYCONFIG gcDefaultKeyboardControls;  /* 0x41f314: default key layout of both players */
extern JOYCONFIG gcDefaultJoystickButtons;   /* 0x41f354: default joystick buttons of both players */
#define giScreenMode BSS(int, giScreenMode)        /* 0x424704: display mode in use: 0 window (RGB555 DIB), 1 full screen (RGB565 surface) */
#define gdwSystemTime BSS(DWORD, gdwSystemTime)    /* 0x447dd4: timeGetTime of the next frame */
#define giConfigGameWindowSizeX BSS(int, giConfigGameWindowSizeX)  /* 0x447f20: window client width (ini GameWindowSize_x) */
#define giConfigGameWindowSizeY BSS(int, giConfigGameWindowSizeY)  /* 0x447f24: window client height (ini GameWindowSize_y) */
#define grDialogRect BSS(RECT, grDialogRect)       /* 0x4247a0: window rectangle of the settings dialog */
void vSetupDdrawPrimarySurface(void);
void vCheckWindowBounds(HWND hWnd);
BOOL CALLBACK iTestplayOptionsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void vRegisterInputWindowClasses(void);
void vFindWindowPos(HWND hWnd);
int iGetInputKey(char *szKeyName, BYTE cVirtualKey);
LRESULT CALLBACK iKeyboardInputWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void vSpawnKeyInputWindow(HWND hWnd);
LRESULT CALLBACK iJoyInputWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void vSpawnJoyInputWindow(HWND hWnd);
BOOL CALLBACK iSetupKeyboardDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK iSetupJoystickDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK iLpDialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void vSpawnAboutDlgBox(HWND hWnd);
void vHandleWmCommand(HWND hWnd, int iCommand);
/* ---- */

char *gpsKeyInput = "KeyInput";  /* 0x41f838: window class of the key capture popup */
char *gpsJoyInput = "JoyInput";  /* 0x41f83c: window class of the joystick button capture popup */

/* ------------------------------------------------------------------------------------------ */
/* test-play options                                                                           */
/* ------------------------------------------------------------------------------------------ */

/*
 * Dialog procedure of the game settings dialog (DIALOG_GAMESPEEDSETUP, menu Option/Game): stage
 * (combo box 14001 = 0x36b1), rounds (trackbar 2020 = 0x7e4, 1-9, value shown in 2021), team
 * rounds (2018 = 0x7e2, 1-4, shown in 2019), game speed (2014 = 0x7de, 1-20, shown in 2015), round
 * time (2016 = 0x7e0, 0-99, shown in 2017) and the joystick check box (13003 = 0x32cb).  The hit
 * box / FPS check boxes (13001, 13002) and the CPU combo boxes (0x3f1, 0x3f2) are set or read too,
 * but only exist in other templates (DIALOG_GAMESPEEDSETUP2 has the check boxes).  OK (2010 =
 * 0x7da) stores the values, Cancel (2013 = 0x7dd) drops them.
 * hDlg, uMsg, wParam, lParam: the usual.
 * Returns TRUE for handled messages.
 * Globals: reads gkgtKgtSystem.szStageNames, giAppmode; changes the giConfig* settings, the
 * trackbar handles and positions.
 */
BOOL CALLBACK iTestplayOptionsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hCombo;
    int iStage;

    switch (uMsg) {
    case WM_INITDIALOG:
        /* stage names of the system file (empty names skipped) */
        hCombo = GetDlgItem(hDlg, 0x36b1);
        for (iStage = 0; iStage < 50; iStage++) {
            if (gkgtKgtSystem.szStageNames[iStage][0])
                SendMessageA(hCombo, CB_INSERTSTRING, -1, (LPARAM)gkgtKgtSystem.szStageNames[iStage]);
        }
        SendMessageA(hCombo, CB_SETCURSEL, giConfigTestplayStageNb, 0);
        /* check boxes: hit boxes, FPS/game information, joystick (set twice in the original) */
        CheckDlgButton(hDlg, 0x32c9, giConfigTestplayHitjudge);
        CheckDlgButton(hDlg, 0x32ca, giConfigTestplayGameinfo);
        CheckDlgButton(hDlg, 0x32cb, giConfigTestplayJoystick);
        CheckDlgButton(hDlg, 0x32cb, giConfigTestplayJoystick);

        /* trackbars and their value labels */
        ghTrackbarSpeed = GetDlgItem(hDlg, 0x7de);
        SendMessageA(ghTrackbarSpeed, TBM_SETRANGE, TRUE, MAKELONG(1, 20));
        SendMessageA(ghTrackbarSpeed, TBM_SETPOS, TRUE, giConfigTestplayGamespeed);
        giTrackbarSpeedPos = giConfigTestplayGamespeed;
        SetDlgItemInt(hDlg, 0x7df, giConfigTestplayGamespeed, TRUE);

        ghTrackbarTime = GetDlgItem(hDlg, 0x7e0);
        SendMessageA(ghTrackbarTime, TBM_SETRANGE, TRUE, MAKELONG(0, 99));
        SendMessageA(ghTrackbarTime, TBM_SETPOS, TRUE, giConfigTestplayTime);
        giTrackbarTimePos = giConfigTestplayTime;
        SetDlgItemInt(hDlg, 0x7e1, giConfigTestplayTime, TRUE);

        ghTrackbarRoundsTeamVs = GetDlgItem(hDlg, 0x7e2);
        SendMessageA(ghTrackbarRoundsTeamVs, TBM_SETRANGE, TRUE, MAKELONG(1, 4));
        SendMessageA(ghTrackbarRoundsTeamVs, TBM_SETPOS, TRUE, giConfigNumberOfRoundsTeamVs);
        giTrackbarRoundsTeamVsPos = giConfigNumberOfRoundsTeamVs;
        SetDlgItemInt(hDlg, 0x7e3, giConfigNumberOfRoundsTeamVs, TRUE);

        ghTrackbarRounds = GetDlgItem(hDlg, 0x7e4);
        SendMessageA(ghTrackbarRounds, TBM_SETRANGE, TRUE, MAKELONG(1, 9));
        SendMessageA(ghTrackbarRounds, TBM_SETPOS, TRUE, giConfigNumberOfRounds);
        giTrackbarRoundsPos = giConfigNumberOfRounds;
        SetDlgItemInt(hDlg, 0x7e5, giConfigNumberOfRounds, TRUE);
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, -1);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case 0x7da:
            /* OK: store the settings (the hit box / FPS check boxes are not read back); the CPU
               combo boxes are not in this template, so in test play both CPU modes become 0 */
            if (giAppmode) {
                hCombo = GetDlgItem(hDlg, 0x3f1);
                giConfigTestplayPlayer0Cpu = SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
                hCombo = GetDlgItem(hDlg, 0x3f2);
                giConfigTestplayPlayer1Cpu = SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
            }
            hCombo = GetDlgItem(hDlg, 0x36b1);
            giConfigTestplayStageNb = SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
            giConfigTestplayJoystick = IsDlgButtonChecked(hDlg, 0x32cb);
            giConfigTestplayGamespeed = giTrackbarSpeedPos;
            giConfigTestplayTime = giTrackbarTimePos;
            giConfigNumberOfRoundsTeamVs = giTrackbarRoundsTeamVsPos;
            giConfigNumberOfRounds = giTrackbarRoundsPos;
            EndDialog(hDlg, 1);
            break;
        case 0x7dd:
            /* Cancel */
            EndDialog(hDlg, -1);
            return TRUE;
        }
        return TRUE;

    case WM_HSCROLL:
        /* a trackbar moved: keep its position and show it in the label next to it */
        if (ghTrackbarSpeed == (HWND)lParam) {
            switch (LOWORD(wParam)) {
            case TB_LINEUP: case TB_LINEDOWN: case TB_PAGEUP: case TB_PAGEDOWN:
            case TB_THUMBPOSITION: case TB_THUMBTRACK: case TB_TOP: case TB_BOTTOM:
                giTrackbarSpeedPos = SendMessageA(ghTrackbarSpeed, TBM_GETPOS, 0, 0);
                SetDlgItemInt(hDlg, 0x7df, giTrackbarSpeedPos, TRUE);
                return TRUE;
            }
        }
        if (ghTrackbarTime == (HWND)lParam) {
            switch (LOWORD(wParam)) {
            case TB_LINEUP: case TB_LINEDOWN: case TB_PAGEUP: case TB_PAGEDOWN:
            case TB_THUMBPOSITION: case TB_THUMBTRACK: case TB_TOP: case TB_BOTTOM:
                giTrackbarTimePos = SendMessageA(ghTrackbarTime, TBM_GETPOS, 0, 0);
                SetDlgItemInt(hDlg, 0x7e1, giTrackbarTimePos, TRUE);
                return TRUE;
            }
        }
        if (ghTrackbarRoundsTeamVs == (HWND)lParam) {
            switch (LOWORD(wParam)) {
            case TB_LINEUP: case TB_LINEDOWN: case TB_PAGEUP: case TB_PAGEDOWN:
            case TB_THUMBPOSITION: case TB_THUMBTRACK: case TB_TOP: case TB_BOTTOM:
                giTrackbarRoundsTeamVsPos = SendMessageA(ghTrackbarRoundsTeamVs, TBM_GETPOS, 0, 0);
                SetDlgItemInt(hDlg, 0x7e3, giTrackbarRoundsTeamVsPos, TRUE);
                return TRUE;
            }
        }
        if (ghTrackbarRounds == (HWND)lParam) {
            switch (LOWORD(wParam)) {
            case TB_LINEUP: case TB_LINEDOWN: case TB_PAGEUP: case TB_PAGEDOWN:
            case TB_THUMBPOSITION: case TB_THUMBTRACK: case TB_TOP: case TB_BOTTOM:
                giTrackbarRoundsPos = SendMessageA(ghTrackbarRounds, TBM_GETPOS, 0, 0);
                SetDlgItemInt(hDlg, 0x7e5, giTrackbarRoundsPos, TRUE);
                return TRUE;
            }
        }
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------------------------ */
/* key / button capture windows                                                                */
/* ------------------------------------------------------------------------------------------ */

/*
 * Registers the window classes of the two capture popups ("KeyInput", then "JoyInput" if the
 * first succeeded).
 * Globals: reads ghInstance, gpsKeyInput, gpsJoyInput.
 */
void vRegisterInputWindowClasses(void)
{
    WNDCLASSA wndClass;

    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = iKeyboardInputWndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = ghInstance;
    wndClass.hIcon = NULL;
    wndClass.hCursor = NULL;
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = gpsKeyInput;
    if (RegisterClassA(&wndClass)) {
        wndClass.style = CS_HREDRAW | CS_VREDRAW;
        wndClass.lpfnWndProc = iJoyInputWndProc;
        wndClass.cbClsExtra = 0;
        wndClass.cbWndExtra = 0;
        wndClass.hInstance = ghInstance;
        wndClass.hIcon = NULL;
        wndClass.hCursor = NULL;
        wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wndClass.lpszMenuName = NULL;
        wndClass.lpszClassName = gpsJoyInput;
        RegisterClassA(&wndClass);
    }
}

/*
 * Centres a window on the screen (x rounded to a multiple of 8).
 * hWnd: the window.
 */
void vFindWindowPos(HWND hWnd)
{
    RECT rcWindow;

    /* its size as right/bottom of a rectangle at 0,0 */
    GetWindowRect(hWnd, &rcWindow);
    OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
    MoveWindow(hWnd, ((GetSystemMetrics(SM_CXSCREEN) - rcWindow.right) / 2 + 4) & ~7,
               (GetSystemMetrics(SM_CYSCREEN) - rcWindow.bottom) / 2, rcWindow.right, rcWindow.bottom, FALSE);
}

/*
 * Writes the display name of a virtual key (digits, letters, "#Pad" keys, F keys, named keys; some
 * in half-width katakana).
 * szKeyName: output buffer; cVirtualKey: the VK_ code.
 * Returns 0, or 1 for keys that cannot be assigned (szKeyName is then untouched).
 */
int iGetInputKey(char *szKeyName, BYTE cVirtualKey)
{
    int bUnassignable = 0;

    if (cVirtualKey >= '0' && cVirtualKey <= '9') {
        sprintf(szKeyName, "%d", cVirtualKey - '0');
    } else if (cVirtualKey >= 'A' && cVirtualKey <= 'Z') {
        szKeyName[0] = cVirtualKey;
        szKeyName[1] = 0;
    } else if (cVirtualKey >= VK_NUMPAD0 && cVirtualKey <= VK_NUMPAD9) {
        sprintf(szKeyName, " #Pad  %d", cVirtualKey - VK_NUMPAD0);
    } else if (cVirtualKey >= VK_F1 && cVirtualKey <= VK_F24) {
        sprintf(szKeyName, "F %d", cVirtualKey - (VK_F1 - 1));
    } else {
        switch (cVirtualKey) {
        case 0x01:
            sprintf(szKeyName, "\317\263\275\202\314\215\266\316\336\300\335");  /* ﾏｳｽの左ﾎﾞﾀﾝ (left mouse button) */
            break;
        case 0x02:
            sprintf(szKeyName, "\317\263\275\202\314\211E\316\336\300\335");  /* ﾏｳｽの右ﾎﾞﾀﾝ (right mouse button) */
            break;
        case 0x03:
            sprintf(szKeyName, "\272\335\304\333\260\331\314\336\332\260\270");  /* ｺﾝﾄﾛｰﾙﾌﾞﾚｰｸ (Control-Break) */
            break;
        case 0x04:
            sprintf(szKeyName, "\317\263\275\202\314\222\206\211\233\316\336\300\335");  /* ﾏｳｽの中央ﾎﾞﾀﾝ (middle mouse button) */
            break;
        case 0x08:
            sprintf(szKeyName, "BackSpace");
            break;
        case 0x09:
            sprintf(szKeyName, "Tab");
            break;
        case 0x0c:
            sprintf(szKeyName, "Clear");
            break;
        case 0x0d:
            sprintf(szKeyName, "Enter");
            break;
        case 0x10:
            sprintf(szKeyName, "Shift");
            break;
        case 0x11:
            sprintf(szKeyName, "Ctrl");
            break;
        case 0x12:
            sprintf(szKeyName, "Alt");
            break;
        case 0x13:
            sprintf(szKeyName, "Pause");
            break;
        case 0x1b:
            sprintf(szKeyName, "Esc");
            break;
        case 0x20:
            sprintf(szKeyName, "Space");
            break;
        case 0x21:
            sprintf(szKeyName, "PageUp");
            break;
        case 0x22:
            sprintf(szKeyName, "PageDown");
            break;
        case 0x23:
            sprintf(szKeyName, "End");
            break;
        case 0x24:
            sprintf(szKeyName, "Home");
            break;
        case 0x25:
            sprintf(szKeyName, "<-");
            break;
        case 0x26:
            sprintf(szKeyName, "Up");
            break;
        case 0x27:
            sprintf(szKeyName, "->");
            break;
        case 0x28:
            sprintf(szKeyName, "Dw");
            break;
        case 0x29:
            sprintf(szKeyName, "Select");
            break;
        case 0x2b:
            sprintf(szKeyName, "Execute");
            break;
        case 0x2d:
            sprintf(szKeyName, "Ins");
            break;
        case 0x2e:
            sprintf(szKeyName, "Del");
            break;
        case 0x2f:
            sprintf(szKeyName, "Help");
            break;
        case 0x6a:
            sprintf(szKeyName, " #Pad  *");
            break;
        case 0x6b:
            sprintf(szKeyName, " #Pad  +");
            break;
        case 0x6c:
            sprintf(szKeyName, "\276\312\337\332\260\304\267\260");  /* ｾﾊﾟﾚｰﾄｷｰ (separator key) */
            break;
        case 0x6d:
            sprintf(szKeyName, " #Pad  -");
            break;
        case 0x6e:
            sprintf(szKeyName, " #Pad  .");
            break;
        case 0x6f:
            sprintf(szKeyName, " #Pad  /");
            break;
        /* OEM keys (US layout names) */
        case 0xc0:
            sprintf(szKeyName, "~");
            break;
        case 0xbd:
            sprintf(szKeyName, "-");
            break;
        case 0xbb:
            sprintf(szKeyName, "=");
            break;
        case 0xdc:
            sprintf(szKeyName, "\\");
            break;
        case 0xdb:
            sprintf(szKeyName, "[");
            break;
        case 0xdd:
            sprintf(szKeyName, "]");
            break;
        case 0xba:
            sprintf(szKeyName, ";");
            break;
        case 0xde:
            sprintf(szKeyName, "'");
            break;
        case 0xbc:
            sprintf(szKeyName, "<");
            break;
        case 0xbe:
            sprintf(szKeyName, ">");
            break;
        case 0xbf:
            sprintf(szKeyName, "/");
            break;
        case 0x92:
            sprintf(szKeyName, " #Pad  =");
            break;
        case 0xdf:
            sprintf(szKeyName, "_");
            break;
        case 0xe2:
            sprintf(szKeyName, "_");
            break;
        default:
            bUnassignable = 1;
            break;
        }
    }
    return bUnassignable;
}

/*
 * Window procedure of the key capture popup: centred, closes itself when it loses the focus, shows
 * a prompt, and on the first assignable key stores it at gpKeyInputTarget, tells the parent dialog
 * (WM_COMMAND 10 = refresh) and closes.
 * hWnd, uMsg, wParam, lParam: the usual.
 * Globals: reads gpKeyInputTarget (and writes through it).
 */
LRESULT CALLBACK iKeyboardInputWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC hDc;
    HFONT hFont;
    HGDIOBJ hOldFont;
    PAINTSTRUCT ps;
    char szText[256];

    switch (uMsg) {
    case WM_CREATE:
        vFindWindowPos(hWnd);
        return 0;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE)
            DestroyWindow(hWnd);
        return 0;

    case WM_PAINT:
        /* validate, then draw the prompt through a window DC in 16x8 "MS Gothic" */
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        hDc = GetDC(hWnd);
        hFont = CreateFontA(16, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN,
                            "\202l\202r \203S\203V\203b\203N");  /* ＭＳ ゴシック (MS Gothic) */
        if (hFont == NULL)
            MessageBoxA(NULL, "\216w\222\350\202\314\203t\203H\203\223\203g[\202l\202r \203S\203V\203b\203N]\202\252\214\251\202\302\202\251\202\350\202\334\202\271\202\361",  /* 指定のフォント[ＭＳ ゴシック]が見つかりません (the font [MS Gothic] was not found) */
                        "\212m\224F", MB_TASKMODAL);  /* 確認 (confirmation) */
        hOldFont = SelectObject(hDc, hFont);
        SetBkMode(hDc, TRANSPARENT);
        SetTextColor(hDc, GetSysColor(COLOR_HIGHLIGHT));
        sprintf(szText, "Press a button a your keyboard");
        TextOutA(hDc, 8, 8, szText, lstrlenA(szText));
        SelectObject(hDc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(hWnd, hDc);
        return 0;

    case WM_KEYDOWN:
        /* wParam = virtual-key code; keys without a name are ignored */
        if (iGetInputKey(szText, (BYTE)wParam) == 0) {
            *gpKeyInputTarget = (BYTE)wParam;
            SendMessageA(GetParent(hWnd), WM_COMMAND, 10, 0);
            DestroyWindow(hWnd);
        }
        return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/*
 * Opens the key capture popup (256x32) over a setup dialog.
 * hWnd: the dialog (the popup's parent).
 * Globals: reads gpsKeyInput, ghInstance; changes ghKeyInputWindow.
 */
void vSpawnKeyInputWindow(HWND hWnd)
{
    ghKeyInputWindow = CreateWindowExA(0, gpsKeyInput, "KeyInput", WS_POPUP, 0, 0, 256, 32, hWnd, NULL, ghInstance, NULL);
    ShowWindow(ghKeyInputWindow, SW_SHOW);
}

/*
 * Window procedure of the joystick button capture popup: polls joystick giJoyInputPad with a 10 ms
 * timer and, when a button is down, stores its number (the highest one pressed) at
 * gpJoyInputButton, tells the parent dialog (WM_COMMAND 10 = refresh) and closes.  Closes itself
 * when it loses the focus.
 * hWnd, uMsg, wParam, lParam: the usual.
 * Returns 0 for handled messages.
 * Globals: reads giJoyInputPad, gpJoyInputButton (and writes through it); changes
 * giJoystickInputTimer.
 */
LRESULT CALLBACK iJoyInputWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC hDc;
    HFONT hFont;
    HGDIOBJ hOldFont;
    MMRESULT iStackPad1;  /* unused */
    DWORD dwButtons;
    int iButton;
    char szText[256];

    switch (uMsg) {
    case WM_CREATE:
        vFindWindowPos(hWnd);
        giJoystickInputTimer.wId = SetTimer(hWnd, 1, 10, NULL);
        return 0;

    case WM_DESTROY:
        if (giJoystickInputTimer.wId)
            KillTimer(hWnd, giJoystickInputTimer.wId);
        giJoystickInputTimer.wId = 0;
        return DefWindowProcA(hWnd, WM_DESTROY, wParam, lParam);

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_PAINT:
        {
        PAINTSTRUCT ps;

        /* validate, then draw the prompt through a window DC in 16x8 "MS Gothic" */
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        hDc = GetDC(hWnd);
        hFont = CreateFontA(16, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN,
                            "\202l\202r \203S\203V\203b\203N");  /* ＭＳ ゴシック (MS Gothic) */
        if (hFont == NULL)
            MessageBoxA(NULL, "\216w\222\350\202\314\203t\203H\203\223\203g[\202l\202r \203S\203V\203b\203N]\202\252\214\251\202\302\202\251\202\350\202\334\202\271\202\361",  /* 指定のフォント[ＭＳ ゴシック]が見つかりません (the font [MS Gothic] was not found) */
                        "\212m\224F", MB_TASKMODAL);  /* 確認 (confirmation) */
        hOldFont = SelectObject(hDc, hFont);
        SetBkMode(hDc, TRANSPARENT);
        SetTextColor(hDc, GetSysColor(COLOR_HIGHLIGHT));
        /* the format has no %d: the joystick number argument is unused */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-extra-args"   /* the argument is unused, as in the original */
        sprintf(szText, "Press a Button on your JoyStick                 ", giJoyInputPad + 1);
#pragma GCC diagnostic pop
        TextOutA(hDc, 8, 8, szText, lstrlenA(szText));
        SelectObject(hDc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(hWnd, hDc);
        }
        break;

    case WM_TIMER:
        {
        JOYINFOEX joyInfo;

        /* restart the 10 ms timer, then poll the joystick */
        if (giJoystickInputTimer.wId)
            KillTimer(hWnd, giJoystickInputTimer.wId);
        giJoystickInputTimer.wId = 0;
        giJoystickInputTimer.wId = SetTimer(hWnd, 1, 10, NULL);
        if (giJoystickInputTimer.wId) {
            joyInfo.dwSize = sizeof(joyInfo);
            joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
            switch (giJoyInputPad) {
            case 0:
                if (joyGetPosEx(JOYSTICKID1, &joyInfo) != JOYERR_NOERROR)
                    goto handled;
                break;
            case 1:
                if (joyGetPosEx(JOYSTICKID2, &joyInfo) != JOYERR_NOERROR)
                    goto handled;
                break;
            default:
                goto handled;
            }
            /* a button is down: its number = index of the highest set bit of dwButtons */
            dwButtons = joyInfo.dwButtons;
            if (dwButtons) {
                for (iButton = 0; dwButtons > 1; dwButtons >>= 1)
                    iButton++;
                *gpJoyInputButton = iButton;
                SendMessageA(GetParent(hWnd), WM_COMMAND, 10, 0);
                PostMessageA(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
        }
        }
        break;

    default:
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }
handled:
    return 0;
}

/*
 * Opens the joystick button capture popup (400x32) over the joystick setup dialog.
 * hWnd: the dialog (the popup's parent).
 * Globals: reads gpsJoyInput, ghInstance; changes ghJoyWindow.
 */
void vSpawnJoyInputWindow(HWND hWnd)
{
    ghJoyWindow = CreateWindowExA(0, gpsJoyInput, "JoyInput", WS_POPUP, 0, 0, 400, 32, hWnd, NULL, ghInstance, NULL);
    ShowWindow(ghJoyWindow, SW_SHOW);
}

/* ------------------------------------------------------------------------------------------ */
/* setup dialogs                                                                               */
/* ------------------------------------------------------------------------------------------ */

/*
 * Dialog procedure of the keyboard setup dialog (DIALOG_Setup_KeyBoard, menu Option/KeyBoard).
 * Its buttons show the keys: 1001-1011 (0x3e9-0x3f3) player 1 and 1021-1031 (0x3fd-0x407)
 * player 2, in the order of KEYCONFIG (up, left, down, right, A-F, pause); a click opens the key
 * capture popup for that entry.  WM_COMMAND 10 refreshes the captions, OK (IDOK) keeps the edited
 * layout, Cancel drops it, Default (2011 = 0x7db) loads the default keys.
 * hDlg, uMsg, wParam, lParam: the usual.
 * Returns TRUE for handled messages.
 * Globals: changes gKeyConfigEdit, gpKeyInputTarget, gcKeyboardControlsSets (on OK); reads
 * gcDefaultKeyboardControls.
 */
BOOL CALLBACK iSetupKeyboardDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int iEntry;
    char szKeyName[256];

    switch (uMsg) {
    case WM_COMMAND:
        /* an entry button: capture a key for it */
        if (wParam >= 0x3e9 && wParam <= 0x3f3) {
            gpKeyInputTarget = &gKeyConfigEdit.cKeys[0][wParam - 0x3e9];
            vSpawnKeyInputWindow(hDlg);
            return TRUE;
        }
        if (wParam >= 0x3fd && wParam <= 0x407) {
            gpKeyInputTarget = &gKeyConfigEdit.cKeys[1][wParam - 0x3fd];
            vSpawnKeyInputWindow(hDlg);
            return TRUE;
        }
        switch (wParam) {
        case 10:        /* refresh */
            for (iEntry = 0; iEntry < 11; iEntry++) {
                iGetInputKey(szKeyName, gKeyConfigEdit.cKeys[0][iEntry]);
                SetDlgItemTextA(hDlg, 0x3e9 + iEntry, szKeyName);
            }
            for (iEntry = 0; iEntry < 11; iEntry++) {
                iGetInputKey(szKeyName, gKeyConfigEdit.cKeys[1][iEntry]);
                SetDlgItemTextA(hDlg, 0x3fd + iEntry, szKeyName);
            }
            return TRUE;
        case IDOK:
            gcKeyboardControlsSets = gKeyConfigEdit;
            EndDialog(hDlg, 1);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, -1);
            return TRUE;
        case 0x7db:     /* defaults */
            gKeyConfigEdit = gcDefaultKeyboardControls;
            SendMessageA(hDlg, WM_COMMAND, 10, 0);
            break;
        }
        return TRUE;

    case WM_INITDIALOG:
        /* edit a copy of the current layout */
        gKeyConfigEdit = gcKeyboardControlsSets;
        SendMessageA(hDlg, WM_COMMAND, 10, 0);
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, -1);
        break;
    }
    return FALSE;
}

/*
 * Dialog procedure of the joystick setup dialog (DIALOG_Setup_JoyStick, menu Option/JoyStick).
 * Its buttons show the button numbers (from 1): 1005-1011 (0x3ed-0x3f3) A-F and pause of joystick
 * 1, 1025-1031 (0x401-0x407) of joystick 2 (the direction buttons 1001-1004 / 1012-1014 / 1023 are
 * disabled in the template); a click opens the button capture popup for that entry.  WM_COMMAND
 * 10 refreshes the captions, OK keeps the edited buttons, Cancel drops them, Default (2011 =
 * 0x7db) loads the defaults.
 * hDlg, uMsg, wParam, lParam: the usual.
 * Returns TRUE for handled messages.
 * Globals: changes gJoyConfigEdit, giJoyInputPad, gpJoyInputButton, gcJoystickButtons (on OK);
 * reads gcDefaultJoystickButtons.
 */
BOOL CALLBACK iSetupJoystickDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int iEntry;
    char szButton[256];

    switch (uMsg) {
    case WM_COMMAND:
        /* an entry button: capture a joystick button for it */
        if (wParam >= 0x3ed && wParam <= 0x3f3) {
            giJoyInputPad = 0;
            gpJoyInputButton = &gJoyConfigEdit.cButtons[0][wParam - 0x3ed];
            vSpawnJoyInputWindow(hDlg);
            return TRUE;
        }
        if (wParam >= 0x401 && wParam <= 0x407) {
            giJoyInputPad = 1;
            gpJoyInputButton = &gJoyConfigEdit.cButtons[1][wParam - 0x401];
            vSpawnJoyInputWindow(hDlg);
            return TRUE;
        }
        switch (wParam) {
        case 10:        /* refresh: button numbers shown from 1 */
            for (iEntry = 0; iEntry < 7; iEntry++) {
                sprintf(szButton, "%d", gJoyConfigEdit.cButtons[0][iEntry] + 1);
                SetDlgItemTextA(hDlg, 0x3ed + iEntry, szButton);
            }
            for (iEntry = 0; iEntry < 7; iEntry++) {
                sprintf(szButton, "%d", gJoyConfigEdit.cButtons[1][iEntry] + 1);
                SetDlgItemTextA(hDlg, 0x401 + iEntry, szButton);
            }
            break;
        case IDOK:
            gcJoystickButtons = gJoyConfigEdit;
            EndDialog(hDlg, 1);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, -1);
            return TRUE;
        case 0x7db:     /* defaults */
            gJoyConfigEdit = gcDefaultJoystickButtons;
            SendMessageA(hDlg, WM_COMMAND, 10, 0);
            break;
        }
        return TRUE;

    case WM_INITDIALOG:
        /* edit a copy of the current buttons */
        gJoyConfigEdit = gcJoystickButtons;
        SendMessageA(hDlg, WM_COMMAND, 10, 0);
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, -1);
        break;
    }
    return FALSE;
}

/*
 * Dialog procedure of the about box (ABOUT_DLG): closes on OK (2010 = 0x7da) or the close box.
 * hDlg, uMsg, wParam, lParam: the usual.
 * Returns TRUE for handled messages.
 */
BOOL CALLBACK iLpDialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    case WM_INITDIALOG:
        /* not in the original: the icon control (id 0, text "exe_ico") cannot load the icon by name
           without a .rsrc section, so it is given the embedded one */
        SendDlgItemMessageA(hDlg, 0, STM_SETICON, (WPARAM)hLoadEmbeddedIcon("exe_ico"), 0);
        return TRUE;
    case WM_COMMAND:
        switch (wParam) {
        case 0x7da:
            EndDialog(hDlg, 1);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/*
 * Shows the about box (menu Help/About).  (On this branch the dialogs are shown with
 * iEmbeddedDialogBoxParamA, DialogBoxParamA with the templates compiled in by resources.c.)
 * hWnd: the owner window.
 * Globals: reads ghInstance.
 */
void vSpawnAboutDlgBox(HWND hWnd)
{
    iEmbeddedDialogBoxParamA(ghInstance, "about_dlg", hWnd, iLpDialogFunc, 0);
}

/* ------------------------------------------------------------------------------------------ */
/* main window menu                                                                            */
/* ------------------------------------------------------------------------------------------ */

/*
 * Carries out a menu command of the game window (CUPID_MENU in the .rc; 0x836 and 0xa29 are not
 * in the menu).
 * hWnd: the game window; iCommand: WM_COMMAND's wParam (the command id; notification code 0 for menus).
 * Called by the window procedure (main.c).
 * Globals: changes giScreenMode, gdwSystemTime, giConfigGameWindowSizeX/Y, grDialogRect; reads
 * ghInstance, ghWnd.
 */
void vHandleWmCommand(HWND hWnd, int iCommand)
{
    switch (iCommand) {
    case 0x910:     /* 2320 "Full Screen" (F4): toggle full screen / window */
        giScreenMode = (giScreenMode - 1) & 1;
        vSetupDdrawPrimarySurface();
        gdwSystemTime = timeGetTime();
        break;
    case 0x836:     /* 2102: quit */
        PostQuitMessage(0);
        break;
    case 0x911:     /* 2321 "WindowSize 1x1": client area 640x480 (the height adds the frame
                       with SM_CXFRAME, as in the original) */
        giConfigGameWindowSizeX = 640;
        giConfigGameWindowSizeY = 480;
        GetWindowRect(hWnd, &grDialogRect);
        MoveWindow(hWnd, grDialogRect.left, grDialogRect.top, GetSystemMetrics(SM_CXFRAME) * 2 + 640,
                   GetSystemMetrics(SM_CXFRAME) * 2 + 480 + GetSystemMetrics(SM_CYMENU) + GetSystemMetrics(SM_CYCAPTION), TRUE);
        vCheckWindowBounds(hWnd);
        break;
    case 0x839:     /* 2105 "About" */
        vSpawnAboutDlgBox(hWnd);
        break;
    case 0x91f:     /* 2335 Option/"Game": game settings */
        iEmbeddedDialogBoxParamA(ghInstance, "DIALOG_GAMESPEEDSETUP", hWnd, iTestplayOptionsDlgProc, 0);
        break;
    case 0x9c5:     /* 2501 Option/"KeyBoard" */
        iEmbeddedDialogBoxParamA(ghInstance, "DIALOG_Setup_KeyBoard", hWnd, iSetupKeyboardDlgProc, 0);
        break;
    case 0x9c6:     /* 2502 Option/"JoyStick" */
        iEmbeddedDialogBoxParamA(ghInstance, "DIALOG_Setup_JoyStick", hWnd, iSetupJoystickDlgProc, 0);
        break;
    case 0xa29:     /* 2601: netplay dialog (online.c) */
        vSpawnOnlineDialog(ghInstance, ghWnd);
        break;
    }
}

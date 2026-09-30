/*
 * config.c - the warning message box and the ini files:
 *   * kgt2k.INI (no path, so in the Windows directory): the netplay player and session names;
 *   * game.ini (normal game) or "2D Fighter  Maker 2002.ini" (test play from the editor, which
 *     writes the test-play choices there), both in the executable's directory: test-play options,
 *     numbers of rounds, the system file name, the keyboard/joystick layout and the window position,
 *     size and screen mode.
 * All values live in the section gpsLpAppName ("TestPlay" or "GamePlay", set by WinMain from
 * giAppmode), except File/Filename.  The keys of the controls are "Player<1|2> KEY <name>" and
 * "Player<1|2> JOY <name>" with the names of gpsKeyboardKeyNames / gpsJoystickKeyNames.
 */
#include "kgt.h"

/* ---- externs not in globals.h / protos.h ---- */
extern char *gpsContact;                          /* 0x41e2f4: message box caption */
#define gpsLpAppName BSS(char *, gpsLpAppName)     /* 0x541f7c: ini section name ("TestPlay" or "GamePlay") */
#define gkgtGameState BSS(kgtGameState, gkgtGameState)  /* 0x470020: state of the current game */
#define gszCurrentDirectory BSS(char[256], gszCurrentDirectory)  /* 0x4d1c60: directory of the executable (data files are opened from here) */
#define gcKeyboardControlsSets BSS(BYTE[2][17], gcKeyboardControlsSets)  /* 0x425980: key layout per player: up, left, down, right, A-F, pause (ini "PlayerN KEY ...") */
#define gcJoystickButtons BSS(BYTE[2][7], gcJoystickButtons)  /* 0x445710: joystick button per input A-F, pause, per player (ini "PlayerN JOY ...") */
#define giConfigTestplayPlayer0Nb BSS(int, giConfigTestplayPlayer0Nb)  /* 0x4300e0: test play: character of player 0 */
#define giConfigTestplayPlayer0Cpu BSS(int, giConfigTestplayPlayer0Cpu)  /* 0x4300e4: test play: player 0 CPU mode */
#define giConfigTestplayPlayer1Nb BSS(int, giConfigTestplayPlayer1Nb)  /* 0x4300f0: test play: character of player 1 */
#define giConfigTestplayPlayer1Cpu BSS(int, giConfigTestplayPlayer1Cpu)  /* 0x4300f4: test play: player 1 CPU mode */
#define giConfigTestplayHitjudge BSS(int, giConfigTestplayHitjudge)  /* 0x430100: test play: show hit boxes */
#define giConfigTestplayGamespeed BSS(int, giConfigTestplayGamespeed)  /* 0x430104: test play: game speed */
#define giConfigTestplayGameinfo BSS(int, giConfigTestplayGameinfo)  /* 0x430108: test play: show game information */
#define giConfigTestplayStageNb BSS(int, giConfigTestplayStageNb)  /* 0x43010c: test play: stage */
#define giConfigTestplayJoystick BSS(int, giConfigTestplayJoystick)  /* 0x430110: use the joysticks */
#define giConfigTestplayTime BSS(int, giConfigTestplayTime)  /* 0x430114: round time setting */
#define giConfigTestplayExit BSS(int, giConfigTestplayExit)  /* 0x430118: test play: quit when the window loses focus */
#define giConfigTestplayVsMode BSS(int, giConfigTestplayVsMode)  /* 0x430120: test play: versus mode */
#define giConfigNumberOfRounds BSS(int, giConfigNumberOfRounds)  /* 0x430124: rounds of a single game */
#define giConfigNumberOfRoundsTeamVs BSS(int, giConfigNumberOfRoundsTeamVs)  /* 0x430128: rounds of a team game */
#define gszConfigReturnedFilename BSS(char[MAX_PATH], gszConfigReturnedFilename)  /* 0x43012c: system file name (ini File/Filename) */
#define giConfigEditorDemoNb BSS(int, giConfigEditorDemoNb)  /* 0x43022c: demo chosen in the editor (Editer.DemoNb) */
#define giConfigGameWindowPointX BSS(int, giConfigGameWindowPointX)  /* 0x425a48: window x (ini GameWindowPoint_x) */
#define giConfigGameWindowPointY BSS(int, giConfigGameWindowPointY)  /* 0x425a4c: window y (ini GameWindowPoint_y) */
#define giConfigGameWindowSizeX BSS(int, giConfigGameWindowSizeX)  /* 0x447f20: window client width (ini GameWindowSize_x) */
#define giConfigGameWindowSizeY BSS(int, giConfigGameWindowSizeY)  /* 0x447f24: window client height (ini GameWindowSize_y) */
#define giConfigGameScreenMode BSS(int, giConfigGameScreenMode)  /* 0x4d1d60: display mode setting (ini GameScreenMode): 0 window, 1 full screen */
#define giHitJudge BSS(int, giHitJudge)            /* 0x42470c: hit-judge display (hit boxes and player state), copy of giConfigTestplayHitjudge; the next int (0x424710) enables the line switch button */

void vSaveOnlineNamesToIni(void);
void vLoadKgt2kConfig(void);
void vLoadGameConfig(void);
void vSaveGameConfig(void);
/* ---- */

char *gpsKgt2kConfigFilename = "kgt2k.INI";  /* 0x41f28c: ini file of the netplay names (Windows directory) */
char *gpsPlayerNameKey = "PlayerName";  /* 0x41f290: ini key: netplay player name (gszPlayerName) */
char *gpsSessionNameKey = "SessionName";  /* 0x41f294: ini key: netplay session name (gszSessionName) */
char *gpsTestmodeGameConfigFilename = "2D Fighter  Maker 2002.ini";  /* 0x41f298: ini file in test play (the editor's; the double space is in the original) */
char *gpsGameConfigFilename = "game.ini";  /* 0x41f29c: ini file of the normal game */
char *gpsFileSection = "File";  /* 0x41f2a0: ini section of the system file name */
char *gpsFilenameKey = "Filename";  /* 0x41f2a4: ini key: system file name (gszConfigReturnedFilename) */
char *gpsKeyTestplayPlayer0Nb = "Editor.TestPlay.Player0.nb";  /* 0x41f2a8: ini key: giConfigTestplayPlayer0Nb (read only) */
char *gpsKeyTestplayPlayer0Cpu = "Editor.TestPlay.Player0.cpu";  /* 0x41f2ac: ini key: giConfigTestplayPlayer0Cpu */
char *gpsKeyTestplayPlayer1Nb = "Editor.TestPlay.Player1.nb";  /* 0x41f2b0: ini key: giConfigTestplayPlayer1Nb (read only) */
char *gpsKeyTestplayPlayer1Cpu = "Editor.TestPlay.Player1.cpu";  /* 0x41f2b4: ini key: giConfigTestplayPlayer1Cpu */
char *gpsKeyTestplayGameSpeed = "Editor.TestPlay.GameSpeed";  /* 0x41f2b8: ini key: giConfigTestplayGamespeed (default 10) */
char *gpsKeyTestplayHitJudge = "Editor.TestPlay.HitJudge";  /* 0x41f2bc: ini key: giConfigTestplayHitjudge */
char *gpsKeyTestplayGameInformation = "Editor.TestPlay.GameInformation";  /* 0x41f2c0: ini key: giConfigTestplayGameinfo */
char *gpsKeyTestplayStageNb = "Editor.TestPlay.StageNb";  /* 0x41f2c4: ini key: giConfigTestplayStageNb */
char *gpsKeyTestplayWay = "Editor.TestPlay.way";  /* 0x41f2c8: ini key (not used by the game) */
char *gpsKeyTestplayJoyStick = "Editor.TestPlay.JoyStick";  /* 0x41f2cc: ini key: giConfigTestplayJoystick (default 1) */
char *gpsKeyTestplayTime = "Editor.TestPlay.time";  /* 0x41f2d0: ini key: giConfigTestplayTime (default 60) */
char *gpsKeyTestplayExit = "Editor.TestPlay.exit";  /* 0x41f2d4: ini key: giConfigTestplayExit */
char *gpsKeyTestplayVSMode = "Editor.TestPlay.VSMode";  /* 0x41f2d8: ini key: giConfigTestplayVsMode */
char *gpsKeyTestplayVSSinglePlay = "Editor.TestPlay.VSSinglePlay";  /* 0x41f2dc: ini key: giConfigNumberOfRounds (default 3) */
char *gpsKeyTestplayVSTeamPlay = "Editor.TestPlay.VSTeamPlay";  /* 0x41f2e0: ini key: giConfigNumberOfRoundsTeamVs (default 3) */
char *gpsKeyEditorDemoNb = "Editer.DemoNb";  /* 0x41f2e4: ini key: giConfigEditorDemoNb (read only; "Editer" sic) */
/* 0x41f2e8: ini key names of the keyboard layout, "Player<n> KEY <name>".  NB: gcKeyboardControlsSets
   holds up, left, down, right, but the names of [1] and [3] say right and left: the key stored as
   "右" (right) is the one that acts as left (defaults F / numpad 4). */
char *gpsKeyboardKeyNames[11] = {
    "\217\343",     /* 上 (up) */
    "\211E",        /* 右 (right) */
    "\211\272",     /* 下 (down) */
    "\215\266",     /* 左 (left) */
    "\202`",        /* Ａ */
    "\202a",        /* Ｂ */
    "\202b",        /* Ｃ */
    "\202c",        /* Ｄ */
    "\202d",        /* Ｅ */
    "\202e",        /* Ｆ */
    "PAUSE",
};
/* default keys: up, left, down, right, A-F, pause (virtual-key codes; bytes 11-16 unused) */
BYTE gcDefaultKeyboardControls[2][17] = {  /* 0x41f314: default key layout of both players */
    { 'T', 'F', 'B', 'H', 'A', 'S', 'D', 'Q', 'W', 'E', VK_ESCAPE },
    { VK_NUMPAD8, VK_NUMPAD4, VK_NUMPAD2, VK_NUMPAD6, 'J', 'K', 'L', 'I', 'O', 'P', VK_ESCAPE },
};
char *gpsJoystickKeyNames[7] = {  /* 0x41f338: ini key names of the joystick buttons, "Player<n> JOY <name>" */
    "\202`",        /* Ａ */
    "\202a",        /* Ｂ */
    "\202b",        /* Ｃ */
    "\202c",        /* Ｄ */
    "\202d",        /* Ｅ */
    "\202e",        /* Ｆ */
    "PAUSE",
};
/* default joystick buttons for A-F, pause (button numbers from 0 = bit of JOYINFOEX.dwButtons) */
BYTE gcDefaultJoystickButtons[2][7] = {  /* 0x41f354: default joystick buttons of both players */
    { 0, 1, 2, 3, 4, 5, 9 },
    { 0, 1, 2, 3, 4, 5, 9 },
};
char *gpsKeyGameWindowSizeX = "GameWindowSize_x";  /* 0x41f364: ini key: giConfigGameWindowSizeX */
char *gpsKeyGameWindowSizeY = "GameWindowSize_y";  /* 0x41f368: ini key: giConfigGameWindowSizeY */
char *gpsKeyGameWindowPointX = "GameWindowPoint_x";  /* 0x41f36c: ini key: giConfigGameWindowPointX */
char *gpsKeyGameWindowPointY = "GameWindowPoint_y";  /* 0x41f370: ini key: giConfigGameWindowPointY */
char *gpsKeyGameScreenMode = "GameScreenMode";  /* 0x41f374: ini key: giConfigGameScreenMode */

/*
 * Shows a task-modal warning box (the loaders' error messages).
 * szText: the message.
 * Globals: reads ghWnd, gpsContact (the caption, "連絡" = notice).
 */
void vSpawnTaskModalWithWarning(char *szText)
{
    MessageBoxA(ghWnd, szText, gpsContact, MB_TASKMODAL | MB_ICONEXCLAMATION);
}

/*
 * Saves the netplay player and session names to kgt2k.INI (at exit).
 * Globals: reads gpsLpAppName, gszPlayerName, gszSessionName.
 */
void vSaveOnlineNamesToIni(void)
{
    WritePrivateProfileStringA(gpsLpAppName, gpsPlayerNameKey, gszPlayerName, gpsKgt2kConfigFilename);
    WritePrivateProfileStringA(gpsLpAppName, gpsSessionNameKey, gszSessionName, gpsKgt2kConfigFilename);
}

/*
 * Loads the netplay player and session names from kgt2k.INI (defaults "Player 1", "Session 1").
 * Globals: reads gpsLpAppName; changes gszPlayerName, gszSessionName.
 */
void vLoadKgt2kConfig(void)
{
    GetPrivateProfileStringA(gpsLpAppName, gpsPlayerNameKey, "Player 1", gszPlayerName, 32, gpsKgt2kConfigFilename);
    GetPrivateProfileStringA(gpsLpAppName, gpsSessionNameKey, "Session 1", gszSessionName, 32, gpsKgt2kConfigFilename);
}

/*
 * Loads the settings from game.ini (normal game) or "2D Fighter  Maker 2002.ini" (test play) in
 * the executable's directory; missing keys get the defaults below (game speed 10, joystick on, time
 * 60, 3 rounds, the default controls, the rest 0).
 * Globals: reads giAppmode, gszCurrentDirectory, gpsLpAppName, the defaults; changes the
 * giConfig* settings, gszConfigReturnedFilename, gcKeyboardControlsSets, gcJoystickButtons and
 * giHitJudge.
 */
void vLoadGameConfig(void)
{
    char szIniPath[256];
    char szKey[256];
    int iPlayer, iEntry;

    /* "<exe directory>\game.ini" or "<exe directory>\2D Fighter  Maker 2002.ini" */
    if (giAppmode == 0)
        sprintf(szIniPath, "%s\\%s", gszCurrentDirectory, gpsGameConfigFilename);
    else
        sprintf(szIniPath, "%s\\%s", gszCurrentDirectory, gpsTestmodeGameConfigFilename);
    /* test-play options and rounds */
    giConfigTestplayPlayer0Nb = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayPlayer0Nb, 0, szIniPath);
    giConfigTestplayPlayer0Cpu = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayPlayer0Cpu, 0, szIniPath);
    giConfigTestplayPlayer1Nb = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayPlayer1Nb, 0, szIniPath);
    giConfigTestplayPlayer1Cpu = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayPlayer1Cpu, 0, szIniPath);
    giConfigTestplayGamespeed = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayGameSpeed, 10, szIniPath);
    giConfigTestplayHitjudge = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayHitJudge, 0, szIniPath);
    giConfigTestplayGameinfo = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayGameInformation, 0, szIniPath);
    giConfigTestplayStageNb = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayStageNb, 0, szIniPath);
    giConfigTestplayJoystick = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayJoyStick, 1, szIniPath);
    giConfigTestplayTime = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayTime, 60, szIniPath);
    giConfigTestplayExit = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayExit, 0, szIniPath);
    giConfigTestplayVsMode = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayVSMode, 0, szIniPath);
    giConfigNumberOfRoundsTeamVs = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayVSTeamPlay, 3, szIniPath);
    giConfigNumberOfRounds = GetPrivateProfileIntA(gpsLpAppName, gpsKeyTestplayVSSinglePlay, 3, szIniPath);
    /* [File] Filename: the system file to load (default "") */
    GetPrivateProfileStringA(gpsFileSection, gpsFilenameKey, "", gszConfigReturnedFilename, MAX_PATH, szIniPath);
    /* controls of both players: "Player1 KEY 上", ..., "Player2 JOY PAUSE" */
    for (iPlayer = 0; iPlayer < 2; iPlayer++) {
        for (iEntry = 0; iEntry < 11; iEntry++) {
            sprintf(szKey, "Player%d KEY %s", iPlayer + 1, gpsKeyboardKeyNames[iEntry]);
            gcKeyboardControlsSets[iPlayer][iEntry] = GetPrivateProfileIntA(gpsLpAppName, szKey, gcDefaultKeyboardControls[iPlayer][iEntry], szIniPath);
        }
        for (iEntry = 0; iEntry < 7; iEntry++) {
            sprintf(szKey, "Player%d JOY %s", iPlayer + 1, gpsJoystickKeyNames[iEntry]);
            gcJoystickButtons[iPlayer][iEntry] = GetPrivateProfileIntA(gpsLpAppName, szKey, gcDefaultJoystickButtons[iPlayer][iEntry], szIniPath);
        }
    }
    /* editor demo, window placement and screen mode */
    giConfigEditorDemoNb = GetPrivateProfileIntA(gpsLpAppName, gpsKeyEditorDemoNb, 0, szIniPath);
    giConfigGameWindowPointX = GetPrivateProfileIntA(gpsLpAppName, gpsKeyGameWindowPointX, 0, szIniPath);
    giConfigGameWindowPointY = GetPrivateProfileIntA(gpsLpAppName, gpsKeyGameWindowPointY, 0, szIniPath);
    giConfigGameWindowSizeX = GetPrivateProfileIntA(gpsLpAppName, gpsKeyGameWindowSizeX, 0, szIniPath);
    giConfigGameWindowSizeY = GetPrivateProfileIntA(gpsLpAppName, gpsKeyGameWindowSizeY, 0, szIniPath);
    giConfigGameScreenMode = GetPrivateProfileIntA(gpsLpAppName, gpsKeyGameScreenMode, 0, szIniPath);
    giHitJudge = giConfigTestplayHitjudge;
}

/*
 * Saves the settings to the same ini file as vLoadGameConfig (at exit), all as unsigned decimal
 * strings.  Not saved: the characters of the test-play players (Editor.TestPlay.Player0.nb / Player1.nb),
 * the editor demo and the system file name.
 * Globals: reads giAppmode, gszCurrentDirectory, gpsLpAppName, gkgtGameState.uStatusDisplay,
 * giHitJudge, the giConfig* settings, gcKeyboardControlsSets, gcJoystickButtons; changes
 * giConfigTestplayGameinfo and giConfigTestplayHitjudge (taken from the current game).
 */
void vSaveGameConfig(void)
{
    char szValue[256];
    char szIniPath[256];
    char szKey[256];
    int iPlayer, iEntry;

    /* the status and hit-box displays may have been toggled during the game */
    giConfigTestplayGameinfo = gkgtGameState.uStatusDisplay;
    if (giAppmode == 0)
        sprintf(szIniPath, "%s\\%s", gszCurrentDirectory, gpsGameConfigFilename);
    else
        sprintf(szIniPath, "%s\\%s", gszCurrentDirectory, gpsTestmodeGameConfigFilename);
    giConfigTestplayHitjudge = giHitJudge;
    /* test-play options and rounds */
    wsprintfA(szValue, "%u", giConfigTestplayPlayer0Cpu);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayPlayer0Cpu, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayPlayer1Cpu);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayPlayer1Cpu, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayGamespeed);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayGameSpeed, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayHitjudge);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayHitJudge, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayGameinfo);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayGameInformation, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayStageNb);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayStageNb, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayJoystick);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayJoyStick, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayTime);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayTime, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayExit);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayExit, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigTestplayVsMode);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayVSMode, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigNumberOfRounds);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayVSSinglePlay, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigNumberOfRoundsTeamVs);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyTestplayVSTeamPlay, szValue, szIniPath);
    /* controls of both players */
    for (iPlayer = 0; iPlayer < 2; iPlayer++) {
        for (iEntry = 0; iEntry < 11; iEntry++) {
            sprintf(szKey, "Player%d KEY %s", iPlayer + 1, gpsKeyboardKeyNames[iEntry]);
            wsprintfA(szValue, "%u", gcKeyboardControlsSets[iPlayer][iEntry]);
            WritePrivateProfileStringA(gpsLpAppName, szKey, szValue, szIniPath);
        }
        for (iEntry = 0; iEntry < 7; iEntry++) {
            sprintf(szKey, "Player%d JOY %s", iPlayer + 1, gpsJoystickKeyNames[iEntry]);
            wsprintfA(szValue, "%u", gcJoystickButtons[iPlayer][iEntry]);
            WritePrivateProfileStringA(gpsLpAppName, szKey, szValue, szIniPath);
        }
    }
    /* window placement and screen mode */
    wsprintfA(szValue, "%u", giConfigGameWindowPointX);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyGameWindowPointX, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigGameWindowPointY);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyGameWindowPointY, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigGameWindowSizeX);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyGameWindowSizeX, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigGameWindowSizeY);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyGameWindowSizeY, szValue, szIniPath);
    wsprintfA(szValue, "%u", giConfigGameScreenMode);
    WritePrivateProfileStringA(gpsLpAppName, gpsKeyGameScreenMode, szValue, szIniPath);
}

/*
 * input.c - reading the keyboard and the two joysticks (winmm joyGetPosEx) into the per-frame input
 * words of the players, the input history ring (giInputBuffer) and the menu auto-repeat.
 *
 * Input word bits (giUserKeydowns, giInputBuffer, giLastInput*):
 *   0x001 back  (left outside a fight)      0x010-0x200 buttons A-F
 *   0x002 forward (right outside a fight)   0x400 pause
 *   0x004 up    0x008 down
 * In a fight, left/right are stored relative to the player's facing (a player facing left gets
 * "forward" for the left key), except in guard-button mode where they stay screen left/right.
 */
#include "kgt.h"

/* keyboard layout of one player (virtual-key codes).  NB: the ini key names for [1] and [3] are
   "right" and "left" (see config.c) but the defaults are F/H = left/right. */
typedef struct {
    BYTE cUp;                   /* 0x00 */
    BYTE cLeft;                 /* 0x01 (ini name "右", right) */
    BYTE cDown;                 /* 0x02 */
    BYTE cRight;                /* 0x03 (ini name "左", left) */
    BYTE cButton[6];            /* 0x04 A..F */
    BYTE cPause;                /* 0x0a */
    BYTE cUnk0B[6];             /* 0x0b unused */
} KEY_LAYOUT;                   /* size 17 */

/* ---- externs not in globals.h / protos.h ---- */
#define gkgtGameState BSS(kgtGameState, gkgtGameState)  /* 0x470020: state of the current game */
#define gJoyCaps1 BSS(JOYCAPSA, gJoyCaps1)         /* 0x4249e0: capabilities of joystick 1 */
#define gJoyCaps2 BSS(JOYCAPSA, gJoyCaps2)         /* 0x424b80: capabilities of joystick 2 */
#define gcKeyState BSS(BYTE[256], gcKeyState)      /* 0x424d20: GetKeyboardState buffer */
#define gdwJoystickX BSS(DWORD, gdwJoystickX)      /* 0x424e20: joystick 1 x centre */
#define gdwJoystickY BSS(DWORD, gdwJoystickY)      /* 0x424e24: joystick 1 y centre */
#define gdwJoystickTwoX BSS(DWORD, gdwJoystickTwoX)  /* 0x424e28: joystick 2 x centre */
#define gdwJoystickTwoY BSS(DWORD, gdwJoystickTwoY)  /* 0x424e2c: joystick 2 y centre */
#define giStoryModePlayerIdx BSS(int, giStoryModePlayerIdx)  /* 0x424f20: player (0/1) who started story mode */
#define gcKeyboardControlsSets BSS(KEY_LAYOUT[2], gcKeyboardControlsSets)  /* 0x425980: key layout per player: up, left, down, right, A-F, pause (ini "PlayerN KEY ...") */
#define giUserKeydowns BSS(int[8], giUserKeydowns)  /* 0x4259c0: inputs held this frame per player */
#define giAnyInputXor BSS(int, giAnyInputXor)      /* 0x4280d8: giLastInputXor of all players ORed */
#define gcJoystickButtons BSS(BYTE[2][7], gcJoystickButtons)  /* 0x445710: joystick button per input A-F, pause, per player (ini "PlayerN JOY ...") */
#define giInputBufferPos BSS(int, giInputBufferPos)  /* 0x447ee0: current frame in giInputBuffer (0-1023) */
#define giLastInput BSS(int[8], giLastInput)       /* 0x447f00: inputs held in the previous frame */
#define giLastInputCleaned BSS(int[8], giLastInputCleaned)  /* 0x447f40: newly pressed or auto-repeated inputs (menus) */
#define giLastInputXor BSS(int[8], giLastInputXor)  /* 0x447f60: inputs newly pressed this frame */
#define giAnyInput BSS(int, giAnyInput)            /* 0x4cfa04: giUserKeydowns of all players ORed */
#define giAnyInputCleaned BSS(int, giAnyInputCleaned)  /* 0x4d1c20: giLastInputCleaned of all players ORed */
#define giInputCountdowns BSS(int[8], giInputCountdowns)  /* 0x4d1c40: auto-repeat countdown per player */
#define giRepeatInput BSS(int[8], giRepeatInput)   /* 0x541f80: input compared for auto-repeat */
#define giConfigTestplayJoystick BSS(int, giConfigTestplayJoystick)  /* 0x430110: use the joysticks */
extern int giKeyRepeatDelay;                  /* 0x41e3fc: auto-repeat delay in frames */
extern int giKeyRepeatRate;                   /* 0x41e400: auto-repeat interval in frames */

int iGetJoystickOnePos(void);
int iCheckJoystickOne(void);
BOOL bIsJoystickOneAvailable(void);
int iGetJoystickTwoPos(void);
int iCheckJoystickTwo(void);
BOOL bIsJoystickTwoAvailable(void);
int iTranslateKeyPress(int iController, int iPlayer);
void vGetPlayerInputs(void);
void vPollKeyboardState(void);
int iSubtractTwoFiftySixIfAboveOneTwentySeven(char cValue);
/* ---- */

/* input bits (see the file header; before the tidy-up these two were named the other way round) */
#define IN_BACK     0x001
#define IN_FORWARD  0x002
#define IN_UP       0x004
#define IN_DOWN     0x008
#define IN_A        0x010
#define IN_B        0x020
#define IN_C        0x040
#define IN_D        0x080
#define IN_E        0x100
#define IN_F        0x200
#define IN_PAUSE    0x400

/*
 * Reads joystick 1 and keeps its position as the centre (called at start-up, stick at rest).
 * Returns 1 if the joystick answered, else 0.
 * Globals: changes gdwJoystickX, gdwJoystickY.
 */
int iGetJoystickOnePos(void)
{
    JOYINFOEX joyInfo;

    joyInfo.dwSize = sizeof(joyInfo);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
    if (joyGetPosEx(JOYSTICKID1, &joyInfo) != JOYERR_NOERROR)
        return 0;
    gdwJoystickX = joyInfo.dwXpos;
    gdwJoystickY = joyInfo.dwYpos;
    return 1;
}

/*
 * Start-up check of joystick 1: takes its centre and reads its capabilities (axis ranges).
 * Returns 1 if it is present, else 0 (gJoyCaps1 then stays zero).
 * Globals: changes gdwJoystickX, gdwJoystickY, gJoyCaps1.
 */
int iCheckJoystickOne(void)
{
    if (!iGetJoystickOnePos())
        return 0;
    joyGetDevCapsA(JOYSTICKID1, &gJoyCaps1, sizeof(gJoyCaps1));
    return 1;
}

/*
 * Tells whether joystick 1 answers (not called).
 */
BOOL bIsJoystickOneAvailable(void)
{
    JOYINFOEX joyInfo;

    joyInfo.dwSize = sizeof(joyInfo);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
    return joyGetPosEx(JOYSTICKID1, &joyInfo) == JOYERR_NOERROR;
}

/*
 * The joystick 2 version of iGetJoystickOnePos - but it reads JOYSTICKID1 (a bug of the
 * original), so joystick 2's centre is joystick 1's position.
 * Returns 1 if the (first) joystick answered, else 0.
 * Globals: changes gdwJoystickTwoX, gdwJoystickTwoY.
 */
int iGetJoystickTwoPos(void)
{
    JOYINFOEX joyInfo;

    joyInfo.dwSize = sizeof(joyInfo);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
    if (joyGetPosEx(JOYSTICKID1, &joyInfo) != JOYERR_NOERROR)
        return 0;
    gdwJoystickTwoX = joyInfo.dwXpos;
    gdwJoystickTwoY = joyInfo.dwYpos;
    return 1;
}

/*
 * Start-up check of joystick 2; like iGetJoystickTwoPos it queries JOYSTICKID1 (so gJoyCaps2 gets
 * joystick 1's capabilities).
 * Returns 1 if the (first) joystick is present, else 0.
 * Globals: changes gdwJoystickTwoX, gdwJoystickTwoY, gJoyCaps2.
 */
int iCheckJoystickTwo(void)
{
    if (!iGetJoystickTwoPos())
        return 0;
    joyGetDevCapsA(JOYSTICKID1, &gJoyCaps2, sizeof(gJoyCaps2));
    return 1;
}

/*
 * Tells whether "joystick 2" answers (not called; queries JOYSTICKID1 as well).
 */
BOOL bIsJoystickTwoAvailable(void)
{
    JOYINFOEX joyInfo;

    joyInfo.dwSize = sizeof(joyInfo);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
    return joyGetPosEx(JOYSTICKID1, &joyInfo) == JOYERR_NOERROR;
}

/*
 * Builds the input word of one player from the keyboard (gcKeyState, read by the caller) and,
 * when joysticks are enabled, from joystick iController: stick outside +-3 of 20 steps around the
 * centre gives a direction, the configured buttons give A-F and pause.
 * iController: controls to read (0/1: key layout, joystick); iPlayer: player slot whose facing
 * decides back/forward.
 * Returns the input word (bits in the file header).
 * Globals: reads gkgtGameState.iGameStateNumber, gkgtLoadedCharacter[iPlayer], gcKeyState,
 * gcKeyboardControlsSets, giConfigTestplayJoystick, gJoyCaps1/2, the joystick centres and
 * gcJoystickButtons.
 */
int iTranslateKeyPress(int iController, int iPlayer)
{
    JOYINFOEX joyInfo;
    int iInput = 0;
    int bUnflipped = 0;
    int iStickX, iStickY;

    /* left = back (bit 0) unless the player is in a fight (states 3000-3999), faces left
       (iTargetFacing 1) and is not in guard-button mode (iOptionFlags & 8) */
    if (gkgtGameState.iGameStateNumber < 3000 || gkgtGameState.iGameStateNumber >= 4000
        || !gkgtLoadedCharacter[iPlayer].iTargetFacing
        || (gkgtLoadedCharacter[iPlayer].iOptionFlags & 8))
        bUnflipped = 1;

    /* keyboard: bit 7 of a GetKeyboardState entry = key down */
    if (gcKeyState[gcKeyboardControlsSets[iController].cDown] & 0x80)
        iInput = IN_DOWN;
    if (gcKeyState[gcKeyboardControlsSets[iController].cUp] & 0x80)
        iInput |= IN_UP;
    if (bUnflipped) {
        if (gcKeyState[gcKeyboardControlsSets[iController].cLeft] & 0x80)
            iInput |= IN_BACK;
        if (gcKeyState[gcKeyboardControlsSets[iController].cRight] & 0x80)
            iInput |= IN_FORWARD;
    } else {
        if (gcKeyState[gcKeyboardControlsSets[iController].cLeft] & 0x80)
            iInput |= IN_FORWARD;
        if (gcKeyState[gcKeyboardControlsSets[iController].cRight] & 0x80)
            iInput |= IN_BACK;
    }
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[0]] & 0x80)
        iInput |= IN_A;
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[1]] & 0x80)
        iInput |= IN_B;
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[2]] & 0x80)
        iInput |= IN_C;
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[3]] & 0x80)
        iInput |= IN_D;
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[4]] & 0x80)
        iInput |= IN_E;
    if (gcKeyState[gcKeyboardControlsSets[iController].cButton[5]] & 0x80)
        iInput |= IN_F;
    if (gcKeyState[gcKeyboardControlsSets[iController].cPause] & 0x80)
        iInput |= IN_PAUSE;

    if (!giConfigTestplayJoystick)
        return iInput;

    /* joystick: stick position scaled to about -10..10 around the centre taken at start-up;
       without capabilities (range 0) the raw 0-65535 position / 0xccc gives 0..20 instead, which
       is never centred (the stick then reads as held to the right/down) */
    joyInfo.dwSize = sizeof(joyInfo);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_USEDEADZONE;
    switch (iController) {
    case 0:
        if (joyGetPosEx(JOYSTICKID1, &joyInfo) != JOYERR_NOERROR)
            return iInput;
        if (gJoyCaps1.wXmax - gJoyCaps1.wXmin == 0) {
            iStickX = joyInfo.dwXpos / 0xccc;
            iStickY = joyInfo.dwYpos / 0xccc;
        } else {
            iStickX = (int)((joyInfo.dwXpos - gdwJoystickX) * 20) / (int)(gJoyCaps1.wXmax - gJoyCaps1.wXmin);
            iStickY = (int)((joyInfo.dwYpos - gdwJoystickY) * 20) / (int)(gJoyCaps1.wYmax - gJoyCaps1.wYmin);
        }
        break;
    case 1:
        if (joyGetPosEx(JOYSTICKID2, &joyInfo) != JOYERR_NOERROR)
            return iInput;
        if (gJoyCaps2.wXmax - gJoyCaps2.wXmin == 0) {
            iStickX = joyInfo.dwXpos / 0xccc;
            iStickY = joyInfo.dwYpos / 0xccc;
        } else {
            iStickX = (int)((joyInfo.dwXpos - gdwJoystickTwoX) * 20) / (int)(gJoyCaps2.wXmax - gJoyCaps2.wXmin);
            iStickY = (int)((joyInfo.dwYpos - gdwJoystickTwoY) * 20) / (int)(gJoyCaps2.wYmax - gJoyCaps2.wYmin);
        }
        break;
    default:
        return iInput;
    }

    /* directions, with a dead zone of +-3 */
    if (bUnflipped) {
        if (iStickX < -3)
            iInput |= IN_BACK;
        if (iStickX > 3)
            iInput |= IN_FORWARD;
    } else {
        if (iStickX < -3)
            iInput |= IN_FORWARD;
        if (iStickX > 3)
            iInput |= IN_BACK;
    }
    if (iStickY < -3)
        iInput |= IN_UP;
    if (iStickY > 3)
        iInput |= IN_DOWN;
    /* buttons: gcJoystickButtons holds the button number (bit of dwButtons) of A-F and pause.
       (The count is taken modulo 32 as the x86 shift of the original does; a button number of 32 or
       more from the ini would otherwise be an undefined shift.) */
#define JOY_BUTTON_BIT(cButton) (1u << ((cButton) & 31))
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][0]))
        iInput |= IN_A;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][1]))
        iInput |= IN_B;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][2]))
        iInput |= IN_C;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][3]))
        iInput |= IN_D;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][4]))
        iInput |= IN_E;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][5]))
        iInput |= IN_F;
    if (joyInfo.dwButtons & JOY_BUTTON_BIT(gcJoystickButtons[iController][6]))
        iInput |= IN_PAUSE;
    return iInput;
}

/*
 * Reads this frame's input (once per game frame): the keyboard state, then the input words of the
 * local players - slot 0 with the controls of giStoryModePlayerIdx in story mode (fights only),
 * else slots 0 and 1 with controls 0 and 1 - stored into giUserKeydowns and the next slot of the
 * giInputBuffer ring.  For all 8 slots it then derives the new presses (giLastInputXor) and the
 * menu input with auto-repeat (giLastInputCleaned: the press itself, then after giKeyRepeatDelay
 * frames every giKeyRepeatRate frames while the same input is held), and the ORs over all players.
 * Globals: reads gkgtGameState, giStoryModePlayerIdx, giKeyRepeatDelay, giKeyRepeatRate; changes
 * gcKeyState, giInputBufferPos, giInputBuffer, giUserKeydowns, giLastInput, giLastInputXor,
 * giLastInputCleaned, giInputCountdowns, giRepeatInput, giAnyInput, giAnyInputXor,
 * giAnyInputCleaned.
 */
void vGetPlayerInputs(void)
{
    int iPlayer;

    /* read the keyboard and advance the history ring (1024 frames) */
    GetKeyboardState(gcKeyState);
#ifdef KGT_TRACE
    { void vTraceInput(BYTE *pKeyState); vTraceInput(gcKeyState); }    /* debug: scripted keys (see README) */
#endif
    giInputBufferPos = (giInputBufferPos + 1) & 0x3ff;
    for (iPlayer = 0; iPlayer < 8; iPlayer++)
        giUserKeydowns[iPlayer] = 0;
    if (gkgtGameState.iGameStateNumber >= 3000 && gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
        /* story fight: the story player is always slot 0 */
        giUserKeydowns[0] = iTranslateKeyPress(giStoryModePlayerIdx, 0);
        giInputBuffer[0][giInputBufferPos] = giUserKeydowns[0];
    } else {
        giUserKeydowns[0] = iTranslateKeyPress(0, 0);
        giInputBuffer[0][giInputBufferPos] = giUserKeydowns[0];
        giUserKeydowns[1] = iTranslateKeyPress(1, 1);
        giInputBuffer[1][giInputBufferPos] = giUserKeydowns[1];
    }
    /* edges and auto-repeat */
    giAnyInput = 0;
    giAnyInputXor = 0;
    giAnyInputCleaned = 0;
    for (iPlayer = 0; iPlayer < 8; iPlayer++) {
        /* newly pressed: held now and not in the previous frame */
        giLastInputXor[iPlayer] = (giUserKeydowns[iPlayer] ^ giLastInput[iPlayer]) & giUserKeydowns[iPlayer];
        giLastInput[iPlayer] = giUserKeydowns[iPlayer];
        if (giUserKeydowns[iPlayer] && giUserKeydowns[iPlayer] == giRepeatInput[iPlayer]) {
            /* held: auto-repeat */
            if (--giInputCountdowns[iPlayer] == 0) {
                giLastInputCleaned[iPlayer] = giUserKeydowns[iPlayer];
                giInputCountdowns[iPlayer] = giKeyRepeatRate;
            } else {
                giLastInputCleaned[iPlayer] = 0;
            }
        } else {
            /* the input changed: report it now and restart the delay, but a direction axis that
               was already held before the change (left/right, up/down) does not fire again */
            giLastInputCleaned[iPlayer] = giUserKeydowns[iPlayer];
            giInputCountdowns[iPlayer] = giKeyRepeatDelay;
            if (giRepeatInput[iPlayer] & 3)
                giLastInputCleaned[iPlayer] = giUserKeydowns[iPlayer] & ~3;
            if (giRepeatInput[iPlayer] & 0xc)
                giLastInputCleaned[iPlayer] &= ~0xc;
            giRepeatInput[iPlayer] = giUserKeydowns[iPlayer];
        }
        giAnyInput |= giUserKeydowns[iPlayer];
        giAnyInputXor |= giLastInputXor[iPlayer];
        giAnyInputCleaned |= giLastInputCleaned[iPlayer];
    }
}

/*
 * Reads the keyboard state and translates controls 0 for slot 0, discarding the result (not
 * called).
 * Globals: changes gcKeyState.
 */
void vPollKeyboardState(void)
{
    GetKeyboardState(gcKeyState);
    iTranslateKeyPress(0, 0);
}

/*
 * Meant to turn a byte 128-255 into -128..-1; with VC6's signed char, cValue > 127 is never true,
 * so it just returns the sign-extended value (used for the signed "add stocks" of the special
 * gauge script command).
 * cValue: the byte.
 * Returns it as an int (-128..127).
 */
int iSubtractTwoFiftySixIfAboveOneTwentySeven(char cValue)
{
    if (cValue > 127)
        return cValue - 256;
    return cValue;
}

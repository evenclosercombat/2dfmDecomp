/*
 * game_bss.c - definition of gBss, the game's uninitialized variables in their original order
 * (see include/game_bss.h), checks of that layout, and pBssAddr32.
 */
#include <stddef.h>
#include "game_bss.h"   /* not kgt.h: its globals.h turns the variable names into BSS() macros */

/* 32-byte aligned like the original section, so every variable keeps its original alignment */
__attribute__((aligned(32))) kgtBss gBss;

/*
 * The variables: name, original address, size in the original, and how the original's bytes map to
 * this build's (MAP_PLAIN no pointers inside: the same bytes; MAP_PTRS pointers or pointer-sized
 * values; MAP_CORE a structure starting with a kgt_core; MAP_TRAILS the after-image trails;
 * MAP_NONE other structures with pointers, not mapped by pBssAddr32).  BSS_FIELDS are the names
 * inside other variables (name, original address, size).
 */
#define BSS_MEMBERS(X) \
    X(g_4213c4, 0x4213c4, 4, MAP_PLAIN) \
    X(gdpidLocalPlayer, 0x4213c8, 4, MAP_PLAIN) \
    X(g_4213cc, 0x4213cc, 4, MAP_PLAIN) \
    X(gSessionGuids, 0x4213d0, 320, MAP_PLAIN) \
    X(gServiceProviderGuids, 0x421510, 320, MAP_PLAIN) \
    X(gBmiStrip, 0x421650, 1064, MAP_PLAIN) \
    X(ghStripDc, 0x421a78, 4, MAP_PTRS) \
    X(ghStripBitmap, 0x421a7c, 4, MAP_PTRS) \
    X(ghStripOldBitmap, 0x421a80, 4, MAP_PTRS) \
    X(gpStripBits, 0x421a84, 4, MAP_PTRS) \
    X(gszServiceProviderNames, 0x421a88, 5120, MAP_PLAIN) \
    X(gszSessionNames, 0x422e88, 5120, MAP_PLAIN) \
    X(gDpName, 0x424288, 16, MAP_NONE) \
    X(gBmiFrame, 0x424298, 1064, MAP_PLAIN) \
    X(ghFrameDc, 0x4246c0, 4, MAP_PTRS) \
    X(ghFrameBitmap, 0x4246c4, 4, MAP_PTRS) \
    X(ghFrameOldBitmap, 0x4246c8, 4, MAP_PTRS) \
    X(gpFrameBits, 0x4246cc, 4, MAP_PTRS) \
    X(g_4246d0, 0x4246d0, 16, MAP_PLAIN) \
    X(gpDirectPlay, 0x4246e0, 4, MAP_PTRS) \
    X(gpAppGuid, 0x4246e4, 4, MAP_PTRS) \
    X(giNumServiceProviders, 0x4246e8, 4, MAP_PLAIN) \
    X(giNumSessionsFound, 0x4246ec, 4, MAP_PLAIN) \
    X(g_4246f0, 0x4246f0, 4, MAP_PLAIN) \
    X(giSkipframeCount, 0x4246f4, 4, MAP_PLAIN) \
    X(ghWnd, 0x4246f8, 4, MAP_PTRS) \
    X(giObjectCount, 0x4246fc, 4, MAP_PLAIN) \
    X(giTimeAdjustmentFlag, 0x424700, 4, MAP_PLAIN) \
    X(giScreenMode, 0x424704, 4, MAP_PLAIN) \
    X(giStartGameUnusedA, 0x424708, 4, MAP_PLAIN) \
    X(giHitJudge, 0x42470c, 8, MAP_PLAIN) \
    X(gbStoryMode, 0x424714, 4, MAP_PLAIN) \
    X(giForceRoundEnd, 0x424718, 4, MAP_PLAIN) \
    X(gbShowDebugStatus, 0x42471c, 4, MAP_PLAIN) \
    X(guCdAudioDeviceId, 0x424720, 4, MAP_PLAIN) \
    X(gbCdAudioOpen, 0x424724, 4, MAP_PLAIN) \
    X(giDiscLoopFlag, 0x424728, 4, MAP_PLAIN) \
    X(giCdAudioError, 0x42472c, 4, MAP_PLAIN) \
    X(g_424730, 0x424730, 4, MAP_PLAIN) \
    X(giCdAudioRequested, 0x424734, 4, MAP_PLAIN) \
    X(gbMidiOpen, 0x424738, 4, MAP_PLAIN) \
    X(guMidiDeviceId, 0x42473c, 4, MAP_PLAIN) \
    X(giMidiLoopFlag, 0x424740, 4, MAP_PLAIN) \
    X(giAppmode, 0x424744, 4, MAP_PLAIN) \
    X(gszEmptyCommandName, 0x424748, 4, MAP_PLAIN) \
    X(giDsoundInitializedFlag, 0x42474c, 4, MAP_PLAIN) \
    X(gpDDSPrimary, 0x424750, 4, MAP_PTRS) \
    X(gpDDSBack, 0x424754, 4, MAP_PTRS) \
    X(gpDirectDraw, 0x424758, 4, MAP_PTRS) \
    X(gbSaveWindowRect, 0x42475c, 4, MAP_PLAIN) \
    X(gbDdrawInitialized, 0x424760, 4, MAP_PLAIN) \
    X(g_424764, 0x424764, 4, MAP_PLAIN) \
    X(giModeSwitchFrames, 0x424768, 4, MAP_PLAIN) \
    X(gbSwitchingDisplayMode, 0x42476c, 4, MAP_PLAIN) \
    X(giDisplayModeSwitches, 0x424770, 4, MAP_PLAIN) \
    X(gbDisplayModeReady, 0x424774, 4, MAP_PLAIN) \
    X(g_424778, 0x424778, 4, MAP_PLAIN) \
    X(gszEmptyWindowName, 0x42477c, 4, MAP_PLAIN) \
    X(giMenuSelectionIdx, 0x424780, 4, MAP_PLAIN) \
    X(gszEmptyBgFile, 0x424784, 4, MAP_PLAIN) \
    X(giKgtCompressOutSize, 0x424788, 4, MAP_PLAIN) \
    X(gszEmptyIniDefault, 0x42478c, 4, MAP_PLAIN) \
    X(ghWavFileAlloc, 0x424790, 4, MAP_PTRS) \
    X(g_424794, 0x424794, 4, MAP_PLAIN) \
    X(ghTrackbarTime, 0x424798, 4, MAP_PTRS) \
    X(g_42479c, 0x42479c, 4, MAP_PLAIN) \
    X(grDialogRect, 0x4247a0, 16, MAP_PLAIN) \
    X(giTrackbarTimePos, 0x4247b0, 4, MAP_PTRS) \
    X(ghTrackbarRounds, 0x4247b4, 4, MAP_PTRS) \
    X(ghTrackbarRoundsTeamVs, 0x4247b8, 4, MAP_PTRS) \
    X(giTrackbarSpeedPos, 0x4247bc, 4, MAP_PTRS) \
    X(giTrackbarRoundsPos, 0x4247c0, 4, MAP_PTRS) \
    X(gKeyConfigEdit, 0x4247c4, 34, MAP_PLAIN) \
    X(gJoyConfigEdit, 0x4247e8, 14, MAP_PLAIN) \
    X(giTrackbarRoundsTeamVsPos, 0x4247f8, 4, MAP_PTRS) \
    X(ghTrackbarSpeed, 0x4247fc, 4, MAP_PTRS) \
    X(gpJoyInputButton, 0x4249b0, 4, MAP_PTRS) \
    X(ghJoyWindow, 0x4249b4, 4, MAP_PTRS) \
    X(gpKeyInputTarget, 0x4249b8, 4, MAP_PTRS) \
    X(giJoyInputPad, 0x4249bc, 4, MAP_PLAIN) \
    X(ghKeyInputWindow, 0x4249c0, 4, MAP_PTRS) \
    X(gJoyCaps1, 0x4249e0, 404, MAP_PLAIN) \
    X(gJoyCaps2, 0x424b80, 404, MAP_PLAIN) \
    X(gcKeyState, 0x424d20, 256, MAP_PLAIN) \
    X(gdwJoystickX, 0x424e20, 4, MAP_PLAIN) \
    X(gdwJoystickY, 0x424e24, 4, MAP_PLAIN) \
    X(gdwJoystickTwoX, 0x424e28, 4, MAP_PLAIN) \
    X(gdwJoystickTwoY, 0x424e2c, 4, MAP_PLAIN) \
    X(gpKgtCompressOut, 0x424e30, 4, MAP_PTRS) \
    X(giGameModes, 0x424e40, 12, MAP_PLAIN) \
    X(gkgtSelectCursor1, 0x424e50, 8, MAP_PLAIN) \
    X(gkgtSelectCursor2, 0x424e58, 8, MAP_PLAIN) \
    X(giAmountOfGameModes, 0x424e60, 4, MAP_PLAIN) \
    X(giRoundsNotWon, 0x424e64, 4, MAP_PLAIN) \
    X(gkgtStorySelectCursor, 0x424e68, 8, MAP_PLAIN) \
    X(gpTeamPortraits, 0x424e80, 128, MAP_PTRS) \
    X(giBattlePrestartTimer, 0x424f00, 4, MAP_PLAIN) \
    X(gcDemoSkipWithInput, 0x424f04, 1, MAP_PLAIN) \
    X(giDemoTime, 0x424f08, 4, MAP_PLAIN) \
    X(giStoryFrontStageFlag, 0x424f0c, 4, MAP_PLAIN) \
    X(gStoryMode, 0x424f20, 20, MAP_PLAIN) \
    X(giStoryModeCurrentRound, 0x424f34, 4, MAP_PLAIN) \
    X(giStoryWinsP1, 0x424f38, 4, MAP_PLAIN) \
    X(giStoryWinsP2, 0x424f3c, 4, MAP_PLAIN) \
    X(grWindowRect, 0x424f40, 16, MAP_PLAIN) \
    X(gkgtBitmaps, 0x424f60, 2560, MAP_NONE) \
    X(gdwFrameTimeDiff, 0x425960, 4, MAP_PLAIN) \
    X(gcKeyboardControlsSets, 0x425980, 34, MAP_PLAIN) \
    X(giEngineObjectIter, 0x4259a4, 4, MAP_PLAIN) \
    X(gpDrawNodeNext, 0x4259a8, 4, MAP_PTRS) \
    X(giUserKeydowns, 0x4259c0, 32, MAP_PLAIN) \
    X(grClipRect, 0x4259e0, 16, MAP_PLAIN) \
    X(gDebugStatus, 0x425a00, 68, MAP_PLAIN) \
    X(gpGlobalMemoryAlloc, 0x425a44, 4, MAP_PTRS) \
    X(giConfigGameWindowPointX, 0x425a48, 4, MAP_PLAIN) \
    X(giConfigGameWindowPointY, 0x425a4c, 4, MAP_PLAIN) \
    X(gkgtLoadedDemo, 0x425a60, 9833, MAP_CORE) \
    X(gpDirectSound, 0x4280cc, 4, MAP_PTRS) \
    X(guLocalIpAddr, 0x4280d0, 4, MAP_PLAIN) \
    X(gdpidHost, 0x4280d4, 4, MAP_PLAIN) \
    X(giAnyInputXor, 0x4280d8, 4, MAP_PLAIN) \
    X(giInputBuffer, 0x4280e0, 32768, MAP_PLAIN) \
    X(gEditorTestplayConfig, 0x4300e0, 332, MAP_PLAIN) \
    X(giConfigEditorDemoNb, 0x43022c, 4, MAP_PLAIN) \
    X(gkgtDrawLayers, 0x430240, 1024, MAP_NONE) \
    X(gpWavs, 0x430640, 11264, MAP_PTRS) \
    X(gkgtKgtSystem, 0x433240, 74940, MAP_CORE) \
    X(giReverseShakeDirection, 0x4456fc, 4, MAP_PLAIN) \
    X(giGravityScalar, 0x445700, 4, MAP_PLAIN) \
    X(giGamespeedFrames, 0x445704, 4, MAP_PLAIN) \
    X(gcJoystickButtons, 0x445710, 14, MAP_PLAIN) \
    X(gpLocalHostEnt, 0x445720, 4, MAP_PTRS) \
    X(gkgtLoadedStage, 0x445740, 9873, MAP_CORE) \
    X(gdwSystemTime, 0x447dd4, 4, MAP_PLAIN) \
    X(gszLocalHostName, 0x447de0, 256, MAP_PLAIN) \
    X(giInputBufferPos, 0x447ee0, 4, MAP_PLAIN) \
    X(giLastInput, 0x447f00, 32, MAP_PLAIN) \
    X(giConfigGameWindowSizeX, 0x447f20, 4, MAP_PLAIN) \
    X(giConfigGameWindowSizeY, 0x447f24, 4, MAP_PLAIN) \
    X(giStartGameUnusedB, 0x447f28, 4, MAP_PLAIN) \
    X(giCameraX, 0x447f2c, 4, MAP_PLAIN) \
    X(giCameraY, 0x447f30, 4, MAP_PLAIN) \
    X(giLastInputCleaned, 0x447f40, 32, MAP_PLAIN) \
    X(giLastInputXor, 0x447f60, 32, MAP_PLAIN) \
    X(gAfterImageTrails, 0x447f80, 161600, MAP_TRAILS) \
    X(gkgtDebugLog, 0x46f6c0, 2160, MAP_PLAIN) \
    X(gDDSurfaceDesc, 0x46ff40, 108, MAP_NONE) \
    X(gszPlayerName, 0x46ffc0, 32, MAP_PLAIN) \
    X(gszSessionName, 0x46ffe0, 64, MAP_PLAIN) \
    X(gkgtGameState, 0x470020, 428, MAP_PLAIN) \
    X(ghInstance, 0x4701cc, 4, MAP_PTRS) \
    X(gkgtEngineObjects, 0x4701e0, 391168, MAP_NONE) \
    X(giPlayerFileIndices, 0x4cf9e0, 32, MAP_PLAIN) \
    X(gpkgtCurrentEngineObject, 0x4cfa00, 4, MAP_PTRS) \
    X(giAnyInput, 0x4cfa04, 4, MAP_PLAIN) \
    X(gkgtDrawNodes, 0x4cfa20, 8192, MAP_NONE) \
    X(gwTintedPalette16, 0x4d1a20, 512, MAP_PLAIN) \
    X(giAnyInputCleaned, 0x4d1c20, 4, MAP_PLAIN) \
    X(giInputCountdowns, 0x4d1c40, 32, MAP_PLAIN) \
    X(gszCurrentDirectory, 0x4d1c60, 256, MAP_PLAIN) \
    X(giConfigGameScreenMode, 0x4d1d60, 4, MAP_PLAIN) \
    X(gkgtLoadedCharacter, 0x4d1d80, 459256, MAP_NONE) \
    X(giPlayerMomentumScalar, 0x541f78, 4, MAP_PLAIN) \
    X(gpsLpAppName, 0x541f7c, 4, MAP_PTRS) \
    X(giRepeatInput, 0x541f80, 32, MAP_PLAIN) \
    /* end */

#define BSS_FIELDS(X) \
    X(giStoryModePlayerIdx, 0x424f20, 4) \
    X(giStoryModeSide, 0x424f24, 4) \
    X(giCurrentStoryStep, 0x424f28, 12) \
    X(giConfigTestplayPlayer0Nb, 0x4300e0, 4) \
    X(giConfigTestplayPlayer0Cpu, 0x4300e4, 4) \
    X(giConfigTestplayPlayer1Nb, 0x4300f0, 4) \
    X(giConfigTestplayPlayer1Cpu, 0x4300f4, 4) \
    X(giConfigTestplayHitjudge, 0x430100, 4) \
    X(giConfigTestplayGamespeed, 0x430104, 4) \
    X(giConfigTestplayGameinfo, 0x430108, 4) \
    X(giConfigTestplayStageNb, 0x43010c, 4) \
    X(giConfigTestplayJoystick, 0x430110, 4) \
    X(giConfigTestplayTime, 0x430114, 4) \
    X(giConfigTestplayExit, 0x430118, 4) \
    X(giConfigTestplayVsMode, 0x430120, 4) \
    X(giConfigNumberOfRounds, 0x430124, 4) \
    X(giConfigNumberOfRoundsTeamVs, 0x430128, 4) \
    X(gszConfigReturnedFilename, 0x43012c, 256) \
    X(gshSystemVariables, 0x4456b0, 32) \
    X(giSystemFlashType, 0x4456d0, 4) \
    X(giSystemFlashRed, 0x4456d4, 4) \
    X(giSystemFlashGreen, 0x4456d8, 4) \
    X(giSystemFlashBlue, 0x4456dc, 4) \
    X(giSystemFlashAlpha, 0x4456e0, 4) \
    X(giSystemFlashTimeLeft, 0x4456e4, 20) \
    X(giSystemFlashDuration, 0x4456f8, 4) \
    X(giStageFlashType, 0x447d7d, 4) \
    X(giStageFlashRed, 0x447d81, 4) \
    X(giStageFlashGreen, 0x447d85, 4) \
    X(giStageFlashBlue, 0x447d89, 4) \
    X(giStageFlashAlpha, 0x447d8d, 4) \
    X(giStageFlashTimeLeft, 0x447d91, 20) \
    X(giStageFlashDuration, 0x447da5, 4) \
    X(giShakeXMode, 0x447da9, 4) \
    X(giShakeXOffset, 0x447dad, 4) \
    X(giShakeXAmplitude, 0x447db1, 4) \
    X(giShakeXTimeLeft, 0x447db5, 4) \
    X(giShakeXDuration, 0x447db9, 4) \
    X(giShakeYMode, 0x447dbd, 4) \
    X(giShakeYOffset, 0x447dc1, 4) \
    X(giShakeYAmplitude, 0x447dc5, 4) \
    X(giShakeYTimeLeft, 0x447dc9, 4) \
    X(giShakeYDuration, 0x447dcd, 4) \
    X(gAfterImageLayers, 0x447f80, 161600) \
    X(gAfterImageTrailsBase, 0x447930, 1) \
    /* end */
#define MAP_PLAIN  0
#define MAP_PTRS   1
#define MAP_CORE   2
#define MAP_TRAILS 3
#define MAP_NONE   4

#if !defined(_WIN64)
/* i686: exactly the original layout */
#define CHECK(name, addr, size) \
    _Static_assert(BSS_OFS_##name == (addr) - BSS_ADDR && BSS_OFS_##name + (size) <= sizeof(kgtBss), #name);
#define CHECK_MEMBER(name, addr, size, map) \
    CHECK(name, addr, size) \
    _Static_assert(offsetof(kgtBss, m_##name) == BSS_OFS_##name && sizeof(gBss.m_##name) == (size), #name);

_Static_assert(sizeof(kgtBss) == 0x541fa0 - BSS_ADDR, "size of gBss");
BSS_MEMBERS(CHECK_MEMBER)
BSS_FIELDS(CHECK)
#else
/* x86-64: the variables without pointers keep their size, the pointer-sized ones are pointer-aligned */
#define CHECK_MEMBER(name, addr, size, map) \
    _Static_assert((map) != MAP_PLAIN || sizeof(gBss.m_##name) == (size), #name); \
    _Static_assert((map) == MAP_PLAIN || offsetof(kgtBss, m_##name) % sizeof(void *) == 0, #name); \
    _Static_assert((map) != MAP_PTRS || sizeof(gBss.m_##name) == (size) / 4 * sizeof(void *), #name);
BSS_MEMBERS(CHECK_MEMBER)
#endif

/* ---- pBssAddr32 ---- */

typedef struct {
    uint32_t uAddr;     /* original address */
    uint32_t uSize;     /* size in the original */
    size_t ofs;         /* offset in gBss */
    int iMap;           /* MAP_ */
} BssVar;

#define BSS_VAR_ENTRY(name, addr, size, map) { addr, size, offsetof(kgtBss, m_##name), map },
static const BssVar gBssVars[] = { BSS_MEMBERS(BSS_VAR_ENTRY) };

/* offset o of the original layout of a structure whose pointers (4 bytes there) start at the
   original offsets auSlots[0..n-1] (ascending), in this build's layout; inside a pointer: its low bytes */
static size_t uMapPtrSlots(uint32_t o, const uint32_t *auSlots, int n)
{
    size_t uExtra = 0;
    int i;

    for (i = 0; i < n && o >= auSlots[i] + 4; i++)
        uExtra += KGT_PTR_GROWTH;
    return o + uExtra;
}

unsigned char *pBssAddr32(uint32_t uAddr32)
{
    static const uint32_t auCoreSlots[4] = { 0x110, 0x114, 0x118, 0x221c };  /* kgt_core's pointers */
    const BssVar *pVar;
    uint32_t o, r, n;
    size_t i;

    for (i = 0; i < sizeof(gBssVars) / sizeof(gBssVars[0]); i++) {
        pVar = &gBssVars[i];
        if (uAddr32 < pVar->uAddr || uAddr32 - pVar->uAddr >= pVar->uSize)
            continue;
        o = uAddr32 - pVar->uAddr;
        switch (pVar->iMap) {
        case MAP_PLAIN:
            return (unsigned char *)&gBss + pVar->ofs + o;
        case MAP_PTRS:
            return (unsigned char *)&gBss + pVar->ofs + o / 4 * sizeof(void *) + o % 4;
        case MAP_CORE:
            return (unsigned char *)&gBss + pVar->ofs + uMapPtrSlots(o, auCoreSlots, 4);
        case MAP_TRAILS:
            /* unk_0x650_struct: pStep at 8, then 100 frames of 16 bytes with pImage at 12 */
            r = o % 0x650;
            n = (r >= 12) + (r >= 32 ? (r - 32) / 16 + 1 : 0);   /* pointers before r */
            if (r >= 8 && r < 12)
                n = 0;
            else if (r >= 16 && (r - 16) % 16 >= 12)
                n = 1 + (r - 16) / 16;   /* inside frame (r - 16) / 16's pImage */
            return (unsigned char *)&gBss + pVar->ofs + o / 0x650 * sizeof(unk_0x650_struct) + r + n * KGT_PTR_GROWTH;
        default:
            return NULL;
        }
    }
    return NULL;
}
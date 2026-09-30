/*
 * game_bss.h - the game's uninitialized variables, laid out as in the original executable.
 *
 * The code relies on that layout in places: a variable reached through its neighbour (vMemzero of
 * gBmiFrame also clears the four handles after it, gAfterImageTrails is indexed from one element
 * before its start, ...) and "field" names for members inside a larger variable (the screen-shake
 * and flash variables behind the packed kgt_stage in gkgtLoadedStage, the test-play settings, the
 * story-mode progress).  So instead of separate C variables, whose relative placement the compiler
 * and linker are free to choose, all of them are parts of one object, gBss, in their original order
 * (the unreferenced space of the original, g_XXXXXX, and LIBC's .bss between the plain .bss and the
 * communal variables are kept as filler).
 *
 * Each translation unit declares the variables it uses with the type it uses them with:
 *
 *     #define giScreenMode BSS(int, giScreenMode)       instead of   extern int giScreenMode;
 *
 * BSS(T, name) is the lvalue of type T at gBss + BSS_OFS_name.  As with the extern declarations of
 * the main branch, the type is the one the file uses (giHitJudge is int[2] in battle.c and int
 * elsewhere, gcKeyboardControlsSets a KEY_LAYOUT[2], a KEYCONFIG or a BYTE[2][17], ...).  The
 * kgtBss members (m_name) are only storage (byte arrays); src/game_bss.c defines gBss and checks the
 * offsets.  Building needs -fno-strict-aliasing (the bytes are accessed with other types).
 *
 * 64-bit port: the members that hold pointers (handles, object pointers, and the structures with
 * pointer members) take their size from the real types (BSS_PTR, sizeof) and are pointer-aligned
 * (BSS_ALIGNED), and every BSS_OFS_ is computed with offsetof, so the i686 build has exactly the
 * original layout (src/game_bss.c checks every address against the original there) and the x86-64
 * build the same order with wider members; the names inside other variables are offsets from their
 * variable (after a kgt_core they move with its pointers, KGT_CORE_GROWTH).  pBssAddr32
 * (src/game_bss.c) maps an address of the original's .bss to the same byte of this build, for the
 * few places where the original reaches memory through the wrong structure (see battle.c).
 *
 * Converted once from asm/game_bss.txt, the table the original-toolchain build uses on the main
 * branch (address, size, name; "field" rows are extra names inside other variables); maintained by
 * hand since.  To add a variable, give it a member (or a BSS_OFS_ inside an existing one) at its
 * original address and a check in src/game_bss.c.
 */
#ifndef GAME_BSS_H
#define GAME_BSS_H

#include <stddef.h>     /* offsetof */
#include <stdint.h>     /* uint32_t */
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>      /* DDSURFACEDESC */
#include <dsound.h>     /* kgtWav (kgt_types.h) */
#include <dplay.h>      /* DPNAME */
#include "kgt_types.h"  /* the sizes of the structures kept in gBss */

#define BSS_ADDR 0x4213c0   /* original address of gBss */

/* a pointer-sized variable (4 bytes in the original) and the alignment of the members holding pointers */
#define BSS_PTR sizeof(void *)
#define BSS_ALIGNED _Alignas(void *)
typedef struct kgtBss {
    unsigned char m__4213c0[4];                      /* 0x4213c0: (the blitter's last .bss dword; keeps the original alignment) */
    unsigned char m_g_4213c4[4];                     /* 0x4213c4 */
    unsigned char m_gdpidLocalPlayer[4];             /* 0x4213c8 */
    unsigned char m_g_4213cc[4];                     /* 0x4213cc */
    unsigned char m_gSessionGuids[320];              /* 0x4213d0: GUID[20] */
    unsigned char m_gServiceProviderGuids[320];      /* 0x421510: GUID[20] */
    unsigned char m_gBmiStrip[1064];                 /* 0x421650: BITMAPINFOHEADER + RGBQUAD[256] */
    BSS_ALIGNED unsigned char m_ghStripDc[BSS_PTR];   /* 0x421a78 */
    BSS_ALIGNED unsigned char m_ghStripBitmap[BSS_PTR]; /* 0x421a7c */
    BSS_ALIGNED unsigned char m_ghStripOldBitmap[BSS_PTR]; /* 0x421a80 */
    BSS_ALIGNED unsigned char m_gpStripBits[BSS_PTR]; /* 0x421a84 */
    unsigned char m_gszServiceProviderNames[5120];   /* 0x421a88: char[20][256] */
    unsigned char m_gszSessionNames[5120];           /* 0x422e88: char[20][256] */
    BSS_ALIGNED unsigned char m_gDpName[sizeof(DPNAME)]; /* 0x424288: DPNAME */
    unsigned char m_gBmiFrame[1064];                 /* 0x424298: BITMAPINFOHEADER + RGBQUAD[256] */
    BSS_ALIGNED unsigned char m_ghFrameDc[BSS_PTR];   /* 0x4246c0 */
    BSS_ALIGNED unsigned char m_ghFrameBitmap[BSS_PTR]; /* 0x4246c4 */
    BSS_ALIGNED unsigned char m_ghFrameOldBitmap[BSS_PTR]; /* 0x4246c8 */
    BSS_ALIGNED unsigned char m_gpFrameBits[BSS_PTR]; /* 0x4246cc */
    unsigned char m_g_4246d0[16];                    /* 0x4246d0 */
    BSS_ALIGNED unsigned char m_gpDirectPlay[BSS_PTR]; /* 0x4246e0 */
    BSS_ALIGNED unsigned char m_gpAppGuid[BSS_PTR];   /* 0x4246e4 */
    unsigned char m_giNumServiceProviders[4];        /* 0x4246e8 */
    unsigned char m_giNumSessionsFound[4];           /* 0x4246ec */
    unsigned char m_g_4246f0[4];                     /* 0x4246f0 */
    unsigned char m_giSkipframeCount[4];             /* 0x4246f4 */
    BSS_ALIGNED unsigned char m_ghWnd[BSS_PTR];       /* 0x4246f8 */
    unsigned char m_giObjectCount[4];                /* 0x4246fc */
    unsigned char m_giTimeAdjustmentFlag[4];         /* 0x424700 */
    unsigned char m_giScreenMode[4];                 /* 0x424704 */
    unsigned char m_giStartGameUnusedA[4];           /* 0x424708 */
    unsigned char m_giHitJudge[8];                   /* 0x42470c: int[2] */
    unsigned char m_gbStoryMode[4];                  /* 0x424714 */
    unsigned char m_giForceRoundEnd[4];              /* 0x424718: engine.c: giForceRoundEnd */
    unsigned char m_gbShowDebugStatus[4];            /* 0x42471c */
    unsigned char m_guCdAudioDeviceId[4];            /* 0x424720 */
    unsigned char m_gbCdAudioOpen[4];                /* 0x424724 */
    unsigned char m_giDiscLoopFlag[4];               /* 0x424728 */
    unsigned char m_giCdAudioError[4];               /* 0x42472c */
    unsigned char m_g_424730[4];                     /* 0x424730 */
    unsigned char m_giCdAudioRequested[4];           /* 0x424734 */
    unsigned char m_gbMidiOpen[4];                   /* 0x424738 */
    unsigned char m_guMidiDeviceId[4];               /* 0x42473c */
    unsigned char m_giMidiLoopFlag[4];               /* 0x424740 */
    unsigned char m_giAppmode[4];                    /* 0x424744 */
    unsigned char m_gszEmptyCommandName[4];          /* 0x424748: a "" literal of globals.c in the original (unused here: the code writes "") */
    unsigned char m_giDsoundInitializedFlag[4];      /* 0x42474c */
    BSS_ALIGNED unsigned char m_gpDDSPrimary[BSS_PTR]; /* 0x424750 */
    BSS_ALIGNED unsigned char m_gpDDSBack[BSS_PTR];   /* 0x424754 */
    BSS_ALIGNED unsigned char m_gpDirectDraw[BSS_PTR]; /* 0x424758 */
    unsigned char m_gbSaveWindowRect[4];             /* 0x42475c */
    unsigned char m_gbDdrawInitialized[4];           /* 0x424760 */
    unsigned char m_g_424764[4];                     /* 0x424764 */
    unsigned char m_giModeSwitchFrames[4];           /* 0x424768 */
    unsigned char m_gbSwitchingDisplayMode[4];       /* 0x42476c */
    unsigned char m_giDisplayModeSwitches[4];        /* 0x424770 */
    unsigned char m_gbDisplayModeReady[4];           /* 0x424774 */
    unsigned char m_g_424778[4];                     /* 0x424778 */
    unsigned char m_gszEmptyWindowName[4];           /* 0x42477c: main's "" literal in the original (unused here) */
    unsigned char m_giMenuSelectionIdx[4];           /* 0x424780 */
    unsigned char m_gszEmptyBgFile[4];               /* 0x424784: engine's "" literal in the original (unused here) */
    unsigned char m_giKgtCompressOutSize[4];         /* 0x424788 */
    unsigned char m_gszEmptyIniDefault[4];           /* 0x42478c: config's "" literal in the original (unused here) */
    BSS_ALIGNED unsigned char m_ghWavFileAlloc[BSS_PTR]; /* 0x424790 */
    unsigned char m_g_424794[4];                     /* 0x424794 */
    BSS_ALIGNED unsigned char m_ghTrackbarTime[BSS_PTR]; /* 0x424798 */
    unsigned char m_g_42479c[4];                     /* 0x42479c */
    unsigned char m_grDialogRect[16];                /* 0x4247a0: RECT */
    BSS_ALIGNED unsigned char m_giTrackbarTimePos[BSS_PTR]; /* 0x4247b0 */
    BSS_ALIGNED unsigned char m_ghTrackbarRounds[BSS_PTR]; /* 0x4247b4 */
    BSS_ALIGNED unsigned char m_ghTrackbarRoundsTeamVs[BSS_PTR]; /* 0x4247b8 */
    BSS_ALIGNED unsigned char m_giTrackbarSpeedPos[BSS_PTR]; /* 0x4247bc */
    BSS_ALIGNED unsigned char m_giTrackbarRoundsPos[BSS_PTR]; /* 0x4247c0 */
    unsigned char m_gKeyConfigEdit[34];              /* 0x4247c4: KEYCONFIG (BYTE[2][17]) */
    unsigned char m__4247e6[2];                      /* 0x4247e6: (alignment) */
    unsigned char m_gJoyConfigEdit[14];              /* 0x4247e8: JOYCONFIG (BYTE[2][7]) */
    unsigned char m__4247f6[2];                      /* 0x4247f6: (alignment) */
    BSS_ALIGNED unsigned char m_giTrackbarRoundsTeamVsPos[BSS_PTR]; /* 0x4247f8 */
    BSS_ALIGNED unsigned char m_ghTrackbarSpeed[BSS_PTR]; /* 0x4247fc */
    unsigned char m__424800[4];                      /* 0x424800: (dialogs.c's static giJoystickInputTimer in the original) */
    unsigned char m__424804[428];                    /* 0x424804: (LIBC's .bss in the original) */
    /* ---- communal variables ---- */
    BSS_ALIGNED unsigned char m_gpJoyInputButton[BSS_PTR]; /* 0x4249b0 */
    BSS_ALIGNED unsigned char m_ghJoyWindow[BSS_PTR]; /* 0x4249b4 */
    BSS_ALIGNED unsigned char m_gpKeyInputTarget[BSS_PTR]; /* 0x4249b8 */
    unsigned char m_giJoyInputPad[4];                /* 0x4249bc */
    BSS_ALIGNED unsigned char m_ghKeyInputWindow[BSS_PTR]; /* 0x4249c0 */
    unsigned char m__4249c4[28];                     /* 0x4249c4: (alignment) */
    unsigned char m_gJoyCaps1[404];                  /* 0x4249e0: JOYCAPSA */
    unsigned char m__424b74[12];                     /* 0x424b74: (alignment) */
    unsigned char m_gJoyCaps2[404];                  /* 0x424b80: JOYCAPSA */
    unsigned char m__424d14[12];                     /* 0x424d14: (alignment) */
    unsigned char m_gcKeyState[256];                 /* 0x424d20 */
    unsigned char m_gdwJoystickX[4];                 /* 0x424e20 */
    unsigned char m_gdwJoystickY[4];                 /* 0x424e24 */
    unsigned char m_gdwJoystickTwoX[4];              /* 0x424e28 */
    unsigned char m_gdwJoystickTwoY[4];              /* 0x424e2c */
    BSS_ALIGNED unsigned char m_gpKgtCompressOut[BSS_PTR]; /* 0x424e30 */
    unsigned char m__424e34[12];                     /* 0x424e34: (alignment) */
    unsigned char m_giGameModes[12];                 /* 0x424e40: int[3] */
    unsigned char m__424e4c[4];                      /* 0x424e4c: (alignment) */
    unsigned char m_gkgtSelectCursor1[8];            /* 0x424e50: kgtGridCoordinates */
    unsigned char m_gkgtSelectCursor2[8];            /* 0x424e58 */
    unsigned char m_giAmountOfGameModes[4];          /* 0x424e60 */
    unsigned char m_giRoundsNotWon[4];               /* 0x424e64 */
    unsigned char m_gkgtStorySelectCursor[8];        /* 0x424e68 */
    unsigned char m__424e70[16];                     /* 0x424e70: (alignment) */
    BSS_ALIGNED unsigned char m_gpTeamPortraits[32 * BSS_PTR]; /* 0x424e80: kgtEngineObject *[32]: [side * 4 + member]; the size is what puts giBattlePrestartTimer at 0x424f00 */
    unsigned char m_giBattlePrestartTimer[4];        /* 0x424f00 */
    unsigned char m_gcDemoSkipWithInput[1];          /* 0x424f04: char */
    unsigned char m__424f05[3];                      /* 0x424f05: (alignment) */
    unsigned char m_giDemoTime[4];                   /* 0x424f08 */
    unsigned char m_giStoryFrontStageFlag[4];        /* 0x424f0c */
    unsigned char m__424f10[16];                     /* 0x424f10: (alignment) */
    unsigned char m_gStoryMode[20];                  /* 0x424f20: { int iPlayerIdx; int iPlayerIdx_2; int aiStep[3]; } */
    unsigned char m_giStoryModeCurrentRound[4];      /* 0x424f34 */
    unsigned char m_giStoryWinsP1[4];                /* 0x424f38 */
    unsigned char m_giStoryWinsP2[4];                /* 0x424f3c */
    unsigned char m_grWindowRect[16];                /* 0x424f40: RECT */
    unsigned char m__424f50[16];                     /* 0x424f50: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtBitmaps[128 * sizeof(kgtBMPINFO)]; /* 0x424f60: kgtBMPINFO[128] */
    unsigned char m_gdwFrameTimeDiff[4];             /* 0x425960 */
    unsigned char m__425964[28];                     /* 0x425964: (alignment) */
    unsigned char m_gcKeyboardControlsSets[34];      /* 0x425980: KEY_LAYOUT[2] */
    unsigned char m__4259a2[2];                      /* 0x4259a2: (alignment) */
    unsigned char m_giEngineObjectIter[4];           /* 0x4259a4 */
    BSS_ALIGNED unsigned char m_gpDrawNodeNext[BSS_PTR]; /* 0x4259a8 */
    unsigned char m__4259ac[20];                     /* 0x4259ac: (alignment) */
    unsigned char m_giUserKeydowns[32];              /* 0x4259c0: int[8] */
    unsigned char m_grClipRect[16];                  /* 0x4259e0: RECT */
    unsigned char m__4259f0[16];                     /* 0x4259f0: (alignment) */
    unsigned char m_gDebugStatus[68];                /* 0x425a00: DEBUG_B */
    BSS_ALIGNED unsigned char m_gpGlobalMemoryAlloc[BSS_PTR]; /* 0x425a44 */
    unsigned char m_giConfigGameWindowPointX[4];     /* 0x425a48 */
    unsigned char m_giConfigGameWindowPointY[4];     /* 0x425a4c */
    unsigned char m__425a50[16];                     /* 0x425a50: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtLoadedDemo[sizeof(kgt_demo_file)]; /* 0x425a60: kgt_demo_file */
    unsigned char m__4280c9[3];                      /* 0x4280c9: (alignment) */
    BSS_ALIGNED unsigned char m_gpDirectSound[BSS_PTR]; /* 0x4280cc */
    unsigned char m_guLocalIpAddr[4];                /* 0x4280d0 */
    unsigned char m_gdpidHost[4];                    /* 0x4280d4 */
    unsigned char m_giAnyInputXor[4];                /* 0x4280d8 */
    unsigned char m__4280dc[4];                      /* 0x4280dc: (alignment) */
    unsigned char m_giInputBuffer[32768];            /* 0x4280e0: DWORD[8][1024] */
    unsigned char m_gEditorTestplayConfig[332];      /* 0x4300e0 */
    unsigned char m_giConfigEditorDemoNb[4];         /* 0x43022c */
    unsigned char m__430230[16];                     /* 0x430230: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtDrawLayers[128 * sizeof(kgtDrawLayer)]; /* 0x430240: kgtDrawLayer[128] */
    BSS_ALIGNED unsigned char m_gpWavs[11 * 256 * BSS_PTR]; /* 0x430640: kgtWav *[11][256] */
    BSS_ALIGNED unsigned char m_gkgtKgtSystem[sizeof(kgtSystem)]; /* 0x433240: kgtSystem */
    unsigned char m_giReverseShakeDirection[4];      /* 0x4456fc */
    unsigned char m_giGravityScalar[4];              /* 0x445700 */
    unsigned char m_giGamespeedFrames[4];            /* 0x445704 */
    unsigned char m__445708[8];                      /* 0x445708: (alignment) */
    unsigned char m_gcJoystickButtons[14];           /* 0x445710: JOYCONFIG (BYTE[2][7]) */
    unsigned char m__44571e[2];                      /* 0x44571e: (alignment) */
    BSS_ALIGNED unsigned char m_gpLocalHostEnt[BSS_PTR]; /* 0x445720 */
    unsigned char m__445724[28];                     /* 0x445724: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtLoadedStage[sizeof(kgt_stage)]; /* 0x445740: kgt_stage (packed) */
    unsigned char m__447dd1[3];                      /* 0x447dd1: (alignment) */
    unsigned char m_gdwSystemTime[4];                /* 0x447dd4 */
    unsigned char m__447dd8[8];                      /* 0x447dd8: (alignment) */
    unsigned char m_gszLocalHostName[256];           /* 0x447de0 */
    unsigned char m_giInputBufferPos[4];             /* 0x447ee0 */
    unsigned char m__447ee4[28];                     /* 0x447ee4: (alignment) */
    unsigned char m_giLastInput[32];                 /* 0x447f00: int[8] */
    unsigned char m_giConfigGameWindowSizeX[4];      /* 0x447f20 */
    unsigned char m_giConfigGameWindowSizeY[4];      /* 0x447f24 */
    unsigned char m_giStartGameUnusedB[4];           /* 0x447f28 */
    unsigned char m_giCameraX[4];                    /* 0x447f2c */
    unsigned char m_giCameraY[4];                    /* 0x447f30 */
    unsigned char m__447f34[12];                     /* 0x447f34: (alignment) */
    unsigned char m_giLastInputCleaned[32];          /* 0x447f40: DWORD[8] */
    unsigned char m_giLastInputXor[32];              /* 0x447f60: DWORD[8] */
    BSS_ALIGNED unsigned char m_gAfterImageTrails[100 * sizeof(unk_0x650_struct)]; /* 0x447f80: unk_0x650_struct[100] */
    unsigned char m_gkgtDebugLog[2160];              /* 0x46f6c0: kgt_debug_a */
    unsigned char m__46ff30[16];                     /* 0x46ff30: (alignment) */
    BSS_ALIGNED unsigned char m_gDDSurfaceDesc[sizeof(DDSURFACEDESC)]; /* 0x46ff40: DDSURFACEDESC */
    unsigned char m__46ffac[20];                     /* 0x46ffac: (alignment) */
    unsigned char m_gszPlayerName[32];               /* 0x46ffc0 */
    unsigned char m_gszSessionName[64];              /* 0x46ffe0: used as char[32], but something must fill 0x470000-0x47001f */
    unsigned char m_gkgtGameState[428];              /* 0x470020: kgtGameState */
    BSS_ALIGNED unsigned char m_ghInstance[BSS_PTR];  /* 0x4701cc */
    unsigned char m__4701d0[16];                     /* 0x4701d0: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtEngineObjects[1024 * sizeof(kgtEngineObject)]; /* 0x4701e0: kgtEngineObject[1024] */
    unsigned char m_giPlayerFileIndices[32];         /* 0x4cf9e0: int[8] */
    BSS_ALIGNED unsigned char m_gpkgtCurrentEngineObject[BSS_PTR]; /* 0x4cfa00 */
    unsigned char m_giAnyInput[4];                   /* 0x4cfa04 */
    unsigned char m__4cfa08[24];                     /* 0x4cfa08: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtDrawNodes[1024 * sizeof(kgtDrawNode)]; /* 0x4cfa20: kgtDrawNode[1024] */
    unsigned char m_gwTintedPalette16[512];          /* 0x4d1a20: WORD[256] */
    unsigned char m_giAnyInputCleaned[4];            /* 0x4d1c20 */
    unsigned char m__4d1c24[28];                     /* 0x4d1c24: (alignment) */
    unsigned char m_giInputCountdowns[32];           /* 0x4d1c40: int[8] */
    unsigned char m_gszCurrentDirectory[256];        /* 0x4d1c60 */
    unsigned char m_giConfigGameScreenMode[4];       /* 0x4d1d60 */
    unsigned char m__4d1d64[28];                     /* 0x4d1d64: (alignment) */
    BSS_ALIGNED unsigned char m_gkgtLoadedCharacter[8 * sizeof(kgt_character_struct)]; /* 0x4d1d80: kgt_character_struct[8] */
    unsigned char m_giPlayerMomentumScalar[4];       /* 0x541f78 */
    BSS_ALIGNED unsigned char m_gpsLpAppName[BSS_PTR]; /* 0x541f7c */
    unsigned char m_giRepeatInput[32];               /* 0x541f80: int[8] */
} kgtBss;

extern kgtBss gBss;

/* the byte of gBss that is at address uAddr32 of the original's .bss (the same byte in the i686
   build; in the x86-64 build the corresponding byte, a pointer's slot mapped to its low 4 bytes),
   or NULL outside the variables it knows (see src/game_bss.c) */
unsigned char *pBssAddr32(uint32_t uAddr32);

/* offset of each name in gBss (fields: inside the variable named in the comment) */
#define BSS_OFS_g_4213c4                         offsetof(kgtBss, m_g_4213c4)
#define BSS_OFS_gdpidLocalPlayer                 offsetof(kgtBss, m_gdpidLocalPlayer)
#define BSS_OFS_g_4213cc                         offsetof(kgtBss, m_g_4213cc)
#define BSS_OFS_gSessionGuids                    offsetof(kgtBss, m_gSessionGuids)
#define BSS_OFS_gServiceProviderGuids            offsetof(kgtBss, m_gServiceProviderGuids)
#define BSS_OFS_gBmiStrip                        offsetof(kgtBss, m_gBmiStrip)
#define BSS_OFS_ghStripDc                        offsetof(kgtBss, m_ghStripDc)
#define BSS_OFS_ghStripBitmap                    offsetof(kgtBss, m_ghStripBitmap)
#define BSS_OFS_ghStripOldBitmap                 offsetof(kgtBss, m_ghStripOldBitmap)
#define BSS_OFS_gpStripBits                      offsetof(kgtBss, m_gpStripBits)
#define BSS_OFS_gszServiceProviderNames          offsetof(kgtBss, m_gszServiceProviderNames)
#define BSS_OFS_gszSessionNames                  offsetof(kgtBss, m_gszSessionNames)
#define BSS_OFS_gDpName                          offsetof(kgtBss, m_gDpName)
#define BSS_OFS_gBmiFrame                        offsetof(kgtBss, m_gBmiFrame)
#define BSS_OFS_ghFrameDc                        offsetof(kgtBss, m_ghFrameDc)
#define BSS_OFS_ghFrameBitmap                    offsetof(kgtBss, m_ghFrameBitmap)
#define BSS_OFS_ghFrameOldBitmap                 offsetof(kgtBss, m_ghFrameOldBitmap)
#define BSS_OFS_gpFrameBits                      offsetof(kgtBss, m_gpFrameBits)
#define BSS_OFS_g_4246d0                         offsetof(kgtBss, m_g_4246d0)
#define BSS_OFS_gpDirectPlay                     offsetof(kgtBss, m_gpDirectPlay)
#define BSS_OFS_gpAppGuid                        offsetof(kgtBss, m_gpAppGuid)
#define BSS_OFS_giNumServiceProviders            offsetof(kgtBss, m_giNumServiceProviders)
#define BSS_OFS_giNumSessionsFound               offsetof(kgtBss, m_giNumSessionsFound)
#define BSS_OFS_g_4246f0                         offsetof(kgtBss, m_g_4246f0)
#define BSS_OFS_giSkipframeCount                 offsetof(kgtBss, m_giSkipframeCount)
#define BSS_OFS_ghWnd                            offsetof(kgtBss, m_ghWnd)
#define BSS_OFS_giObjectCount                    offsetof(kgtBss, m_giObjectCount)
#define BSS_OFS_giTimeAdjustmentFlag             offsetof(kgtBss, m_giTimeAdjustmentFlag)
#define BSS_OFS_giScreenMode                     offsetof(kgtBss, m_giScreenMode)
#define BSS_OFS_giStartGameUnusedA               offsetof(kgtBss, m_giStartGameUnusedA)
#define BSS_OFS_giHitJudge                       offsetof(kgtBss, m_giHitJudge)
#define BSS_OFS_gbStoryMode                      offsetof(kgtBss, m_gbStoryMode)
#define BSS_OFS_giForceRoundEnd                  offsetof(kgtBss, m_giForceRoundEnd)
#define BSS_OFS_gbShowDebugStatus                offsetof(kgtBss, m_gbShowDebugStatus)
#define BSS_OFS_guCdAudioDeviceId                offsetof(kgtBss, m_guCdAudioDeviceId)
#define BSS_OFS_gbCdAudioOpen                    offsetof(kgtBss, m_gbCdAudioOpen)
#define BSS_OFS_giDiscLoopFlag                   offsetof(kgtBss, m_giDiscLoopFlag)
#define BSS_OFS_giCdAudioError                   offsetof(kgtBss, m_giCdAudioError)
#define BSS_OFS_g_424730                         offsetof(kgtBss, m_g_424730)
#define BSS_OFS_giCdAudioRequested               offsetof(kgtBss, m_giCdAudioRequested)
#define BSS_OFS_gbMidiOpen                       offsetof(kgtBss, m_gbMidiOpen)
#define BSS_OFS_guMidiDeviceId                   offsetof(kgtBss, m_guMidiDeviceId)
#define BSS_OFS_giMidiLoopFlag                   offsetof(kgtBss, m_giMidiLoopFlag)
#define BSS_OFS_giAppmode                        offsetof(kgtBss, m_giAppmode)
#define BSS_OFS_gszEmptyCommandName              offsetof(kgtBss, m_gszEmptyCommandName)
#define BSS_OFS_giDsoundInitializedFlag          offsetof(kgtBss, m_giDsoundInitializedFlag)
#define BSS_OFS_gpDDSPrimary                     offsetof(kgtBss, m_gpDDSPrimary)
#define BSS_OFS_gpDDSBack                        offsetof(kgtBss, m_gpDDSBack)
#define BSS_OFS_gpDirectDraw                     offsetof(kgtBss, m_gpDirectDraw)
#define BSS_OFS_gbSaveWindowRect                 offsetof(kgtBss, m_gbSaveWindowRect)
#define BSS_OFS_gbDdrawInitialized               offsetof(kgtBss, m_gbDdrawInitialized)
#define BSS_OFS_g_424764                         offsetof(kgtBss, m_g_424764)
#define BSS_OFS_giModeSwitchFrames               offsetof(kgtBss, m_giModeSwitchFrames)
#define BSS_OFS_gbSwitchingDisplayMode           offsetof(kgtBss, m_gbSwitchingDisplayMode)
#define BSS_OFS_giDisplayModeSwitches            offsetof(kgtBss, m_giDisplayModeSwitches)
#define BSS_OFS_gbDisplayModeReady               offsetof(kgtBss, m_gbDisplayModeReady)
#define BSS_OFS_g_424778                         offsetof(kgtBss, m_g_424778)
#define BSS_OFS_gszEmptyWindowName               offsetof(kgtBss, m_gszEmptyWindowName)
#define BSS_OFS_giMenuSelectionIdx               offsetof(kgtBss, m_giMenuSelectionIdx)
#define BSS_OFS_gszEmptyBgFile                   offsetof(kgtBss, m_gszEmptyBgFile)
#define BSS_OFS_giKgtCompressOutSize             offsetof(kgtBss, m_giKgtCompressOutSize)
#define BSS_OFS_gszEmptyIniDefault               offsetof(kgtBss, m_gszEmptyIniDefault)
#define BSS_OFS_ghWavFileAlloc                   offsetof(kgtBss, m_ghWavFileAlloc)
#define BSS_OFS_g_424794                         offsetof(kgtBss, m_g_424794)
#define BSS_OFS_ghTrackbarTime                   offsetof(kgtBss, m_ghTrackbarTime)
#define BSS_OFS_g_42479c                         offsetof(kgtBss, m_g_42479c)
#define BSS_OFS_grDialogRect                     offsetof(kgtBss, m_grDialogRect)
#define BSS_OFS_giTrackbarTimePos                offsetof(kgtBss, m_giTrackbarTimePos)
#define BSS_OFS_ghTrackbarRounds                 offsetof(kgtBss, m_ghTrackbarRounds)
#define BSS_OFS_ghTrackbarRoundsTeamVs           offsetof(kgtBss, m_ghTrackbarRoundsTeamVs)
#define BSS_OFS_giTrackbarSpeedPos               offsetof(kgtBss, m_giTrackbarSpeedPos)
#define BSS_OFS_giTrackbarRoundsPos              offsetof(kgtBss, m_giTrackbarRoundsPos)
#define BSS_OFS_gKeyConfigEdit                   offsetof(kgtBss, m_gKeyConfigEdit)
#define BSS_OFS_gJoyConfigEdit                   offsetof(kgtBss, m_gJoyConfigEdit)
#define BSS_OFS_giTrackbarRoundsTeamVsPos        offsetof(kgtBss, m_giTrackbarRoundsTeamVsPos)
#define BSS_OFS_ghTrackbarSpeed                  offsetof(kgtBss, m_ghTrackbarSpeed)
#define BSS_OFS_gpJoyInputButton                 offsetof(kgtBss, m_gpJoyInputButton)
#define BSS_OFS_ghJoyWindow                      offsetof(kgtBss, m_ghJoyWindow)
#define BSS_OFS_gpKeyInputTarget                 offsetof(kgtBss, m_gpKeyInputTarget)
#define BSS_OFS_giJoyInputPad                    offsetof(kgtBss, m_giJoyInputPad)
#define BSS_OFS_ghKeyInputWindow                 offsetof(kgtBss, m_ghKeyInputWindow)
#define BSS_OFS_gJoyCaps1                        offsetof(kgtBss, m_gJoyCaps1)
#define BSS_OFS_gJoyCaps2                        offsetof(kgtBss, m_gJoyCaps2)
#define BSS_OFS_gcKeyState                       offsetof(kgtBss, m_gcKeyState)
#define BSS_OFS_gdwJoystickX                     offsetof(kgtBss, m_gdwJoystickX)
#define BSS_OFS_gdwJoystickY                     offsetof(kgtBss, m_gdwJoystickY)
#define BSS_OFS_gdwJoystickTwoX                  offsetof(kgtBss, m_gdwJoystickTwoX)
#define BSS_OFS_gdwJoystickTwoY                  offsetof(kgtBss, m_gdwJoystickTwoY)
#define BSS_OFS_gpKgtCompressOut                 offsetof(kgtBss, m_gpKgtCompressOut)
#define BSS_OFS_giGameModes                      offsetof(kgtBss, m_giGameModes)
#define BSS_OFS_gkgtSelectCursor1                offsetof(kgtBss, m_gkgtSelectCursor1)
#define BSS_OFS_gkgtSelectCursor2                offsetof(kgtBss, m_gkgtSelectCursor2)
#define BSS_OFS_giAmountOfGameModes              offsetof(kgtBss, m_giAmountOfGameModes)
#define BSS_OFS_giRoundsNotWon                   offsetof(kgtBss, m_giRoundsNotWon)
#define BSS_OFS_gkgtStorySelectCursor            offsetof(kgtBss, m_gkgtStorySelectCursor)
#define BSS_OFS_gpTeamPortraits                  offsetof(kgtBss, m_gpTeamPortraits)
#define BSS_OFS_giBattlePrestartTimer            offsetof(kgtBss, m_giBattlePrestartTimer)
#define BSS_OFS_gcDemoSkipWithInput              offsetof(kgtBss, m_gcDemoSkipWithInput)
#define BSS_OFS_giDemoTime                       offsetof(kgtBss, m_giDemoTime)
#define BSS_OFS_giStoryFrontStageFlag            offsetof(kgtBss, m_giStoryFrontStageFlag)
#define BSS_OFS_gStoryMode                       offsetof(kgtBss, m_gStoryMode)
#define BSS_OFS_giStoryModePlayerIdx             (BSS_OFS_gStoryMode + 0x0)   /* gStoryMode + 0x0, 4 bytes */
#define BSS_OFS_giStoryModeSide                  (BSS_OFS_gStoryMode + 0x4)   /* gStoryMode + 0x4, 4 bytes */
#define BSS_OFS_giCurrentStoryStep               (BSS_OFS_gStoryMode + 0x8)   /* gStoryMode + 0x8, 12 bytes */
#define BSS_OFS_giStoryModeCurrentRound          offsetof(kgtBss, m_giStoryModeCurrentRound)
#define BSS_OFS_giStoryWinsP1                    offsetof(kgtBss, m_giStoryWinsP1)
#define BSS_OFS_giStoryWinsP2                    offsetof(kgtBss, m_giStoryWinsP2)
#define BSS_OFS_grWindowRect                     offsetof(kgtBss, m_grWindowRect)
#define BSS_OFS_gkgtBitmaps                      offsetof(kgtBss, m_gkgtBitmaps)
#define BSS_OFS_gdwFrameTimeDiff                 offsetof(kgtBss, m_gdwFrameTimeDiff)
#define BSS_OFS_gcKeyboardControlsSets           offsetof(kgtBss, m_gcKeyboardControlsSets)
#define BSS_OFS_giEngineObjectIter               offsetof(kgtBss, m_giEngineObjectIter)
#define BSS_OFS_gpDrawNodeNext                   offsetof(kgtBss, m_gpDrawNodeNext)
#define BSS_OFS_giUserKeydowns                   offsetof(kgtBss, m_giUserKeydowns)
#define BSS_OFS_grClipRect                       offsetof(kgtBss, m_grClipRect)
#define BSS_OFS_gDebugStatus                     offsetof(kgtBss, m_gDebugStatus)
#define BSS_OFS_gpGlobalMemoryAlloc              offsetof(kgtBss, m_gpGlobalMemoryAlloc)
#define BSS_OFS_giConfigGameWindowPointX         offsetof(kgtBss, m_giConfigGameWindowPointX)
#define BSS_OFS_giConfigGameWindowPointY         offsetof(kgtBss, m_giConfigGameWindowPointY)
#define BSS_OFS_gkgtLoadedDemo                   offsetof(kgtBss, m_gkgtLoadedDemo)
#define BSS_OFS_gpDirectSound                    offsetof(kgtBss, m_gpDirectSound)
#define BSS_OFS_guLocalIpAddr                    offsetof(kgtBss, m_guLocalIpAddr)
#define BSS_OFS_gdpidHost                        offsetof(kgtBss, m_gdpidHost)
#define BSS_OFS_giAnyInputXor                    offsetof(kgtBss, m_giAnyInputXor)
#define BSS_OFS_giInputBuffer                    offsetof(kgtBss, m_giInputBuffer)
#define BSS_OFS_gEditorTestplayConfig            offsetof(kgtBss, m_gEditorTestplayConfig)
#define BSS_OFS_giConfigTestplayPlayer0Nb        (BSS_OFS_gEditorTestplayConfig + 0x0)   /* gEditorTestplayConfig + 0x0, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer0Cpu       (BSS_OFS_gEditorTestplayConfig + 0x4)   /* gEditorTestplayConfig + 0x4, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer1Nb        (BSS_OFS_gEditorTestplayConfig + 0x10)   /* gEditorTestplayConfig + 0x10, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer1Cpu       (BSS_OFS_gEditorTestplayConfig + 0x14)   /* gEditorTestplayConfig + 0x14, 4 bytes */
#define BSS_OFS_giConfigTestplayHitjudge         (BSS_OFS_gEditorTestplayConfig + 0x20)   /* gEditorTestplayConfig + 0x20, 4 bytes */
#define BSS_OFS_giConfigTestplayGamespeed        (BSS_OFS_gEditorTestplayConfig + 0x24)   /* gEditorTestplayConfig + 0x24, 4 bytes */
#define BSS_OFS_giConfigTestplayGameinfo         (BSS_OFS_gEditorTestplayConfig + 0x28)   /* gEditorTestplayConfig + 0x28, 4 bytes */
#define BSS_OFS_giConfigTestplayStageNb          (BSS_OFS_gEditorTestplayConfig + 0x2c)   /* gEditorTestplayConfig + 0x2c, 4 bytes */
#define BSS_OFS_giConfigTestplayJoystick         (BSS_OFS_gEditorTestplayConfig + 0x30)   /* gEditorTestplayConfig + 0x30, 4 bytes */
#define BSS_OFS_giConfigTestplayTime             (BSS_OFS_gEditorTestplayConfig + 0x34)   /* gEditorTestplayConfig + 0x34, 4 bytes */
#define BSS_OFS_giConfigTestplayExit             (BSS_OFS_gEditorTestplayConfig + 0x38)   /* gEditorTestplayConfig + 0x38, 4 bytes */
#define BSS_OFS_giConfigTestplayVsMode           (BSS_OFS_gEditorTestplayConfig + 0x40)   /* gEditorTestplayConfig + 0x40, 4 bytes */
#define BSS_OFS_giConfigNumberOfRounds           (BSS_OFS_gEditorTestplayConfig + 0x44)   /* gEditorTestplayConfig + 0x44, 4 bytes */
#define BSS_OFS_giConfigNumberOfRoundsTeamVs     (BSS_OFS_gEditorTestplayConfig + 0x48)   /* gEditorTestplayConfig + 0x48, 4 bytes */
#define BSS_OFS_gszConfigReturnedFilename        (BSS_OFS_gEditorTestplayConfig + 0x4c)   /* gEditorTestplayConfig + 0x4c, 256 bytes */
#define BSS_OFS_giConfigEditorDemoNb             offsetof(kgtBss, m_giConfigEditorDemoNb)
#define BSS_OFS_gkgtDrawLayers                   offsetof(kgtBss, m_gkgtDrawLayers)
#define BSS_OFS_gpWavs                           offsetof(kgtBss, m_gpWavs)
#define BSS_OFS_gkgtKgtSystem                    offsetof(kgtBss, m_gkgtKgtSystem)
#define BSS_OFS_gshSystemVariables               (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x12470)   /* gkgtKgtSystem + 0x12470, 32 bytes */
#define BSS_OFS_giSystemFlashType                (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x12490)   /* gkgtKgtSystem + 0x12490, 4 bytes */
#define BSS_OFS_giSystemFlashRed                 (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x12494)   /* gkgtKgtSystem + 0x12494, 4 bytes */
#define BSS_OFS_giSystemFlashGreen               (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x12498)   /* gkgtKgtSystem + 0x12498, 4 bytes */
#define BSS_OFS_giSystemFlashBlue                (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x1249c)   /* gkgtKgtSystem + 0x1249c, 4 bytes */
#define BSS_OFS_giSystemFlashAlpha               (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x124a0)   /* gkgtKgtSystem + 0x124a0, 4 bytes */
#define BSS_OFS_giSystemFlashTimeLeft            (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x124a4)   /* gkgtKgtSystem + 0x124a4, 20 bytes */
#define BSS_OFS_giSystemFlashDuration            (BSS_OFS_gkgtKgtSystem + KGT_CORE_GROWTH + 0x124b8)   /* gkgtKgtSystem + 0x124b8, 4 bytes */
#define BSS_OFS_giReverseShakeDirection          offsetof(kgtBss, m_giReverseShakeDirection)
#define BSS_OFS_giGravityScalar                  offsetof(kgtBss, m_giGravityScalar)
#define BSS_OFS_giGamespeedFrames                offsetof(kgtBss, m_giGamespeedFrames)
#define BSS_OFS_gcJoystickButtons                offsetof(kgtBss, m_gcJoystickButtons)
#define BSS_OFS_gpLocalHostEnt                   offsetof(kgtBss, m_gpLocalHostEnt)
#define BSS_OFS_gkgtLoadedStage                  offsetof(kgtBss, m_gkgtLoadedStage)
#define BSS_OFS_giStageFlashType                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x263d)   /* gkgtLoadedStage + 0x263d, 4 bytes */
#define BSS_OFS_giStageFlashRed                  (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2641)   /* gkgtLoadedStage + 0x2641, 4 bytes */
#define BSS_OFS_giStageFlashGreen                (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2645)   /* gkgtLoadedStage + 0x2645, 4 bytes */
#define BSS_OFS_giStageFlashBlue                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2649)   /* gkgtLoadedStage + 0x2649, 4 bytes */
#define BSS_OFS_giStageFlashAlpha                (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x264d)   /* gkgtLoadedStage + 0x264d, 4 bytes */
#define BSS_OFS_giStageFlashTimeLeft             (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2651)   /* gkgtLoadedStage + 0x2651, 20 bytes */
#define BSS_OFS_giStageFlashDuration             (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2665)   /* gkgtLoadedStage + 0x2665, 4 bytes */
#define BSS_OFS_giShakeXMode                     (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2669)   /* gkgtLoadedStage + 0x2669, 4 bytes */
#define BSS_OFS_giShakeXOffset                   (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x266d)   /* gkgtLoadedStage + 0x266d, 4 bytes */
#define BSS_OFS_giShakeXAmplitude                (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2671)   /* gkgtLoadedStage + 0x2671, 4 bytes */
#define BSS_OFS_giShakeXTimeLeft                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2675)   /* gkgtLoadedStage + 0x2675, 4 bytes */
#define BSS_OFS_giShakeXDuration                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2679)   /* gkgtLoadedStage + 0x2679, 4 bytes */
#define BSS_OFS_giShakeYMode                     (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x267d)   /* gkgtLoadedStage + 0x267d, 4 bytes */
#define BSS_OFS_giShakeYOffset                   (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2681)   /* gkgtLoadedStage + 0x2681, 4 bytes */
#define BSS_OFS_giShakeYAmplitude                (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2685)   /* gkgtLoadedStage + 0x2685, 4 bytes */
#define BSS_OFS_giShakeYTimeLeft                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x2689)   /* gkgtLoadedStage + 0x2689, 4 bytes */
#define BSS_OFS_giShakeYDuration                 (BSS_OFS_gkgtLoadedStage + KGT_CORE_GROWTH + 0x268d)   /* gkgtLoadedStage + 0x268d, 4 bytes */
#define BSS_OFS_gdwSystemTime                    offsetof(kgtBss, m_gdwSystemTime)
#define BSS_OFS_gszLocalHostName                 offsetof(kgtBss, m_gszLocalHostName)
#define BSS_OFS_giInputBufferPos                 offsetof(kgtBss, m_giInputBufferPos)
#define BSS_OFS_giLastInput                      offsetof(kgtBss, m_giLastInput)
#define BSS_OFS_giConfigGameWindowSizeX          offsetof(kgtBss, m_giConfigGameWindowSizeX)
#define BSS_OFS_giConfigGameWindowSizeY          offsetof(kgtBss, m_giConfigGameWindowSizeY)
#define BSS_OFS_giStartGameUnusedB               offsetof(kgtBss, m_giStartGameUnusedB)
#define BSS_OFS_giCameraX                        offsetof(kgtBss, m_giCameraX)
#define BSS_OFS_giCameraY                        offsetof(kgtBss, m_giCameraY)
#define BSS_OFS_giLastInputCleaned               offsetof(kgtBss, m_giLastInputCleaned)
#define BSS_OFS_giLastInputXor                   offsetof(kgtBss, m_giLastInputXor)
#define BSS_OFS_gAfterImageTrails                offsetof(kgtBss, m_gAfterImageTrails)
#define BSS_OFS_gAfterImageLayers                (BSS_OFS_gAfterImageTrails + 0x0)   /* gAfterImageTrails + 0x0, 161600 bytes */
#define BSS_OFS_gAfterImageTrailsBase            (BSS_OFS_gAfterImageTrails - sizeof(unk_0x650_struct))   /* gAfterImageTrails - one element (in the original gkgtLoadedStage + 0x21f0) */
#define BSS_OFS_gkgtDebugLog                     offsetof(kgtBss, m_gkgtDebugLog)
#define BSS_OFS_gDDSurfaceDesc                   offsetof(kgtBss, m_gDDSurfaceDesc)
#define BSS_OFS_gszPlayerName                    offsetof(kgtBss, m_gszPlayerName)
#define BSS_OFS_gszSessionName                   offsetof(kgtBss, m_gszSessionName)
#define BSS_OFS_gkgtGameState                    offsetof(kgtBss, m_gkgtGameState)
#define BSS_OFS_ghInstance                       offsetof(kgtBss, m_ghInstance)
#define BSS_OFS_gkgtEngineObjects                offsetof(kgtBss, m_gkgtEngineObjects)
#define BSS_OFS_giPlayerFileIndices              offsetof(kgtBss, m_giPlayerFileIndices)
#define BSS_OFS_gpkgtCurrentEngineObject         offsetof(kgtBss, m_gpkgtCurrentEngineObject)
#define BSS_OFS_giAnyInput                       offsetof(kgtBss, m_giAnyInput)
#define BSS_OFS_gkgtDrawNodes                    offsetof(kgtBss, m_gkgtDrawNodes)
#define BSS_OFS_gwTintedPalette16                offsetof(kgtBss, m_gwTintedPalette16)
#define BSS_OFS_giAnyInputCleaned                offsetof(kgtBss, m_giAnyInputCleaned)
#define BSS_OFS_giInputCountdowns                offsetof(kgtBss, m_giInputCountdowns)
#define BSS_OFS_gszCurrentDirectory              offsetof(kgtBss, m_gszCurrentDirectory)
#define BSS_OFS_giConfigGameScreenMode           offsetof(kgtBss, m_giConfigGameScreenMode)
#define BSS_OFS_gkgtLoadedCharacter              offsetof(kgtBss, m_gkgtLoadedCharacter)
#define BSS_OFS_giPlayerMomentumScalar           offsetof(kgtBss, m_giPlayerMomentumScalar)
#define BSS_OFS_gpsLpAppName                     offsetof(kgtBss, m_gpsLpAppName)
#define BSS_OFS_giRepeatInput                    offsetof(kgtBss, m_giRepeatInput)

/* the variable `name` seen as an lvalue of type T */
#define BSS(T, name) (*(__typeof__(T) *)((unsigned char *)&gBss + BSS_OFS_##name))

#endif

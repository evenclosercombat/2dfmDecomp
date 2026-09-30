/*
 * game_bss.h - the game's uninitialized variables, laid out as in the original executable.
 *
 * The code relies on that layout in places: a variable reached through its neighbour (vMemzero of
 * gBmiFrame also clears the four handles after it, gAfterImageTrails is indexed from one element
 * before its start, ...) and "field" names for members inside a larger variable (the screen-shake
 * and flash variables behind the packed kgt_stage in gkgtLoadedStage, the test-play settings, the
 * story-mode progress).  So instead of separate C variables, whose relative placement the compiler
 * and linker are free to choose, all of them are parts of one object, gBss, at their original
 * offsets from 0x4213c0 (the unreferenced space of the original, g_XXXXXX, and LIBC's .bss between the
 * plain .bss and the communal variables are kept as filler).
 *
 * Each translation unit declares the variables it uses with the type it uses them with:
 *
 *     #define giScreenMode BSS(int, giScreenMode)       instead of   extern int giScreenMode;
 *
 * BSS(T, name) is the lvalue of type T at gBss + BSS_OFS_name.  As with the extern declarations of
 * the main branch, the type is the one the file uses (giHitJudge is int[2] in battle.c and int
 * elsewhere, gcKeyboardControlsSets a KEY_LAYOUT[2], a KEYCONFIG or a BYTE[2][17], ...).  The
 * kgtBss members are only storage (byte arrays); src/game_bss.c defines gBss and checks the
 * offsets.  Building needs -fno-strict-aliasing (the bytes are accessed with other types).
 *
 * Converted once from asm/game_bss.txt, the table the original-toolchain build uses on the main
 * branch (address, size, name; "field" rows are extra names inside other variables); maintained by
 * hand since.  To add a variable, give it a member (or a BSS_OFS_ inside an existing one) at its
 * original address and a check in src/game_bss.c.
 */
#ifndef GAME_BSS_H
#define GAME_BSS_H

#define BSS_ADDR 0x4213c0   /* original address of gBss */

typedef struct kgtBss {
    unsigned char _4213c0[4];                      /* 0x4213c0: (the blitter's last .bss dword; keeps the original alignment) */
    unsigned char g_4213c4[4];                     /* 0x4213c4 */
    unsigned char gdpidLocalPlayer[4];             /* 0x4213c8 */
    unsigned char g_4213cc[4];                     /* 0x4213cc */
    unsigned char gSessionGuids[320];              /* 0x4213d0: GUID[20] */
    unsigned char gServiceProviderGuids[320];      /* 0x421510: GUID[20] */
    unsigned char gBmiStrip[1064];                 /* 0x421650: BITMAPINFOHEADER + RGBQUAD[256] */
    unsigned char ghStripDc[4];                    /* 0x421a78 */
    unsigned char ghStripBitmap[4];                /* 0x421a7c */
    unsigned char ghStripOldBitmap[4];             /* 0x421a80 */
    unsigned char gpStripBits[4];                  /* 0x421a84 */
    unsigned char gszServiceProviderNames[5120];   /* 0x421a88: char[20][256] */
    unsigned char gszSessionNames[5120];           /* 0x422e88: char[20][256] */
    unsigned char gDpName[16];                     /* 0x424288: DPNAME */
    unsigned char gBmiFrame[1064];                 /* 0x424298: BITMAPINFOHEADER + RGBQUAD[256] */
    unsigned char ghFrameDc[4];                    /* 0x4246c0 */
    unsigned char ghFrameBitmap[4];                /* 0x4246c4 */
    unsigned char ghFrameOldBitmap[4];             /* 0x4246c8 */
    unsigned char gpFrameBits[4];                  /* 0x4246cc */
    unsigned char g_4246d0[16];                    /* 0x4246d0 */
    unsigned char gpDirectPlay[4];                 /* 0x4246e0 */
    unsigned char gpAppGuid[4];                    /* 0x4246e4 */
    unsigned char giNumServiceProviders[4];        /* 0x4246e8 */
    unsigned char giNumSessionsFound[4];           /* 0x4246ec */
    unsigned char g_4246f0[4];                     /* 0x4246f0 */
    unsigned char giSkipframeCount[4];             /* 0x4246f4 */
    unsigned char ghWnd[4];                        /* 0x4246f8 */
    unsigned char giObjectCount[4];                /* 0x4246fc */
    unsigned char giTimeAdjustmentFlag[4];         /* 0x424700 */
    unsigned char giScreenMode[4];                 /* 0x424704 */
    unsigned char giStartGameUnusedA[4];           /* 0x424708 */
    unsigned char giHitJudge[8];                   /* 0x42470c: int[2] */
    unsigned char gbStoryMode[4];                  /* 0x424714 */
    unsigned char giForceRoundEnd[4];              /* 0x424718: engine.c: giForceRoundEnd */
    unsigned char gbShowDebugStatus[4];            /* 0x42471c */
    unsigned char guCdAudioDeviceId[4];            /* 0x424720 */
    unsigned char gbCdAudioOpen[4];                /* 0x424724 */
    unsigned char giDiscLoopFlag[4];               /* 0x424728 */
    unsigned char giCdAudioError[4];               /* 0x42472c */
    unsigned char g_424730[4];                     /* 0x424730 */
    unsigned char giCdAudioRequested[4];           /* 0x424734 */
    unsigned char gbMidiOpen[4];                   /* 0x424738 */
    unsigned char guMidiDeviceId[4];               /* 0x42473c */
    unsigned char giMidiLoopFlag[4];               /* 0x424740 */
    unsigned char giAppmode[4];                    /* 0x424744 */
    unsigned char gszEmptyCommandName[4];          /* 0x424748: a "" literal of globals.c in the original (unused here: the code writes "") */
    unsigned char giDsoundInitializedFlag[4];      /* 0x42474c */
    unsigned char gpDDSPrimary[4];                 /* 0x424750 */
    unsigned char gpDDSBack[4];                    /* 0x424754 */
    unsigned char gpDirectDraw[4];                 /* 0x424758 */
    unsigned char gbSaveWindowRect[4];             /* 0x42475c */
    unsigned char gbDdrawInitialized[4];           /* 0x424760 */
    unsigned char g_424764[4];                     /* 0x424764 */
    unsigned char giModeSwitchFrames[4];           /* 0x424768 */
    unsigned char gbSwitchingDisplayMode[4];       /* 0x42476c */
    unsigned char giDisplayModeSwitches[4];        /* 0x424770 */
    unsigned char gbDisplayModeReady[4];           /* 0x424774 */
    unsigned char g_424778[4];                     /* 0x424778 */
    unsigned char gszEmptyWindowName[4];           /* 0x42477c: main's "" literal in the original (unused here) */
    unsigned char giMenuSelectionIdx[4];           /* 0x424780 */
    unsigned char gszEmptyBgFile[4];               /* 0x424784: engine's "" literal in the original (unused here) */
    unsigned char giKgtCompressOutSize[4];         /* 0x424788 */
    unsigned char gszEmptyIniDefault[4];           /* 0x42478c: config's "" literal in the original (unused here) */
    unsigned char ghWavFileAlloc[4];               /* 0x424790 */
    unsigned char g_424794[4];                     /* 0x424794 */
    unsigned char ghTrackbarTime[4];               /* 0x424798 */
    unsigned char g_42479c[4];                     /* 0x42479c */
    unsigned char grDialogRect[16];                /* 0x4247a0: RECT */
    unsigned char giTrackbarTimePos[4];            /* 0x4247b0 */
    unsigned char ghTrackbarRounds[4];             /* 0x4247b4 */
    unsigned char ghTrackbarRoundsTeamVs[4];       /* 0x4247b8 */
    unsigned char giTrackbarSpeedPos[4];           /* 0x4247bc */
    unsigned char giTrackbarRoundsPos[4];          /* 0x4247c0 */
    unsigned char gKeyConfigEdit[34];              /* 0x4247c4: KEYCONFIG (BYTE[2][17]) */
    unsigned char _4247e6[2];                      /* 0x4247e6: (alignment) */
    unsigned char gJoyConfigEdit[14];              /* 0x4247e8: JOYCONFIG (BYTE[2][7]) */
    unsigned char _4247f6[2];                      /* 0x4247f6: (alignment) */
    unsigned char giTrackbarRoundsTeamVsPos[4];    /* 0x4247f8 */
    unsigned char ghTrackbarSpeed[4];              /* 0x4247fc */
    unsigned char _424800[4];                      /* 0x424800: (dialogs.c's static giJoystickInputTimer in the original) */
    unsigned char _424804[428];                    /* 0x424804: (LIBC's .bss in the original) */
    /* ---- communal variables ---- */
    unsigned char gpJoyInputButton[4];             /* 0x4249b0 */
    unsigned char ghJoyWindow[4];                  /* 0x4249b4 */
    unsigned char gpKeyInputTarget[4];             /* 0x4249b8 */
    unsigned char giJoyInputPad[4];                /* 0x4249bc */
    unsigned char ghKeyInputWindow[4];             /* 0x4249c0 */
    unsigned char _4249c4[28];                     /* 0x4249c4: (alignment) */
    unsigned char gJoyCaps1[404];                  /* 0x4249e0: JOYCAPSA */
    unsigned char _424b74[12];                     /* 0x424b74: (alignment) */
    unsigned char gJoyCaps2[404];                  /* 0x424b80: JOYCAPSA */
    unsigned char _424d14[12];                     /* 0x424d14: (alignment) */
    unsigned char gcKeyState[256];                 /* 0x424d20 */
    unsigned char gdwJoystickX[4];                 /* 0x424e20 */
    unsigned char gdwJoystickY[4];                 /* 0x424e24 */
    unsigned char gdwJoystickTwoX[4];              /* 0x424e28 */
    unsigned char gdwJoystickTwoY[4];              /* 0x424e2c */
    unsigned char gpKgtCompressOut[4];             /* 0x424e30 */
    unsigned char _424e34[12];                     /* 0x424e34: (alignment) */
    unsigned char giGameModes[12];                 /* 0x424e40: int[3] */
    unsigned char _424e4c[4];                      /* 0x424e4c: (alignment) */
    unsigned char gkgtSelectCursor1[8];            /* 0x424e50: kgtGridCoordinates */
    unsigned char gkgtSelectCursor2[8];            /* 0x424e58 */
    unsigned char giAmountOfGameModes[4];          /* 0x424e60 */
    unsigned char giRoundsNotWon[4];               /* 0x424e64 */
    unsigned char gkgtStorySelectCursor[8];        /* 0x424e68 */
    unsigned char _424e70[16];                     /* 0x424e70: (alignment) */
    unsigned char gpTeamPortraits[128];            /* 0x424e80: kgtEngineObject *[32]: [side * 4 + member]; the size is what puts giBattlePrestartTimer at 0x424f00 */
    unsigned char giBattlePrestartTimer[4];        /* 0x424f00 */
    unsigned char gcDemoSkipWithInput[1];          /* 0x424f04: char */
    unsigned char _424f05[3];                      /* 0x424f05: (alignment) */
    unsigned char giDemoTime[4];                   /* 0x424f08 */
    unsigned char giStoryFrontStageFlag[4];        /* 0x424f0c */
    unsigned char _424f10[16];                     /* 0x424f10: (alignment) */
    unsigned char gStoryMode[20];                  /* 0x424f20: { int iPlayerIdx; int iPlayerIdx_2; int aiStep[3]; } */
    unsigned char giStoryModeCurrentRound[4];      /* 0x424f34 */
    unsigned char giStoryWinsP1[4];                /* 0x424f38 */
    unsigned char giStoryWinsP2[4];                /* 0x424f3c */
    unsigned char grWindowRect[16];                /* 0x424f40: RECT */
    unsigned char _424f50[16];                     /* 0x424f50: (alignment) */
    unsigned char gkgtBitmaps[2560];               /* 0x424f60: kgtBMPINFO[128] */
    unsigned char gdwFrameTimeDiff[4];             /* 0x425960 */
    unsigned char _425964[28];                     /* 0x425964: (alignment) */
    unsigned char gcKeyboardControlsSets[34];      /* 0x425980: KEY_LAYOUT[2] */
    unsigned char _4259a2[2];                      /* 0x4259a2: (alignment) */
    unsigned char giEngineObjectIter[4];           /* 0x4259a4 */
    unsigned char gpDrawNodeNext[4];               /* 0x4259a8 */
    unsigned char _4259ac[20];                     /* 0x4259ac: (alignment) */
    unsigned char giUserKeydowns[32];              /* 0x4259c0: int[8] */
    unsigned char grClipRect[16];                  /* 0x4259e0: RECT */
    unsigned char _4259f0[16];                     /* 0x4259f0: (alignment) */
    unsigned char gDebugStatus[68];                /* 0x425a00: DEBUG_B */
    unsigned char gpGlobalMemoryAlloc[4];          /* 0x425a44 */
    unsigned char giConfigGameWindowPointX[4];     /* 0x425a48 */
    unsigned char giConfigGameWindowPointY[4];     /* 0x425a4c */
    unsigned char _425a50[16];                     /* 0x425a50: (alignment) */
    unsigned char gkgtLoadedDemo[9833];            /* 0x425a60: kgt_demo_file */
    unsigned char _4280c9[3];                      /* 0x4280c9: (alignment) */
    unsigned char gpDirectSound[4];                /* 0x4280cc */
    unsigned char guLocalIpAddr[4];                /* 0x4280d0 */
    unsigned char gdpidHost[4];                    /* 0x4280d4 */
    unsigned char giAnyInputXor[4];                /* 0x4280d8 */
    unsigned char _4280dc[4];                      /* 0x4280dc: (alignment) */
    unsigned char giInputBuffer[32768];            /* 0x4280e0: DWORD[8][1024] */
    unsigned char gEditorTestplayConfig[332];      /* 0x4300e0 */
    unsigned char giConfigEditorDemoNb[4];         /* 0x43022c */
    unsigned char _430230[16];                     /* 0x430230: (alignment) */
    unsigned char gkgtDrawLayers[1024];            /* 0x430240: kgtDrawLayer[128] */
    unsigned char gpWavs[11264];                   /* 0x430640: kgtWav *[11][256] */
    unsigned char gkgtKgtSystem[74940];            /* 0x433240: kgtSystem */
    unsigned char giReverseShakeDirection[4];      /* 0x4456fc */
    unsigned char giGravityScalar[4];              /* 0x445700 */
    unsigned char giGamespeedFrames[4];            /* 0x445704 */
    unsigned char _445708[8];                      /* 0x445708: (alignment) */
    unsigned char gcJoystickButtons[14];           /* 0x445710: JOYCONFIG (BYTE[2][7]) */
    unsigned char _44571e[2];                      /* 0x44571e: (alignment) */
    unsigned char gpLocalHostEnt[4];               /* 0x445720 */
    unsigned char _445724[28];                     /* 0x445724: (alignment) */
    unsigned char gkgtLoadedStage[9873];           /* 0x445740: kgt_stage (packed) */
    unsigned char _447dd1[3];                      /* 0x447dd1: (alignment) */
    unsigned char gdwSystemTime[4];                /* 0x447dd4 */
    unsigned char _447dd8[8];                      /* 0x447dd8: (alignment) */
    unsigned char gszLocalHostName[256];           /* 0x447de0 */
    unsigned char giInputBufferPos[4];             /* 0x447ee0 */
    unsigned char _447ee4[28];                     /* 0x447ee4: (alignment) */
    unsigned char giLastInput[32];                 /* 0x447f00: int[8] */
    unsigned char giConfigGameWindowSizeX[4];      /* 0x447f20 */
    unsigned char giConfigGameWindowSizeY[4];      /* 0x447f24 */
    unsigned char giStartGameUnusedB[4];           /* 0x447f28 */
    unsigned char giCameraX[4];                    /* 0x447f2c */
    unsigned char giCameraY[4];                    /* 0x447f30 */
    unsigned char _447f34[12];                     /* 0x447f34: (alignment) */
    unsigned char giLastInputCleaned[32];          /* 0x447f40: DWORD[8] */
    unsigned char giLastInputXor[32];              /* 0x447f60: DWORD[8] */
    unsigned char gAfterImageTrails[161600];       /* 0x447f80: unk_0x650_struct[100] */
    unsigned char gkgtDebugLog[2160];              /* 0x46f6c0: kgt_debug_a */
    unsigned char _46ff30[16];                     /* 0x46ff30: (alignment) */
    unsigned char gDDSurfaceDesc[108];             /* 0x46ff40: DDSURFACEDESC */
    unsigned char _46ffac[20];                     /* 0x46ffac: (alignment) */
    unsigned char gszPlayerName[32];               /* 0x46ffc0 */
    unsigned char gszSessionName[64];              /* 0x46ffe0: used as char[32], but something must fill 0x470000-0x47001f */
    unsigned char gkgtGameState[428];              /* 0x470020: kgtGameState */
    unsigned char ghInstance[4];                   /* 0x4701cc */
    unsigned char _4701d0[16];                     /* 0x4701d0: (alignment) */
    unsigned char gkgtEngineObjects[391168];       /* 0x4701e0: kgtEngineObject[1024] */
    unsigned char giPlayerFileIndices[32];         /* 0x4cf9e0: int[8] */
    unsigned char gpkgtCurrentEngineObject[4];     /* 0x4cfa00 */
    unsigned char giAnyInput[4];                   /* 0x4cfa04 */
    unsigned char _4cfa08[24];                     /* 0x4cfa08: (alignment) */
    unsigned char gkgtDrawNodes[8192];             /* 0x4cfa20: kgtDrawNode[1024] */
    unsigned char gwTintedPalette16[512];          /* 0x4d1a20: WORD[256] */
    unsigned char giAnyInputCleaned[4];            /* 0x4d1c20 */
    unsigned char _4d1c24[28];                     /* 0x4d1c24: (alignment) */
    unsigned char giInputCountdowns[32];           /* 0x4d1c40: int[8] */
    unsigned char gszCurrentDirectory[256];        /* 0x4d1c60 */
    unsigned char giConfigGameScreenMode[4];       /* 0x4d1d60 */
    unsigned char _4d1d64[28];                     /* 0x4d1d64: (alignment) */
    unsigned char gkgtLoadedCharacter[459256];     /* 0x4d1d80: kgt_character_struct[8] */
    unsigned char giPlayerMomentumScalar[4];       /* 0x541f78 */
    unsigned char gpsLpAppName[4];                 /* 0x541f7c */
    unsigned char giRepeatInput[32];               /* 0x541f80: int[8] */
} kgtBss;

extern kgtBss gBss;

/* offset of each name in gBss (fields: inside the variable named in the comment) */
#define BSS_OFS_g_4213c4                         0x000004
#define BSS_OFS_gdpidLocalPlayer                 0x000008
#define BSS_OFS_g_4213cc                         0x00000c
#define BSS_OFS_gSessionGuids                    0x000010
#define BSS_OFS_gServiceProviderGuids            0x000150
#define BSS_OFS_gBmiStrip                        0x000290
#define BSS_OFS_ghStripDc                        0x0006b8
#define BSS_OFS_ghStripBitmap                    0x0006bc
#define BSS_OFS_ghStripOldBitmap                 0x0006c0
#define BSS_OFS_gpStripBits                      0x0006c4
#define BSS_OFS_gszServiceProviderNames          0x0006c8
#define BSS_OFS_gszSessionNames                  0x001ac8
#define BSS_OFS_gDpName                          0x002ec8
#define BSS_OFS_gBmiFrame                        0x002ed8
#define BSS_OFS_ghFrameDc                        0x003300
#define BSS_OFS_ghFrameBitmap                    0x003304
#define BSS_OFS_ghFrameOldBitmap                 0x003308
#define BSS_OFS_gpFrameBits                      0x00330c
#define BSS_OFS_g_4246d0                         0x003310
#define BSS_OFS_gpDirectPlay                     0x003320
#define BSS_OFS_gpAppGuid                        0x003324
#define BSS_OFS_giNumServiceProviders            0x003328
#define BSS_OFS_giNumSessionsFound               0x00332c
#define BSS_OFS_g_4246f0                         0x003330
#define BSS_OFS_giSkipframeCount                 0x003334
#define BSS_OFS_ghWnd                            0x003338
#define BSS_OFS_giObjectCount                    0x00333c
#define BSS_OFS_giTimeAdjustmentFlag             0x003340
#define BSS_OFS_giScreenMode                     0x003344
#define BSS_OFS_giStartGameUnusedA               0x003348
#define BSS_OFS_giHitJudge                       0x00334c
#define BSS_OFS_gbStoryMode                      0x003354
#define BSS_OFS_giForceRoundEnd                  0x003358
#define BSS_OFS_gbShowDebugStatus                0x00335c
#define BSS_OFS_guCdAudioDeviceId                0x003360
#define BSS_OFS_gbCdAudioOpen                    0x003364
#define BSS_OFS_giDiscLoopFlag                   0x003368
#define BSS_OFS_giCdAudioError                   0x00336c
#define BSS_OFS_g_424730                         0x003370
#define BSS_OFS_giCdAudioRequested               0x003374
#define BSS_OFS_gbMidiOpen                       0x003378
#define BSS_OFS_guMidiDeviceId                   0x00337c
#define BSS_OFS_giMidiLoopFlag                   0x003380
#define BSS_OFS_giAppmode                        0x003384
#define BSS_OFS_gszEmptyCommandName              0x003388
#define BSS_OFS_giDsoundInitializedFlag          0x00338c
#define BSS_OFS_gpDDSPrimary                     0x003390
#define BSS_OFS_gpDDSBack                        0x003394
#define BSS_OFS_gpDirectDraw                     0x003398
#define BSS_OFS_gbSaveWindowRect                 0x00339c
#define BSS_OFS_gbDdrawInitialized               0x0033a0
#define BSS_OFS_g_424764                         0x0033a4
#define BSS_OFS_giModeSwitchFrames               0x0033a8
#define BSS_OFS_gbSwitchingDisplayMode           0x0033ac
#define BSS_OFS_giDisplayModeSwitches            0x0033b0
#define BSS_OFS_gbDisplayModeReady               0x0033b4
#define BSS_OFS_g_424778                         0x0033b8
#define BSS_OFS_gszEmptyWindowName               0x0033bc
#define BSS_OFS_giMenuSelectionIdx               0x0033c0
#define BSS_OFS_gszEmptyBgFile                   0x0033c4
#define BSS_OFS_giKgtCompressOutSize             0x0033c8
#define BSS_OFS_gszEmptyIniDefault               0x0033cc
#define BSS_OFS_ghWavFileAlloc                   0x0033d0
#define BSS_OFS_g_424794                         0x0033d4
#define BSS_OFS_ghTrackbarTime                   0x0033d8
#define BSS_OFS_g_42479c                         0x0033dc
#define BSS_OFS_grDialogRect                     0x0033e0
#define BSS_OFS_giTrackbarTimePos                0x0033f0
#define BSS_OFS_ghTrackbarRounds                 0x0033f4
#define BSS_OFS_ghTrackbarRoundsTeamVs           0x0033f8
#define BSS_OFS_giTrackbarSpeedPos               0x0033fc
#define BSS_OFS_giTrackbarRoundsPos              0x003400
#define BSS_OFS_gKeyConfigEdit                   0x003404
#define BSS_OFS_gJoyConfigEdit                   0x003428
#define BSS_OFS_giTrackbarRoundsTeamVsPos        0x003438
#define BSS_OFS_ghTrackbarSpeed                  0x00343c
#define BSS_OFS_gpJoyInputButton                 0x0035f0
#define BSS_OFS_ghJoyWindow                      0x0035f4
#define BSS_OFS_gpKeyInputTarget                 0x0035f8
#define BSS_OFS_giJoyInputPad                    0x0035fc
#define BSS_OFS_ghKeyInputWindow                 0x003600
#define BSS_OFS_gJoyCaps1                        0x003620
#define BSS_OFS_gJoyCaps2                        0x0037c0
#define BSS_OFS_gcKeyState                       0x003960
#define BSS_OFS_gdwJoystickX                     0x003a60
#define BSS_OFS_gdwJoystickY                     0x003a64
#define BSS_OFS_gdwJoystickTwoX                  0x003a68
#define BSS_OFS_gdwJoystickTwoY                  0x003a6c
#define BSS_OFS_gpKgtCompressOut                 0x003a70
#define BSS_OFS_giGameModes                      0x003a80
#define BSS_OFS_gkgtSelectCursor1                0x003a90
#define BSS_OFS_gkgtSelectCursor2                0x003a98
#define BSS_OFS_giAmountOfGameModes              0x003aa0
#define BSS_OFS_giRoundsNotWon                   0x003aa4
#define BSS_OFS_gkgtStorySelectCursor            0x003aa8
#define BSS_OFS_gpTeamPortraits                  0x003ac0
#define BSS_OFS_giBattlePrestartTimer            0x003b40
#define BSS_OFS_gcDemoSkipWithInput              0x003b44
#define BSS_OFS_giDemoTime                       0x003b48
#define BSS_OFS_giStoryFrontStageFlag            0x003b4c
#define BSS_OFS_gStoryMode                       0x003b60
#define BSS_OFS_giStoryModePlayerIdx             0x003b60   /* gStoryMode + 0x0, 4 bytes */
#define BSS_OFS_giStoryModeSide                  0x003b64   /* gStoryMode + 0x4, 4 bytes */
#define BSS_OFS_giCurrentStoryStep               0x003b68   /* gStoryMode + 0x8, 12 bytes */
#define BSS_OFS_giStoryModeCurrentRound          0x003b74
#define BSS_OFS_giStoryWinsP1                    0x003b78
#define BSS_OFS_giStoryWinsP2                    0x003b7c
#define BSS_OFS_grWindowRect                     0x003b80
#define BSS_OFS_gkgtBitmaps                      0x003ba0
#define BSS_OFS_gdwFrameTimeDiff                 0x0045a0
#define BSS_OFS_gcKeyboardControlsSets           0x0045c0
#define BSS_OFS_giEngineObjectIter               0x0045e4
#define BSS_OFS_gpDrawNodeNext                   0x0045e8
#define BSS_OFS_giUserKeydowns                   0x004600
#define BSS_OFS_grClipRect                       0x004620
#define BSS_OFS_gDebugStatus                     0x004640
#define BSS_OFS_gpGlobalMemoryAlloc              0x004684
#define BSS_OFS_giConfigGameWindowPointX         0x004688
#define BSS_OFS_giConfigGameWindowPointY         0x00468c
#define BSS_OFS_gkgtLoadedDemo                   0x0046a0
#define BSS_OFS_gpDirectSound                    0x006d0c
#define BSS_OFS_guLocalIpAddr                    0x006d10
#define BSS_OFS_gdpidHost                        0x006d14
#define BSS_OFS_giAnyInputXor                    0x006d18
#define BSS_OFS_giInputBuffer                    0x006d20
#define BSS_OFS_gEditorTestplayConfig            0x00ed20
#define BSS_OFS_giConfigTestplayPlayer0Nb        0x00ed20   /* gEditorTestplayConfig + 0x0, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer0Cpu       0x00ed24   /* gEditorTestplayConfig + 0x4, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer1Nb        0x00ed30   /* gEditorTestplayConfig + 0x10, 4 bytes */
#define BSS_OFS_giConfigTestplayPlayer1Cpu       0x00ed34   /* gEditorTestplayConfig + 0x14, 4 bytes */
#define BSS_OFS_giConfigTestplayHitjudge         0x00ed40   /* gEditorTestplayConfig + 0x20, 4 bytes */
#define BSS_OFS_giConfigTestplayGamespeed        0x00ed44   /* gEditorTestplayConfig + 0x24, 4 bytes */
#define BSS_OFS_giConfigTestplayGameinfo         0x00ed48   /* gEditorTestplayConfig + 0x28, 4 bytes */
#define BSS_OFS_giConfigTestplayStageNb          0x00ed4c   /* gEditorTestplayConfig + 0x2c, 4 bytes */
#define BSS_OFS_giConfigTestplayJoystick         0x00ed50   /* gEditorTestplayConfig + 0x30, 4 bytes */
#define BSS_OFS_giConfigTestplayTime             0x00ed54   /* gEditorTestplayConfig + 0x34, 4 bytes */
#define BSS_OFS_giConfigTestplayExit             0x00ed58   /* gEditorTestplayConfig + 0x38, 4 bytes */
#define BSS_OFS_giConfigTestplayVsMode           0x00ed60   /* gEditorTestplayConfig + 0x40, 4 bytes */
#define BSS_OFS_giConfigNumberOfRounds           0x00ed64   /* gEditorTestplayConfig + 0x44, 4 bytes */
#define BSS_OFS_giConfigNumberOfRoundsTeamVs     0x00ed68   /* gEditorTestplayConfig + 0x48, 4 bytes */
#define BSS_OFS_gszConfigReturnedFilename        0x00ed6c   /* gEditorTestplayConfig + 0x4c, 256 bytes */
#define BSS_OFS_giConfigEditorDemoNb             0x00ee6c
#define BSS_OFS_gkgtDrawLayers                   0x00ee80
#define BSS_OFS_gpWavs                           0x00f280
#define BSS_OFS_gkgtKgtSystem                    0x011e80
#define BSS_OFS_gshSystemVariables               0x0242f0   /* gkgtKgtSystem + 0x12470, 32 bytes */
#define BSS_OFS_giSystemFlashType                0x024310   /* gkgtKgtSystem + 0x12490, 4 bytes */
#define BSS_OFS_giSystemFlashRed                 0x024314   /* gkgtKgtSystem + 0x12494, 4 bytes */
#define BSS_OFS_giSystemFlashGreen               0x024318   /* gkgtKgtSystem + 0x12498, 4 bytes */
#define BSS_OFS_giSystemFlashBlue                0x02431c   /* gkgtKgtSystem + 0x1249c, 4 bytes */
#define BSS_OFS_giSystemFlashAlpha               0x024320   /* gkgtKgtSystem + 0x124a0, 4 bytes */
#define BSS_OFS_giSystemFlashTimeLeft            0x024324   /* gkgtKgtSystem + 0x124a4, 20 bytes */
#define BSS_OFS_giSystemFlashDuration            0x024338   /* gkgtKgtSystem + 0x124b8, 4 bytes */
#define BSS_OFS_giReverseShakeDirection          0x02433c
#define BSS_OFS_giGravityScalar                  0x024340
#define BSS_OFS_giGamespeedFrames                0x024344
#define BSS_OFS_gcJoystickButtons                0x024350
#define BSS_OFS_gpLocalHostEnt                   0x024360
#define BSS_OFS_gkgtLoadedStage                  0x024380
#define BSS_OFS_giStageFlashType                 0x0269bd   /* gkgtLoadedStage + 0x263d, 4 bytes */
#define BSS_OFS_giStageFlashRed                  0x0269c1   /* gkgtLoadedStage + 0x2641, 4 bytes */
#define BSS_OFS_giStageFlashGreen                0x0269c5   /* gkgtLoadedStage + 0x2645, 4 bytes */
#define BSS_OFS_giStageFlashBlue                 0x0269c9   /* gkgtLoadedStage + 0x2649, 4 bytes */
#define BSS_OFS_giStageFlashAlpha                0x0269cd   /* gkgtLoadedStage + 0x264d, 4 bytes */
#define BSS_OFS_giStageFlashTimeLeft             0x0269d1   /* gkgtLoadedStage + 0x2651, 20 bytes */
#define BSS_OFS_giStageFlashDuration             0x0269e5   /* gkgtLoadedStage + 0x2665, 4 bytes */
#define BSS_OFS_giShakeXMode                     0x0269e9   /* gkgtLoadedStage + 0x2669, 4 bytes */
#define BSS_OFS_giShakeXOffset                   0x0269ed   /* gkgtLoadedStage + 0x266d, 4 bytes */
#define BSS_OFS_giShakeXAmplitude                0x0269f1   /* gkgtLoadedStage + 0x2671, 4 bytes */
#define BSS_OFS_giShakeXTimeLeft                 0x0269f5   /* gkgtLoadedStage + 0x2675, 4 bytes */
#define BSS_OFS_giShakeXDuration                 0x0269f9   /* gkgtLoadedStage + 0x2679, 4 bytes */
#define BSS_OFS_giShakeYMode                     0x0269fd   /* gkgtLoadedStage + 0x267d, 4 bytes */
#define BSS_OFS_giShakeYOffset                   0x026a01   /* gkgtLoadedStage + 0x2681, 4 bytes */
#define BSS_OFS_giShakeYAmplitude                0x026a05   /* gkgtLoadedStage + 0x2685, 4 bytes */
#define BSS_OFS_giShakeYTimeLeft                 0x026a09   /* gkgtLoadedStage + 0x2689, 4 bytes */
#define BSS_OFS_giShakeYDuration                 0x026a0d   /* gkgtLoadedStage + 0x268d, 4 bytes */
#define BSS_OFS_gdwSystemTime                    0x026a14
#define BSS_OFS_gszLocalHostName                 0x026a20
#define BSS_OFS_giInputBufferPos                 0x026b20
#define BSS_OFS_giLastInput                      0x026b40
#define BSS_OFS_giConfigGameWindowSizeX          0x026b60
#define BSS_OFS_giConfigGameWindowSizeY          0x026b64
#define BSS_OFS_giStartGameUnusedB               0x026b68
#define BSS_OFS_giCameraX                        0x026b6c
#define BSS_OFS_giCameraY                        0x026b70
#define BSS_OFS_giLastInputCleaned               0x026b80
#define BSS_OFS_giLastInputXor                   0x026ba0
#define BSS_OFS_gAfterImageTrails                0x026bc0
#define BSS_OFS_gAfterImageLayers                0x026bc0   /* gAfterImageTrails + 0x0, 161600 bytes */
#define BSS_OFS_gAfterImageTrailsBase            0x026570   /* gkgtLoadedStage + 0x21f0, 1 bytes */
#define BSS_OFS_gkgtDebugLog                     0x04e300
#define BSS_OFS_gDDSurfaceDesc                   0x04eb80
#define BSS_OFS_gszPlayerName                    0x04ec00
#define BSS_OFS_gszSessionName                   0x04ec20
#define BSS_OFS_gkgtGameState                    0x04ec60
#define BSS_OFS_ghInstance                       0x04ee0c
#define BSS_OFS_gkgtEngineObjects                0x04ee20
#define BSS_OFS_giPlayerFileIndices              0x0ae620
#define BSS_OFS_gpkgtCurrentEngineObject         0x0ae640
#define BSS_OFS_giAnyInput                       0x0ae644
#define BSS_OFS_gkgtDrawNodes                    0x0ae660
#define BSS_OFS_gwTintedPalette16                0x0b0660
#define BSS_OFS_giAnyInputCleaned                0x0b0860
#define BSS_OFS_giInputCountdowns                0x0b0880
#define BSS_OFS_gszCurrentDirectory              0x0b08a0
#define BSS_OFS_giConfigGameScreenMode           0x0b09a0
#define BSS_OFS_gkgtLoadedCharacter              0x0b09c0
#define BSS_OFS_giPlayerMomentumScalar           0x120bb8
#define BSS_OFS_gpsLpAppName                     0x120bbc
#define BSS_OFS_giRepeatInput                    0x120bc0

/* the variable `name` seen as an lvalue of type T */
#define BSS(T, name) (*(__typeof__(T) *)((unsigned char *)&gBss + BSS_OFS_##name))

#endif

/*
 * globals.h - game-wide variables.
 *
 * Uninitialized variables are defined in asm/game_bss.txt (see tools/gen_game_bss.py), which
 * reproduces the original memory layout; initialized ones are defined in the C files noted below.
 */
#ifndef GLOBALS_H
#define GLOBALS_H

/* ---- netplay (online.c) ---- */
extern LPDIRECTPLAY2A gpDirectPlay;            /* 0x4246e0: DirectPlay interface */
extern LPGUID gpAppGuid;                       /* 0x4246e4: the game's DirectPlay application GUID */
extern int giNumServiceProviders;              /* 0x4246e8: entries in gServiceProviderGuids / gszServiceProviderNames */
extern int giNumSessionsFound;                 /* 0x4246ec: entries in gSessionGuids / gszSessionNames */
extern DPID gdpidLocalPlayer;                  /* 0x4213c8: DirectPlay id of the local player */
extern DPID gdpidHost;                         /* 0x4280d4: DirectPlay id of the session host */
extern GUID gSessionGuids[20];                 /* 0x4213d0: sessions found by iOnlineEnumSessions */
extern GUID gServiceProviderGuids[20];         /* 0x421510: DirectPlay service providers */
extern char gszServiceProviderNames[20][256];  /* 0x421a88: their names */
extern char gszSessionNames[20][256];          /* 0x422e88: names of the sessions found */
extern DPNAME gDpName;                         /* 0x424288: DirectPlay name of the local player */
extern char gszPlayerName[32];                 /* 0x46ffc0: player name (ini PlayerName) */
extern char gszSessionName[32];                /* 0x46ffe0: session name (ini SessionName); used as char[32] */
extern u_long guLocalIpAddr;                   /* 0x4280d0: local IP address (network order) */
extern struct hostent *gpLocalHostEnt;         /* 0x445720: gethostbyname result for the local host */
extern char gszLocalHostName[256];             /* 0x447de0: local host name */
extern int giLocalOnlineSlot;                  /* 0x41e3f8: gkgtLoadedCharacter slot of the local player, -1 = none */
extern DWORD giInputBuffer[8][1024];           /* 0x4280e0: input history per player, ring of 1024 frames (giInputBufferPos); bits: 1 back (left outside fights), 2 forward (right outside fights), 4 up, 8 down, 0x10-0x200 buttons A-F, 0x400 pause */

/* ---- sound ---- */
extern LPDIRECTSOUND gpDirectSound;         /* 0x4280cc: DirectSound interface */
extern int giDsoundInitializedFlag;         /* 0x42474c: DirectSound was set up */
extern kgtWav *gpWavs[11][256];             /* 0x430640: [iWavBank][sound index]; banks: 0 system, 1 demo, 2 stage, 3+ players */

/* ---- KGT data files ---- */
extern kgtSystem gkgtKgtSystem;             /* 0x433240: the system file */
extern kgt_demo_file gkgtLoadedDemo;        /* 0x425a60: the demo file being played */
extern kgt_stage gkgtLoadedStage;           /* 0x445740: the stage file in use (packed; its runtime effects follow at + 0x263d) */
extern int giPlayerFileIndices[8];          /* 0x4cf9e0: character file loaded in each slot */
extern int giAppmode;                       /* 0x424744: 0 = normal game, otherwise launched for test play from the editor */

/* ---- "" literals of main.c, engine.c and config.c ----
 * VC6 puts an all-zero literal in its TU's .bss, and in the original those three sit in the middle of
 * the uninitialized data (asm/game_bss.txt).  Until those TUs define their own .bss, pass these
 * instead of "" (same code bytes) - otherwise the literals end up after the table and shift LIBC's
 * .bss and every communal variable by 12 bytes. */
extern char gszEmptyWindowName[4];          /* 0x42477c: main.c: CreateWindowExA window name */
extern char gszEmptyBgFile[4];              /* 0x424784: engine.c: iLoadExternalImage(..., "bg_001_0.bmp", "", 0) */
extern char gszEmptyIniDefault[4];          /* 0x42478c: config.c: GetPrivateProfileStringA default */

/* ---- window / players ---- */
extern HINSTANCE ghInstance;                         /* 0x4701cc: module instance */
extern HWND ghWnd;                                   /* 0x4246f8: the game window */
extern kgt_character_struct gkgtLoadedCharacter[8];  /* 0x4d1d80: character file and battle state per player slot */

#endif

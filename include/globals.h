/*
 * globals.h - game-wide variables.
 *
 * Uninitialized variables are parts of gBss, which reproduces the original memory layout (see
 * game_bss.h): each is declared as `#define name BSS(type, name)` rather than `extern type name;`.
 * Initialized ones are defined in the C files noted below.
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#include "game_bss.h"

/* ---- netplay (online.c) ---- */
#define gpDirectPlay BSS(LPDIRECTPLAY2A, gpDirectPlay)  /* 0x4246e0: DirectPlay interface */
#define gpAppGuid BSS(LPGUID, gpAppGuid)           /* 0x4246e4: the game's DirectPlay application GUID */
#define giNumServiceProviders BSS(int, giNumServiceProviders)  /* 0x4246e8: entries in gServiceProviderGuids / gszServiceProviderNames */
#define giNumSessionsFound BSS(int, giNumSessionsFound)  /* 0x4246ec: entries in gSessionGuids / gszSessionNames */
#define gdpidLocalPlayer BSS(DPID, gdpidLocalPlayer)  /* 0x4213c8: DirectPlay id of the local player */
#define gdpidHost BSS(DPID, gdpidHost)             /* 0x4280d4: DirectPlay id of the session host */
#define gSessionGuids BSS(GUID[20], gSessionGuids)  /* 0x4213d0: sessions found by iOnlineEnumSessions */
#define gServiceProviderGuids BSS(GUID[20], gServiceProviderGuids)  /* 0x421510: DirectPlay service providers */
#define gszServiceProviderNames BSS(char[20][256], gszServiceProviderNames)  /* 0x421a88: their names */
#define gszSessionNames BSS(char[20][256], gszSessionNames)  /* 0x422e88: names of the sessions found */
#define gDpName BSS(DPNAME, gDpName)               /* 0x424288: DirectPlay name of the local player */
#define gszPlayerName BSS(char[32], gszPlayerName)  /* 0x46ffc0: player name (ini PlayerName) */
#define gszSessionName BSS(char[32], gszSessionName)  /* 0x46ffe0: session name (ini SessionName); used as char[32] */
#define guLocalIpAddr BSS(u_long, guLocalIpAddr)   /* 0x4280d0: local IP address (network order) */
#define gpLocalHostEnt BSS(struct hostent *, gpLocalHostEnt)  /* 0x445720: gethostbyname result for the local host */
#define gszLocalHostName BSS(char[256], gszLocalHostName)  /* 0x447de0: local host name */
extern int giLocalOnlineSlot;                  /* 0x41e3f8: gkgtLoadedCharacter slot of the local player, -1 = none */
#define giInputBuffer BSS(DWORD[8][1024], giInputBuffer)  /* 0x4280e0: input history per player, ring of 1024 frames (giInputBufferPos); bits: 1 back (left outside fights), 2 forward (right outside fights), 4 up, 8 down, 0x10-0x200 buttons A-F, 0x400 pause */

/* ---- sound ---- */
#define gpDirectSound BSS(LPDIRECTSOUND, gpDirectSound)  /* 0x4280cc: DirectSound interface */
#define giDsoundInitializedFlag BSS(int, giDsoundInitializedFlag)  /* 0x42474c: DirectSound was set up */
#define gpWavs BSS(kgtWav *[11][256], gpWavs)      /* 0x430640: [iWavBank][sound index]; banks: 0 system, 1 demo, 2 stage, 3+ players */

/* ---- KGT data files ---- */
#define gkgtKgtSystem BSS(kgtSystem, gkgtKgtSystem)  /* 0x433240: the system file */
#define gkgtLoadedDemo BSS(kgt_demo_file, gkgtLoadedDemo)  /* 0x425a60: the demo file being played */
#define gkgtLoadedStage BSS(kgt_stage, gkgtLoadedStage)  /* 0x445740: the stage file in use (packed; its runtime effects follow at + 0x263d) */
#define giPlayerFileIndices BSS(int[8], giPlayerFileIndices)  /* 0x4cf9e0: character file loaded in each slot */
#define giAppmode BSS(int, giAppmode)              /* 0x424744: 0 = normal game, otherwise launched for test play from the editor */

/* ---- window / players ---- */
#define ghInstance BSS(HINSTANCE, ghInstance)      /* 0x4701cc: module instance */
#define ghWnd BSS(HWND, ghWnd)                     /* 0x4246f8: the game window */
#define gkgtLoadedCharacter BSS(kgt_character_struct[8], gkgtLoadedCharacter)  /* 0x4d1d80: character file and battle state per player slot */

#endif

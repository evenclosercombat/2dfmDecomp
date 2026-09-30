/*
 * kgt.h - common header for the KGT2nd_GAME.exe sources: every translation unit includes it.
 *
 * The original was built with Visual C++ 6.0 (cl 12.00.8168) /Ox and its own Win32/DirectX headers
 * (the main branch reproduces that build); this branch is built with clang and the mingw-w64
 * headers, for x86-64 or i686, see README.md.
 *
 * The C is the main branch's, so it still contains the constructions that steered VC6 to the
 * original's machine code (marked "matching:" and explained where they appear: empty boundaryN
 * labels, iStackPadN locals, dead stores, reads through pointers, value-neutral casts).  They are
 * kept so that the branches stay comparable; for clang they are plain, harmless C.
 */
#ifndef KGT_H
#define KGT_H

#include <windows.h>    /* Win32 (also winsock.h, used by online.c) */
#include <mmsystem.h>   /* winmm: MCI (CD audio, MIDI), joystick, timeGetTime */
#include <stdio.h>      /* sprintf */
#include <stdlib.h>     /* abs, rand */
#include <ddraw.h>      /* DirectDraw: the full-screen surface */
#include <dsound.h>     /* DirectSound: WAV sounds (dsutil.c) */
#include <dplay.h>      /* DirectPlay: netplay (online.c) */

#include "kgt_types.h"  /* the game's structures (KGT files, engine objects, characters, ...) */
#include "globals.h"    /* variables shared by several files */
#include "protos.h"     /* functions called across files */

#endif

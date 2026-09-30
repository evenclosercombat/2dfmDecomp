/*
 * main.c - the program's frame (0x403300-0x406450 in the executable): the memory and sound helpers,
 * the loaders of the KGT data files (system .kgt, .player, .demo, .stage) and of external .bmp images,
 * DirectDraw / window mode switching, the per-tick object processing and the drawing of a frame
 * (including the test play overlays), the game window, WinMain and the main loop.
 *
 * Conventions used throughout:
 *   * the game draws into gpFrameBits, a 640x480 16-bit DIB section (RGB555 in a window; in full screen
 *     the drawing code writes RGB565 and the frame is copied to a DirectDraw back buffer and flipped);
 *   * every KGT file is a 16-byte signature, the part common to all files (kgt_core, bReadKgtCore) and a
 *     type-specific tail that is read straight into the structure;
 *   * in test play (giAppmode != 0, the game started by the editor) every loader first tries the
 *     editor's temporary copy "<file>.t";
 *   * sounds of the loaded files are grouped in banks (kgt_core.iWavBank, the row of gpWavs): 0 system,
 *     1 demo, 2 stage, 3-10 the eight player slots.
 *
 * The second half (from the declarations after iOpenStageFile) was a separate file (main_b.c) appended
 * to this one; it repeats the extern declarations it needs.
 */
#include "kgt.h"

char gszGameWindow[] = "KGT2KGAME";  /* 0x41e7bc: window class and title */
int giUnk41e7c8 = -1;  /* 0x41e7c8: not referenced by the code */
int giPixelFormat555[6] = { 16, 16, 16, 5, 4, 3 };  /* 0x41e7cc: pixel format description {16, 16, 16, 5, 4, 3}; not referenced */
int giPixelFormat565[6] = { 16, 16, 16, 5, 4, 3 };  /* 0x41e7e4: {16, 16, 16, 5, 4, 3}; vHandleDrawing only animates [0..2] between 8 and 0x600 with the speeds in [3..5]; nothing reads it */

/* ------------------------------------------------------------------------------------------ */
/* memory / sound                                                                              */
/* ------------------------------------------------------------------------------------------ */

/*
 * Clears iSize bytes at pAddress, one byte at a time (the game's memset(p, 0, n)).
 * A size of 0 is treated as a programming error: it only shows a warning box.
 * Parameters: pAddress - memory to clear; iSize - number of bytes (> 0).
 * Globals: none.
 */
void vMemzero(void *pAddress, int iSize)
{
    char *pByte;

    /* a zero size is reported ("memzero : size zero, you fool") and nothing is done */
    if (iSize == 0) {
        vSpawnTaskModalWithWarning("memzero : \221\345\202\253\202\263\202O\202\276\202\274\202\261\202\347");  /* memzero : 大きさ０だぞこら */
        return;
    }
    pByte = (char *)pAddress;
    /* matching: this loop form gives the original's plain byte loop (no rep stos) */
    while (iSize--)
        *pByte++ = 0;
}

/*
 * Creates the DirectSound object of the default sound device and sets the normal cooperative level
 * for the game window.  If creating it fails, a warning box is shown and the game runs without sound;
 * if only SetCooperativeLevel fails, the object is released again but giDsoundInitializedFlag stays 1.
 * Globals: changes gpWavs (cleared), gpDirectSound, giDsoundInitializedFlag; reads ghWnd.
 */
void vSetupDsound(void)
{
    /* no wave objects yet (11 banks of 256) */
    vMemzero(gpWavs, sizeof(gpWavs));
    /* NULL = the default device */
    if (SUCCEEDED(DirectSoundCreate(NULL, &gpDirectSound, NULL))) {
        giDsoundInitializedFlag = 1;
        /* DSSCL_NORMAL: shared device, primary buffer format left as it is */
        if (FAILED(IDirectSound_SetCooperativeLevel(gpDirectSound, ghWnd, DSSCL_NORMAL))) {
            IDirectSound_Release(gpDirectSound);
            gpDirectSound = NULL;
        }
    } else {
        vSpawnTaskModalWithWarning("DirectSound\202\314\217\211\212\372\211\273\202\311\216\270\224s\202\265\202\334\202\265\202\275");  /* DirectSoundの初期化に失敗しました */
    }
}

/*
 * Releases the DirectSound object at shutdown (the wave objects must be gone already).
 * Globals: changes gpDirectSound, giDsoundInitializedFlag.
 */
void vReleaseDsound(void)
{
    if (giDsoundInitializedFlag && gpDirectSound) {
        IDirectSound_Release(gpDirectSound);
        gpDirectSound = NULL;
        giDsoundInitializedFlag = 0;
    }
}

/*
 * Stops every wave object of all 11 banks (gpWavs[bank][sound]) and rewinds it; empty entries are
 * passed on as NULL (iStopAndResetWav ignores them).
 * Globals: reads giDsoundInitializedFlag, gpWavs.
 */
void vStopAndResetAllWavs(void)
{
    int iBank, iSound;

    if (giDsoundInitializedFlag) {
        for (iBank = 0; iBank < 11; iBank++)
            for (iSound = 0; iSound < 256; iSound++)
                iStopAndResetWav(gpWavs[iBank][iSound]);
    }
}

/*
 * Stops the MIDI music and closes the MCI sequencer (a wrapper of vStopAndCloseMidi, midi.c).
 * Globals: through the callee, gbMidiOpen and guMidiDeviceId.
 */
void vStopMidi(void)
{
    vStopAndCloseMidi();
}

/*
 * Stops the CD audio (a wrapper of vStopCdAudio1, cdaudio.c).
 * Globals: through the callee, the CD audio state.
 */
void vStopCdAudio2(void)
{
    vStopCdAudio1();
}

/*
 * Plays one sound of a loaded KGT file (sound script commands, BGM selections).  Despite the Ghidra
 * name nothing is loaded here; the kind is the low nibble of pSound->cFlags:
 *   0 stop all sound, 1 wave (restarted from the beginning), 2 MIDI (written to a file and played
 *   through MCI), 3 CD audio track pSound->cCdTrack.  Flag 0x10 loops waves and CD tracks.
 * Parameters: pSound - the sound (kgt_core.pkgtSounds entry).
 * Globals: reads giDsoundInitializedFlag.
 */
void vHandleLoadingSound(kgtSound *pSound)
{
    LPDIRECTSOUNDBUFFER pBuffer;

    switch (pSound->cFlags & 0xf) {
    case 0:
        vHandleStoppingAllWavs(0);
        break;
    case 1:
        if (giDsoundInitializedFlag) {
            /* a wave: rewind, then play from the start (pWav was built by bReadKgtCore) */
            iStopAndResetWav(pSound->pWav);
            pBuffer = kgtdxReturnSoundBuffer(pSound->pWav);
            if (pBuffer) {
                if (pSound->cFlags & 0x10)
                    IDirectSoundBuffer_Play(pBuffer, 0, 0, DSBPLAY_LOOPING);
                else
                    IDirectSoundBuffer_Play(pBuffer, 0, 0, 0);
            }
        }
        break;
    case 2:
        vWriteAndPlayMidFile(pSound);
        break;
    case 3:
        {
            int bLoop = 0;  /* matching: the explicit 0/1 flag avoids a shr/and sequence (vc6-matching-notes/matching-techniques.md) */
            if (pSound->cFlags & 0x10)
                bLoop = 1;
            vLoadsoundFromDisc(pSound->cCdTrack, bLoop);
        }
        break;
    }
}

/*
 * Stops sound output by kind: 0 everything (waves, MIDI, CD audio), 1 all waves (only when
 * DirectSound is up), 2 the MIDI music, 3 the CD audio.
 * Parameters: iSoundKind - 0-3 as above; other values do nothing.
 * Globals: reads giDsoundInitializedFlag.
 */
void vHandleStoppingAllWavs(int iSoundKind)
{
    switch (iSoundKind) {
    case 0:
        vStopAndResetAllWavs();
        vStopMidi();
        vStopCdAudio2();
        break;
    case 1:
        if (giDsoundInitializedFlag)
            vStopAndResetAllWavs();
        break;
    case 2:
        vStopMidi();
        break;
    case 3:
        vStopCdAudio2();
        break;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* KGT files                                                                                   */
/* ------------------------------------------------------------------------------------------ */

/*
 * Frees what bReadKgtCore allocated for a loaded KGT file: the wave objects of its bank in gpWavs
 * (and any sound data still held, i.e. of non-wave sounds), the image data, the skill and script-step
 * tables and the image and sound tables.  Does nothing if the file was never loaded completely
 * (bLoaded 0); the structure itself is not cleared (the iClear*File callers zero it afterwards).
 * Parameters: pCore - the common part of the file's structure.
 * Globals: reads giDsoundInitializedFlag; changes gpWavs[pCore->iWavBank].
 */
void vFreeKgtCore(kgt_core *pCore)
{
    int i;

    if (pCore->bLoaded) {
        /* sounds (only freed when DirectSound is up: without it their data stays allocated) */
        if (giDsoundInitializedFlag && pCore->iSoundsCount > 0) {
            for (i = 0; i < pCore->iSoundsCount; i++) {
                if (gpWavs[pCore->iWavBank][i])
                    vFreeKgtWav(gpWavs[pCore->iWavBank][i]);
                gpWavs[pCore->iWavBank][i] = NULL;
                if (pCore->pkgtSounds[i].pAlloc)
                    GlobalFree(pCore->pkgtSounds[i].pAlloc);
            }
        }
        /* image data, then the tables */
        for (i = 0; i < pCore->iImagesCount; i++) {
            if (pCore->pImageHeaders[i].pAlloc)
                GlobalFree(pCore->pImageHeaders[i].pAlloc);
        }
        if (pCore->pSkillsAlloc)
            GlobalFree(pCore->pSkillsAlloc);
        if (pCore->pSkillScriptsAlloc)
            GlobalFree(pCore->pSkillScriptsAlloc);
        if (pCore->pImageHeaders)
            GlobalFree(pCore->pImageHeaders);
        if (pCore->pkgtSounds)
            GlobalFree(pCore->pkgtSounds);
    }
}

/*
 * Reads the part common to all KGT files from hFile (positioned after the 16-byte signature) into
 * pCore.  File layout:
 *   char[256]   name (game title / character / demo / stage name)
 *   int n       skills (0-1024), then n kgtSkillHeader (0x27 bytes each)
 *   int n       script steps (0-0x10000), then n kgtSkill (16 bytes each)
 *   int n       images (0-0x2000), then per image a kgtImageHeader (0x14 bytes; its pAlloc field is
 *               meaningless in the file) followed by its data: iSize bytes when iSize is not 0, else
 *               iWidth * iHeight bytes, plus 0x400 when iFlags bit 0 is set (the image's own palette,
 *               256 x 4 bytes, stored before the pixels)
 *   0x2100      palettes: 8 x 0x108 colours of 4 bytes (B, G, R, 1)
 *   int n       sounds (0-256), then per sound a kgtSound (0x2a bytes) followed by iSize bytes of data
 *   DWORD       end of the block (read and dropped)
 * With DirectSound, wave sounds (kind 1) become wave objects at once (kgtwBuildWav), are entered in
 * gpWavs[pCore->iWavBank] and their file data is freed.
 * Returns 0 on success, 1 on a read error or a count out of range (with a warning box for the counts),
 * 2 when a sound's buffer could not be allocated.  On an error what was read so far stays allocated
 * (bLoaded is still 0, so vFreeKgtCore does not free it).
 * Globals: reads giDsoundInitializedFlag, gpDirectSound; changes gpWavs.
 * Not byte-identical: VC6 compiles this C to slightly different register and stack-slot choices.
 * The `main` branch builds this function from the original's machine code instead.
 */
int bReadKgtCore(kgt_core *pCore, HANDLE hFile)
{
    DWORD dwBytesRead;
    int iCount;
    int i;
    DWORD dwSize;
    void *pTable;       /* TODO(match): stack slot order differs from the original */
    kgtImageHeader *pImage;

    kgtSound *pSound;
    int *piFlags, *piWidth;

    dwBytesRead = 0;
    /* the name */
    if (!ReadFile(hFile, pCore->szName, 256, &dwBytesRead, NULL))
        return 1;
    /* skills: count (at most 1024) and headers */
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL))
        return 1;
    if (iCount > 1024 || iCount < 0) {
        vSpawnTaskModalWithWarning("\203A\203N\203V\203\207\203\223\220\224\222l\223\307\215\236\203G\203\211\201[");  /* アクション数値読込エラー */
        return 1;
    }
    pCore->pSkillsAlloc = GlobalAlloc(GMEM_FIXED, iCount * sizeof(kgtSkillHeader));
    if (!ReadFile(hFile, pCore->pSkillsAlloc, iCount * sizeof(kgtSkillHeader), &dwBytesRead, NULL))
        return 1;
    pCore->iActionsCount = iCount;

    /* script steps of all skills: count (at most 0x10000) and steps */
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL))
        return 1;
    if (iCount > 0x10000 || iCount < 0) {
        vSpawnTaskModalWithWarning("\203A\203N\203V\203\207\203\223\203X\203N\203\212\203v\203g\220\224\222l\223\307\215\236\203G\203\211\201[");  /* アクションスクリプト数値読込エラー */
        return 1;
    }
    pCore->pSkillScriptsAlloc = GlobalAlloc(GMEM_FIXED, iCount * sizeof(kgtSkill));
    if (!ReadFile(hFile, pCore->pSkillScriptsAlloc, iCount * sizeof(kgtSkill), &dwBytesRead, NULL))
        return 1;

    /* images: count (at most 0x2000), then header and data of each */
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL))
        return 1;
    if (iCount > 0x2000 || iCount < 0) {
        vSpawnTaskModalWithWarning("\203C\203\201\201[\203W\220\224\222l\223\307\215\236\203G\203\211\201[");  /* イメージ数値読込エラー */
        return 1;
    }
    pTable = GlobalAlloc(GMEM_FIXED, iCount * sizeof(kgtImageHeader));
    pCore->pImageHeaders = pTable;
    pCore->iImagesCount = iCount;
    for (i = 0; i < iCount; i++) {
        pImage = (kgtImageHeader *)pTable + i;
        if (!ReadFile(hFile, pImage, sizeof(kgtImageHeader), &dwBytesRead, NULL))
            return 1;
        /* data size: width * height, + 0x400 for an own palette (256 x 4 bytes); a non-zero iSize (the
           stored size) replaces it.  matching: width and flags are read through pointers so that their
           loads stay in this order around the pAlloc store (vc6-matching-notes/matching-techniques.md) */
        piWidth = &pImage->iWidth;
        dwSize = pImage->iHeight * *piWidth;
        piFlags = &pImage->iFlags;
        pImage->pAlloc = NULL;
        if (*piFlags & 1)
            dwSize += 0x400;
        if (pImage->iSize)
            dwSize = pImage->iSize;
        if (dwSize) {
            pImage->pAlloc = GlobalAlloc(GMEM_FIXED, dwSize);
            if (!ReadFile(hFile, pImage->pAlloc, dwSize, &dwBytesRead, NULL))
                return 1;
        }
    }

    /* the 8 palettes of 0x108 colours (B, G, R, 1), one 0x2100-byte block */
    if (!ReadFile(hFile, pCore->kgtPalettes, 0x2100, &dwBytesRead, NULL))
        return 1;
    /* sounds: count (at most 256), then header and data of each */
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL))
        return 1;
    /* (the message below says palette count; it is the sound count) */
    if (iCount > 256 || iCount < 0) {
        vSpawnTaskModalWithWarning("\203p\203\214\203b\203g\220\224\222l\223\307\215\236\203G\203\211\201[");  /* パレット数値読込エラー */
        return 1;
    }
    pTable = GlobalAlloc(GMEM_FIXED, iCount * sizeof(kgtSound));
    pCore->pkgtSounds = pTable;
    pCore->iSoundsCount = iCount;
    for (i = 0; i < iCount; i++) {
        pSound = (kgtSound *)pTable + i;
        if (!ReadFile(hFile, pSound, sizeof(kgtSound), &dwBytesRead, NULL))
            return 1;
        dwSize = pSound->iSize;
        if (dwSize) {
            pSound->pAlloc = GlobalAlloc(GMEM_FIXED, dwSize);
            /* the allocation failed (GlobalSize(NULL) is 0) */
            if (GlobalSize(pSound->pAlloc) < dwSize)
                return 2;
            if (!ReadFile(hFile, pSound->pAlloc, dwSize, &dwBytesRead, NULL))
                return 1;
            if (giDsoundInitializedFlag) {
                /* a wave: build its DirectSound buffer now and drop the file data (pWav overlays iSize) */
                switch (pSound->cFlags & 0xf) {
                case 1:
                    pSound->pWav = kgtwBuildWav(gpDirectSound, pSound->pAlloc, 1);
                    gpWavs[pCore->iWavBank][i] = pSound->pWav;
                    { void **ppAlloc = &pSound->pAlloc; GlobalFree(*ppAlloc); }  /* matching: freeing through the field's address makes VC6 re-read pAlloc */
                    pSound->pAlloc = NULL;
                    break;
                }
            }
        }
    }
    /* the closing DWORD: 0 = success, 1 = read error */
    { DWORD dwTail; return !ReadFile(hFile, &dwTail, 4, &dwBytesRead, NULL); }
    ReadFile(hFile, &pTable, 4, &dwBytesRead, NULL);  /* matching: unreachable, left from the matching attempts (it takes pTable's address) */
}

/* The loaded part of a character: everything up to the runtime state (see iClearCharacterFile). */
#define KGT_CHARACTER_FILE_SIZE 0xdeed

/*
 * Unloads the character file of player slot iPlayerIdx: frees its KGT data, zeroes the loaded part of
 * gkgtLoadedCharacter[iPlayerIdx] (the first KGT_CHARACTER_FILE_SIZE bytes; the runtime state after it
 * is kept) and marks the slot empty in giPlayerFileIndices.
 * Parameters: iPlayerIdx - player slot 0-7.
 * Returns 0.
 * Globals: changes gkgtLoadedCharacter[iPlayerIdx], giPlayerFileIndices[iPlayerIdx], gpWavs.
 */
int iClearCharacterFile(int iPlayerIdx)
{
    kgt_character_struct *pChar = &gkgtLoadedCharacter[iPlayerIdx];

    vFreeKgtCore(&pChar->kgtCore);
    vMemzero(pChar, KGT_CHARACTER_FILE_SIZE);
    giPlayerFileIndices[iPlayerIdx] = -1;
    return 0;
}

/*
 * Loads character iCharacterIdx of the system file's list into player slot iPlayerIdx, unless the slot
 * holds it already.  The file is "<name>.player" (in test play the editor's "<name>.player.t" first):
 * 16-byte signature (not checked), the common KGT part (bReadKgtCore, wave bank 3 + iPlayerIdx), three
 * counted tables (int count + entries: commands, hit junctions, common images; the counts are not
 * checked against the table sizes) and four fixed-size blocks read straight into the structure.
 * Parameters: iPlayerIdx - player slot 0-7; iCharacterIdx - index into gkgtKgtSystem.szCharacterNames.
 * Returns 0 on success (or when already loaded), -1 after a warning box when the file cannot be opened
 * or read (the file handle is not closed then).
 * Globals: reads giAppmode, gkgtKgtSystem; changes gkgtLoadedCharacter[iPlayerIdx],
 * giPlayerFileIndices[iPlayerIdx], gpWavs; logs to the debug messages.
 */
int iOpenCharacterFile(int iPlayerIdx, int iCharacterIdx)
{
    kgt_character_struct *pChar;
    HANDLE hFile;
    DWORD dwBytesRead;
    int iCount;
    char szFile[256];
    char szMsg[256];

    pChar = &gkgtLoadedCharacter[iPlayerIdx];
    dwBytesRead = 0;
    /* already in this slot */
    if (giPlayerFileIndices[iPlayerIdx] == iCharacterIdx)
        goto done;
    /* free the previous character; the sounds go to bank 3 + slot */
    iClearCharacterFile(iPlayerIdx);
    pChar->kgtCore.iWavBank = iPlayerIdx + 3;
    /* open the file (test play: the editor's .t copy first) */
    if (giAppmode) {
        sprintf(szFile, "%s.player.t", gkgtKgtSystem.szCharacterNames[iCharacterIdx]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            sprintf(szFile, "%s.player", gkgtKgtSystem.szCharacterNames[iCharacterIdx]);
            hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
                goto open_error;
        }
    } else {
        sprintf(szFile, "%s.player", gkgtKgtSystem.szCharacterNames[iCharacterIdx]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            goto open_error;
    }
    /* signature, then the common KGT part */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, pChar, 16, &dwBytesRead, NULL))
        goto read_error;
    if (bReadKgtCore(&pChar->kgtCore, hFile) != 0)
        goto read_error;
    /* the counted tables: commands, hit junctions, common images */
    vMemzero(pChar->kgtCommands, sizeof(pChar->kgtCommands));
    vMemzero(pChar->kgtHitJunctions, sizeof(pChar->kgtHitJunctions));
    vMemzero(pChar->kgtCommonImages, sizeof(pChar->kgtCommonImages));
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, pChar->kgtCommands, iCount * sizeof(pChar->kgtCommands[0]), &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, pChar->kgtHitJunctions, iCount * sizeof(pChar->kgtHitJunctions[0]), &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, &iCount, 4, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, pChar->kgtCommonImages, iCount * sizeof(pChar->kgtCommonImages[0]), &dwBytesRead, NULL)) goto read_error;
    /* fixed blocks: 0x223c-0x4da2, 0x757a-0x7cc5 (the standard skill numbers and the character
       settings), the story section 0x7cc5-0xcd41 and 0xcd41-0xdeed */
    if (!ReadFile(hFile, pChar->pad_223c, 0x2b66, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, &pChar->shSkillIdxStanding, 0x74b, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, pChar->cStorySectionStart, 0x507c, &dwBytesRead, NULL)) goto read_error;
    if (!ReadFile(hFile, &pChar->cSectionI, 0x11ac, &dwBytesRead, NULL)) goto read_error;
    /* loaded: log it and remember which character the slot holds */
    CloseHandle(hFile);
    pChar->kgtCore.bLoaded = 1;
    sprintf(szMsg, "\203L\203\203\203\211\203N\203^\201[\203t\203@\203C\203\213\223\307\202\335\215\236\202\335[%s]", szFile);  /* キャラクターファイル読み込み[%s] */
    iSetDebugInfo(szMsg, 0xdfffff);
    giPlayerFileIndices[iPlayerIdx] = iCharacterIdx;
done:
    return 0;

/* error exits: a message with the file name in a warning box, then -1 */
open_error:
    sprintf(szMsg, "Player Open error[%s]", szFile);
    goto error;
read_error:
    sprintf(szMsg, "Player Read error[%s]", szFile);
    goto error;
    sprintf(szMsg, "Player Memory error[%s]", szFile);      /* unreachable; its literal is still emitted, as in the original */
error:
    vSpawnTaskModalWithWarning(szMsg);
    return -1;
}

/*
 * Unloads the system file: frees its KGT data and zeroes gkgtKgtSystem.
 * Returns 0.
 * Globals: changes gkgtKgtSystem, gpWavs[0].
 */
int iClearKgtSystemFile(void)
{
    vFreeKgtCore(&gkgtKgtSystem.kgtCore);
    vMemzero(&gkgtKgtSystem, sizeof(gkgtKgtSystem));
    return 0;
}

/*
 * Loads the game's system file szFile (the .kgt file named in the ini) into gkgtKgtSystem: 16-byte
 * signature, the common KGT part (wave bank 0), then 0x1023c bytes of fixed data from szCharacterNames
 * on (the character, stage and demo file lists, hit junction types and system settings).
 * In test play the editor's "<szFile>.t" is used instead when it exists, and deleted after loading.
 * Outside test play a missing file starts the editor (KGT2nd_EDITOR.exe) and quits the game.
 * The window title becomes the game name (default: the literal below, "2D Kakutou Tsukuru 2nd."),
 * with " -Test Play-" appended in test play.
 * Parameters: szFile - file name (relative to the current directory).
 * Returns 0 on success, -1 on an error (after a warning box, or after starting the editor).
 * Globals: reads giAppmode, ghWnd; changes gkgtKgtSystem, gpWavs[0]; logs to the debug messages.
 */
int iOpenKgtSystemFile(char *szFile)
{
    HANDLE hFile;
    DWORD dwBytesRead;
    int bTestFile;
    char szMsg[256];
    char szTestFile[256];
    char szTitle[512];

    dwBytesRead = 0;
    bTestFile = 0;
    iClearKgtSystemFile();
    gkgtKgtSystem.kgtCore.iWavBank = 0;
    /* open the file (test play: the editor's .t copy first) */
    if (giAppmode) {
        sprintf(szTestFile, "%s.t", szFile);
        hFile = CreateFileA(szTestFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
                goto open_error;
        } else {
            bTestFile = 1;
        }
    } else {
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            /* no game data: start the editor instead and quit */
            WinExec("KGT2nd_EDITOR.exe", SW_SHOW);
            PostQuitMessage(0);
            return -1;
        }
    }
    /* signature, the common KGT part, then the fixed system data */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, &gkgtKgtSystem, 16, &dwBytesRead, NULL))
        goto read_error;
    if (bReadKgtCore(&gkgtKgtSystem.kgtCore, hFile) != 0)
        goto read_error;
    if (!ReadFile(hFile, gkgtKgtSystem.szCharacterNames, 0x1023c, &dwBytesRead, NULL))
        goto read_error;
    /* loaded: window title, log, and the .t copy is used only once */
    CloseHandle(hFile);
    gkgtKgtSystem.kgtCore.bLoaded = 1;
    if (gkgtKgtSystem.kgtCore.szName[0] == '\0')
        sprintf(szTitle, "\202Q\202c\212i\223\254\203c\203N\201[\203\213\202Qnd.");  /* ２Ｄ格闘ツクール２nd. */
    else
        sprintf(szTitle, "%s", gkgtKgtSystem.kgtCore.szName);
    if (giAppmode)
        strcat(szTitle, " -Test Play-");   /* the original: sprintf(szTitle, "%s -Test Play-\0\0", szTitle) (literal with 4 extra zero bytes), printing onto its own argument - undefined behaviour in C, so appended here */
    SetWindowTextA(ghWnd, szTitle);
    sprintf(szMsg, "\203Q\201[\203\200\203V\203X\203e\203\200\203t\203@\203C\203\213\223\307\202\335\215\236\202\335[%s]", szFile);  /* ゲームシステムファイル読み込み[%s] */
    iSetDebugInfo(szMsg, 0xdfffff);
    if (bTestFile)
        DeleteFileA(szTestFile);
    return 0;

/* error exits: a message with the file name in a warning box, then -1 */
open_error:
    sprintf(szMsg, "GameSystem Open error[%s]", szFile);
    goto error;
read_error:
    sprintf(szMsg, "GameSystem Read error[%s]", szFile);
    goto error;
    sprintf(szMsg, "GameSystem Memory error[%s]", szFile);      /* unreachable; its literal is still emitted, as in the original */
error:
    vSpawnTaskModalWithWarning(szMsg);
    return -1;
}

/*
 * Unloads the demo file: frees its KGT data and zeroes gkgtLoadedDemo.
 * Returns 0.
 * Globals: changes gkgtLoadedDemo, gpWavs[1].
 */
int iClearDemoFile(void)
{
    vFreeKgtCore(&gkgtLoadedDemo.kgtCore);
    vMemzero(&gkgtLoadedDemo, sizeof(gkgtLoadedDemo));
    return 0;
}

/*
 * Loads demo iDemo of the system file's list into gkgtLoadedDemo: "<name>.demo" (in test play the
 * editor's "<name>.demo.t" first), 16-byte signature, the common KGT part (wave bank 1), then 0x409
 * bytes from cBgmSelection on.
 * Parameters: iDemo - 1-based index into gkgtKgtSystem.szDemoNames; 0 = no demo set (warning box).
 * Returns 0 on success, -1 on an error (after a warning box).
 * Globals: reads giAppmode, gkgtKgtSystem; changes gkgtLoadedDemo, gpWavs[1]; logs to the debug messages.
 */
int iOpenDemoFile(int iDemo)
{
    HANDLE hFile;
    DWORD dwBytesRead;
    char szFile[256];
    char szMsg[256];

    dwBytesRead = 0;
    iClearDemoFile();
    /* 0: "no demo file is set" */
    if (iDemo == 0) {
        vSpawnTaskModalWithWarning("\203f\203\202\203t\203@\203C\203\213\202\252\220\335\222\350\202\263\202\352\202\304\202\242\202\334\202\271\202\361");  /* デモファイルが設定されていません */
        return -1;
    }
    iDemo--;  /* to the 0-based list index */
    gkgtLoadedDemo.kgtCore.iWavBank = 1;
    /* open the file (test play: the editor's .t copy first) */
    if (giAppmode) {
        sprintf(szFile, "%s.demo.t", gkgtKgtSystem.szDemoNames[iDemo]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            sprintf(szFile, "%s.demo", gkgtKgtSystem.szDemoNames[iDemo]);
            hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
                goto open_error;
        }
    } else {
        sprintf(szFile, "%s.demo", gkgtKgtSystem.szDemoNames[iDemo]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            goto open_error;
    }
    /* signature, the common KGT part, then the demo settings */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, &gkgtLoadedDemo, 16, &dwBytesRead, NULL))
        goto read_error;
    if (bReadKgtCore(&gkgtLoadedDemo.kgtCore, hFile) != 0)
        goto read_error;
    if (!ReadFile(hFile, &gkgtLoadedDemo.cBgmSelection, 0x409, &dwBytesRead, NULL))
        goto read_error;
    /* loaded: log it */
    CloseHandle(hFile);
    gkgtLoadedDemo.kgtCore.bLoaded = 1;
    sprintf(szMsg, "\203f\203\202\203t\203@\203C\203\213\223\307\202\335\215\236\202\335[%s]", szFile);  /* デモファイル読み込み[%s] */
    iSetDebugInfo(szMsg, 0xdfffff);
    return 0;

/* error exits: a message with the file name in a warning box, then -1 */
open_error:
    sprintf(szMsg, "GameDemo Open error[%s]", szFile);
    goto error;
read_error:
    sprintf(szMsg, "GameDemo Read error[%s]", szFile);
    goto error;
    sprintf(szMsg, "GameDemo Memory error[%s]", szFile);        /* unreachable; its literal is still emitted, as in the original */
error:
    vSpawnTaskModalWithWarning(szMsg);
    return -1;
}

/*
 * Unloads the stage file: frees its KGT data and zeroes gkgtLoadedStage.
 * Returns 0.
 * Globals: changes gkgtLoadedStage, gpWavs[2].
 */
int iClearStageFile(void)
{
    vFreeKgtCore(&gkgtLoadedStage.kgtCore);
    vMemzero(&gkgtLoadedStage, sizeof(gkgtLoadedStage));
    return 0;
}

/*
 * Loads stage iStage of the system file's list into gkgtLoadedStage: "<name>.stage" (in test play the
 * editor's "<name>.stage.t" first), 16-byte signature, the common KGT part (wave bank 2), then 0x409
 * bytes from cBgmSelection on.
 * Parameters: iStage - 0-based index into gkgtKgtSystem.szStageNames.
 * Returns 0 on success, -1 on an error (after a warning box).
 * Globals: reads giAppmode, gkgtKgtSystem; changes gkgtLoadedStage, gpWavs[2]; logs to the debug messages.
 */
int iOpenStageFile(int iStage)
{
    HANDLE hFile;
    DWORD dwBytesRead;
    char szFile[256];
    char szMsg[256];

    dwBytesRead = 0;
    iClearStageFile();
    gkgtLoadedStage.kgtCore.iWavBank = 2;
    /* open the file (test play: the editor's .t copy first) */
    if (giAppmode) {
        sprintf(szFile, "%s.stage.t", gkgtKgtSystem.szStageNames[iStage]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            sprintf(szFile, "%s.stage", gkgtKgtSystem.szStageNames[iStage]);
            hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
                goto open_error;
        }
    } else {
        sprintf(szFile, "%s.stage", gkgtKgtSystem.szStageNames[iStage]);
        hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            goto open_error;
    }
    /* signature, the common KGT part, then the stage settings */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, &gkgtLoadedStage, 16, &dwBytesRead, NULL))
        goto read_error;
    if (bReadKgtCore(&gkgtLoadedStage.kgtCore, hFile) != 0)
        goto read_error;
    if (!ReadFile(hFile, &gkgtLoadedStage.cBgmSelection, 0x409, &dwBytesRead, NULL))
        goto read_error;
    /* loaded: log it */
    CloseHandle(hFile);
    gkgtLoadedStage.kgtCore.bLoaded = 1;
    sprintf(szMsg, "\203X\203e\201[\203W\203t\203@\203C\203\213\223\307\202\335\215\236\202\335[%s]", szFile);  /* ステージファイル読み込み[%s] */
    iSetDebugInfo(szMsg, 0xdfffff);
    return 0;

/* error exits: a message with the file name in a warning box, then -1 */
open_error:
    sprintf(szMsg, "GameStage Open error[%s]", szFile);
    goto error;
read_error:
    sprintf(szMsg, "GameStage Read error[%s]", szFile);
    goto error;
    sprintf(szMsg, "GameStage Memory error[%s]", szFile);       /* unreachable; its literal is still emitted, as in the original */
error:
    vSpawnTaskModalWithWarning(szMsg);
    return -1;
}

/*
 * Second half (formerly main_b.c, appended to this translation unit):
 * external bitmap loading, DirectDraw setup, object processing, drawing, window and main loop.
 */


/* ---- declarations not (yet) in globals.h / protos.h --------------------------------------- */

/* draw list rebuilt every frame by vProcessEngineObjects: one list per layer (iDepth) */
typedef struct kgtDrawNode {
    kgtEngineObject *pObj;  /* the object */
    struct kgtDrawNode *pNext;  /* next object of the same layer, NULL at the end */
} kgtDrawNode;

typedef struct kgtDrawLayer {
    kgtDrawNode *pHead;  /* first node, valid only when pTail is not NULL */
    kgtDrawNode *pTail;  /* last node; NULL = empty layer */
} kgtDrawLayer;

extern char gszGameWindow[];                       /* 0x41e7bc: window class and title */
extern int giPixelFormat565[6];                    /* 0x41e7e4: {16, 16, 16, 5, 4, 3}; vHandleDrawing only animates [0..2] between 8 and 0x600 with the speeds in [3..5]; nothing reads it */
extern int giFrameMs;                              /* 0x41e2f0: tick period in ms; vGameLoop sets it to 10 at start-up (100 ticks per second) */
extern char gszCdDrive[];                          /* 0x41e408: "X:" drive of the CD */
#define ghStripDc BSS(HDC, ghStripDc)              /* 0x421a78: memory DC of the second DIB */
#define ghStripBitmap BSS(HBITMAP, ghStripBitmap)  /* 0x421a7c: the second DIB section */
#define ghStripOldBitmap BSS(HGDIOBJ, ghStripOldBitmap)  /* 0x421a80: bitmap previously selected into ghStripDc */
#define gpStripBits BSS(void *, gpStripBits)       /* 0x421a84: pixels of the second DIB */
#define gBmiStrip BSS(BITMAPINFO, gBmiStrip)       /* 0x421650: BITMAPINFO of the second DIB (640x16, 16 bit) */
#define gBmiFrame BSS(BITMAPINFO, gBmiFrame)       /* 0x424298: BITMAPINFO of the frame buffer DIB (640x480, 16 bit) */
#define ghFrameDc BSS(HDC, ghFrameDc)              /* 0x4246c0: memory DC of the frame buffer DIB */
#define ghFrameBitmap BSS(HBITMAP, ghFrameBitmap)  /* 0x4246c4: the frame buffer DIB section */
#define ghFrameOldBitmap BSS(HGDIOBJ, ghFrameOldBitmap)  /* 0x4246c8: bitmap previously selected into ghFrameDc */
#define gpFrameBits BSS(void *, gpFrameBits)       /* 0x4246cc: pixels of the frame buffer: 640x480, 16 bit (RGB555 in a window, RGB565 full screen) */
#define giSkipframeCount BSS(int, giSkipframeCount)  /* 0x4246f4: frames run in the last tick (1 + skipped) */
#define giObjectCount BSS(int, giObjectCount)      /* 0x4246fc: objects processed in the last frame (Object: in the status line) */
#define giTimeAdjustmentFlag BSS(int, giTimeAdjustmentFlag)  /* 0x424700: 1 while the frame time of the current tick has been measured */
#define giScreenMode BSS(int, giScreenMode)        /* 0x424704: display mode in use: 0 window (RGB555 DIB), 1 full screen (RGB565 surface) */
#define giHitJudge BSS(int, giHitJudge)            /* 0x42470c: hit-judge display (hit boxes and player state), copy of giConfigTestplayHitjudge; the next int (0x424710) enables the line switch button */
#define giForceRoundEnd BSS(int, giForceRoundEnd)  /* 0x424718: set to end the round at once (debug key); engine.c reads it as (&gbStoryMode)[1] */
#define gbShowDebugStatus BSS(int, gbShowDebugStatus)  /* 0x42471c: draw gDebugStatus at the bottom of the screen; never set by the code */
#define guCdAudioDeviceId BSS(MCIDEVICEID, guCdAudioDeviceId)  /* 0x424720: MCI device of the CD audio */
#define gbCdAudioOpen BSS(int, gbCdAudioOpen)      /* 0x424724: the CD audio device is open */
#define giDiscLoopFlag BSS(int, giDiscLoopFlag)    /* 0x424728: loop the CD track (checked on MM_MCINOTIFY) */
#define giCdAudioError BSS(int, giCdAudioError)    /* 0x42472c: CD audio failed, do not try again */
#define gbMidiOpen BSS(int, gbMidiOpen)            /* 0x424738: the MIDI device is open */
#define guMidiDeviceId BSS(MCIDEVICEID, guMidiDeviceId)  /* 0x42473c: MCI device of the MIDI sequencer */
#define giMidiLoopFlag BSS(int, giMidiLoopFlag)    /* 0x424740: loop the MIDI file */
#define gpDDSPrimary BSS(LPDIRECTDRAWSURFACE, gpDDSPrimary)  /* 0x424750: full screen: primary surface (flipping chain) */
#define gpDDSBack BSS(LPDIRECTDRAWSURFACE, gpDDSBack)  /* 0x424754: full screen: back buffer */
#define gpDirectDraw BSS(LPDIRECTDRAW, gpDirectDraw)  /* 0x424758: DirectDraw interface */
#define gbSaveWindowRect BSS(int, gbSaveWindowRect)  /* 0x42475c: grWindowRect must be saved before going full screen */
#define gbDdrawInitialized BSS(int, gbDdrawInitialized)  /* 0x424760: the DirectDraw objects exist */
#define giModeSwitchFrames BSS(int, giModeSwitchFrames)  /* 0x424768: set to 3 after a display mode switch, counted down in full screen frames */
#define gbSwitchingDisplayMode BSS(int, gbSwitchingDisplayMode)  /* 0x42476c: set while the display mode is being changed */
#define giDisplayModeSwitches BSS(int, giDisplayModeSwitches)  /* 0x424770: number of display mode switches */
#define gbDisplayModeReady BSS(int, gbDisplayModeReady)  /* 0x424774: 0 during a display mode switch, non-zero after */
#define grWindowRect BSS(RECT, grWindowRect)       /* 0x424f40: window rectangle saved for going back to window mode */
#define gkgtBitmaps BSS(kgtBMPINFO[128], gkgtBitmaps)  /* 0x424f60: external bitmaps: [1] text.bmp (fonts, digits), others by kgtEngineObject.iDrawFlag */
#define gdwFrameTimeDiff BSS(DWORD, gdwFrameTimeDiff)  /* 0x425960: measured frame time in ms (FPS = 1000 / it) */
#define giEngineObjectIter BSS(int, giEngineObjectIter)  /* 0x4259a4: index of the object being processed */
#define gpDrawNodeNext BSS(kgtDrawNode *, gpDrawNodeNext)  /* 0x4259a8: next free gkgtDrawNodes entry */
#define grClipRect BSS(RECT, grClipRect)           /* 0x4259e0: 1x1 cursor clip rectangle used in full screen */
#define gpGlobalMemoryAlloc BSS(void *, gpGlobalMemoryAlloc)  /* 0x425a44: decompression buffer */
#define giConfigGameWindowPointX BSS(int, giConfigGameWindowPointX)  /* 0x425a48: window x (ini GameWindowPoint_x) */
#define giConfigGameWindowPointY BSS(int, giConfigGameWindowPointY)  /* 0x425a4c: window y (ini GameWindowPoint_y) */
#define giConfigTestplayGameinfo BSS(int, giConfigTestplayGameinfo)  /* 0x430108: test play: show game information */
#define giConfigTestplayExit BSS(int, giConfigTestplayExit)  /* 0x430118: test play: quit when the window loses focus */
#define gszConfigReturnedFilename BSS(char[MAX_PATH], gszConfigReturnedFilename)  /* 0x43012c: system file name (ini File/Filename) */
#define gkgtDrawLayers BSS(kgtDrawLayer[128], gkgtDrawLayers)  /* 0x430240: draw lists, one per kgtEngineObject.iDepth */
#define giSystemFlashTimeLeft BSS(int, giSystemFlashTimeLeft)  /* 0x4456e4: [0] frames left, [1..4] base red, green, blue, alpha */
#define giReverseShakeDirection BSS(int, giReverseShakeDirection)  /* 0x4456fc: frame counter; its low bit flips the shake direction */
#define giStageFlashTimeLeft BSS(int, giStageFlashTimeLeft)  /* 0x447d91: [0] frames left, [1..4] base red, green, blue, alpha */
#define giShakeXMode BSS(int, giShakeXMode)        /* 0x447da9: screen shake x (script command EB): mode; vCalculateShake takes the 5 ints */
#define giShakeYMode BSS(int, giShakeYMode)        /* 0x447dbd: screen shake y: mode */
#define gdwSystemTime BSS(DWORD, gdwSystemTime)    /* 0x447dd4: timeGetTime of the next frame */
#define giInputBufferPos BSS(int, giInputBufferPos)  /* 0x447ee0: current frame in giInputBuffer (0-1023) */
#define giConfigGameWindowSizeX BSS(int, giConfigGameWindowSizeX)  /* 0x447f20: window client width (ini GameWindowSize_x) */
#define giConfigGameWindowSizeY BSS(int, giConfigGameWindowSizeY)  /* 0x447f24: window client height (ini GameWindowSize_y) */
#define giCameraX BSS(int, giCameraX)              /* 0x447f2c: camera x (pixels) */
#define giCameraY BSS(int, giCameraY)              /* 0x447f30: camera y (pixels) */
#define gDDSurfaceDesc BSS(DDSURFACEDESC, gDDSurfaceDesc)  /* 0x46ff40: surface description used to create and lock the surfaces */
#define gkgtGameState BSS(kgtGameState, gkgtGameState)  /* 0x470020: state of the current game */
#define gkgtEngineObjects BSS(kgtEngineObject[1024], gkgtEngineObjects)  /* 0x4701e0: all engine objects */
#define gpkgtCurrentEngineObject BSS(kgtEngineObject *, gpkgtCurrentEngineObject)  /* 0x4cfa00: object whose handler is running */
#define gkgtDrawNodes BSS(kgtDrawNode[1024], gkgtDrawNodes)  /* 0x4cfa20: nodes of the per-layer draw lists */
#define gszCurrentDirectory BSS(char[256], gszCurrentDirectory)  /* 0x4d1c60: directory of the executable (data files are opened from here) */
#define giConfigGameScreenMode BSS(int, giConfigGameScreenMode)  /* 0x4d1d60: display mode setting (ini GameScreenMode): 0 window, 1 full screen */
#define gpsLpAppName BSS(char *, gpsLpAppName)     /* 0x541f7c: ini section name ("TestPlay" or "GamePlay") */

#include "blit.h"   /* copy_ppvBits_to_lpSurface (blit.c) */
void vEmptyEngineObjects(void);
void vFillRect16(int iX, int iY, int iWidth, int iHeight, int iBlendMode, int iColor);
void vDrawNumberSmall(int iValue, int iX, int iY, int iFlags, int iTintR, int iTintG, int iTintB);
void vDrawTextSmall(char *szText, int iX, int iY, int iFlags, int iTintR, int iTintG, int iTintB);
void vDrawTextMedium(char *szText, int iX, int iY, int iFlags, int iTintR, int iTintG, int iTintB);
void vJumptableJump(void);
void vCalculateShake(int *pShake);
void vDrawCurrentEngineObject(void);
void vHitboxHandling(void);
int iCheckJoystickOne(void);
int iCheckJoystickTwo(void);
void vGetPlayerInputs(void);
void vSaveOnlineNamesToIni(void);
void vLoadKgt2kConfig(void);
void vLoadGameConfig(void);
void vSaveGameConfig(void);
void vMemzeroDebugStructs(void);
void vTickDebugEventTimers(void);
void vDrawDebugInfo(void);
int iOpenAndPlayCdAudio(void);
int iPlayAndCloseMidFile(void);
void vRegisterInputWindowClasses(void);
void vHandleWmCommand(HWND hWnd, int iCommand);    /* (declared with WPARAM wParam in the original; dialogs.c defines it with int) */

int iLoadExternalImage(kgtBMPINFO *pInfo, LPCSTR szResource, LPCSTR szFile, int iUnused);
void vReleaseDdrawInterfaces(void);
void vSetupDdrawPrimarySurface(void);
void vRestoreDdrawInterfaces(void);
void vWindowBltFuncs(void);
void vProcessEngineObjects(void);
void vHandleDrawing(void);
void vCheckWindowBounds(HWND hWnd);
void vInitializeWindowsAndMemory(void);
void vShutdownAndFreeResources(void);
void vGameLoop(void);
void vGetWindowPos(HWND hWnd);
LRESULT CALLBACK iMainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------------------------------ */
/* bitmaps                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* 8-bit red, green and blue to a 16-bit RGB555 pixel (5 bits each, red highest) */
#define RGB555(r, g, b) ((WORD)((((WORD)(r) >> 3) << 10) + (((WORD)(g) >> 3) << 5) + ((WORD)(b) >> 3)))

/*
 * Loads a Windows bitmap for the 16-bit drawing code into pInfo: from the file szFile if it exists,
 * else from the executable's RT_BITMAP resource szResource (on this branch: the bitmap compiled in by
 * resources.c).  4- and 8-bit bitmaps keep their pixel
 * bytes (rows as stored, bottom-up) behind a palette converted to RGB555 (iColorsUsed + 1 WORDs);
 * 24-bit bitmaps become RGB555 pixels.  The width is rounded up to a multiple of 4 pixels (8 for 1 bit).
 * Earlier data in pInfo->pData is freed first.
 * Parameters: pInfo - destination (gkgtBitmaps entry); szResource - resource name used when the file is
 * missing; szFile - file name; iUnused - not used.
 * Returns 0 on success; -1 when the file or resource cannot be read, for 1-bit or other unsupported
 * palette bitmaps and for an empty size (each with a message box); 1 for an unsupported bit depth that
 * got past the palette check (biClrUsed set), without freeing anything.
 */
int iLoadExternalImage(kgtBMPINFO *pInfo, LPCSTR szResource, LPCSTR szFile, int iUnused)
{
    HANDLE hFile;
    DWORD dwFileSize;
    BYTE *pFile;
    DWORD dwBitCount;
    HGLOBAL hMem;
    BITMAPINFOHEADER *pHeader;
    BYTE *pResource;
    int bFromResource;
    DWORD dwWidth;
    DWORD dwBytesRead;
    char szMsg[200];
    BYTE *pBits;
    int iColors;
    DWORD dwHeight;
    DWORD dwDataSize;
    WORD *pDst;
    BYTE *pPixels4;
    BYTE *pPixels8;
    RGBQUAD *pPalette;
    DWORD dwX, dwY;

    hMem = NULL;
    bFromResource = 0;
    pResource = NULL;
    dwBytesRead = 0;
    /* the file first: read it whole into a GlobalAlloc block */
    hFile = CreateFileA(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        dwFileSize = GetFileSize(hFile, NULL);
        hMem = GlobalAlloc(GMEM_FIXED, dwFileSize);
        if (hMem == NULL)
            goto file_error;
        pFile = GlobalLock(hMem);
        if (pFile == NULL)
            goto file_error;
        SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        if (!ReadFile(hFile, pFile, dwFileSize, &dwBytesRead, NULL))
            { GlobalUnlock(hMem); GlobalFree(hMem); return -1; }
        CloseHandle(hFile);
        /* the BITMAPINFOHEADER follows the 14-byte file header; bfOffBits locates the pixels */
        pHeader = (BITMAPINFOHEADER *)(pFile + sizeof(BITMAPFILEHEADER));
        pBits = pFile + ((BITMAPFILEHEADER *)pFile)->bfOffBits;
    } else {
        /* no file: the bitmap resource (a BITMAPINFOHEADER without the file header) */
        pResource = (BYTE *)pLockEmbeddedResource(szResource, RT_BITMAP, NULL);  /* FindResourceA + LoadResource + LockResource in the original */
        if (pResource == NULL)
            goto res_error;
        bFromResource = 1;
        pHeader = (BITMAPINFOHEADER *)pResource;
    }

    /* number of palette colours: biClrUsed, else the default for the bit depth */
    if (pHeader->biClrUsed != 0) {
        iColors = pHeader->biClrUsed;
        goto have_colors;
    }
    switch (pHeader->biBitCount) {
    case 1:
        vSpawnTaskModalWithWarning("load_bmp : \202Q\220F\202\314BMP\212\250\225\331");  /* load_bmp : ２色のBMP勘弁 */
        goto error;
    case 4:
        iColors = 16;
        break;
    case 8:
        iColors = 256;
        break;
    case 24:
        iColors = 0;
        break;
    default:
        vSpawnTaskModalWithWarning("load_bmp : \226\242\203T\201[\203|\201[\203g\203p\203\214\203b\203g\202\314BMP\202\276\202\265\202\346");  /* load_bmp : 未サーポートパレットのBMPだしよ */
        goto error;
    }
have_colors:
    dwBitCount = pHeader->biBitCount;
    /* in a resource the pixels follow the header and the palette */
    if (bFromResource)
        pBits = pResource + pHeader->biSize + iColors * sizeof(RGBQUAD);
    /* iColorsUsed is the highest palette index */
    if (iColors)
        pInfo->iColorsUsed = iColors - 1;
    else
        pInfo->iColorsUsed = 0;
    pInfo->iWidth = pHeader->biWidth;
    pInfo->iHeight = dwHeight = pHeader->biHeight;
    /* size of the converted data: 4 bit two pixels per byte, 8 bit one byte per pixel (each after
       a WORD per palette colour), 24 bit one RGB555 WORD per pixel */
    switch (dwBitCount) {
    case 1:
        pInfo->iWidth = (pInfo->iWidth + 7) & ~7;
        dwDataSize = pInfo->iWidth * dwHeight;
        break;
    case 4:
        pInfo->iWidth = (pInfo->iWidth + 3) & ~3;
        dwDataSize = (pInfo->iWidth * dwHeight >> 1) + iColors * 2;
        break;
    case 8:
        pInfo->iWidth = (pInfo->iWidth + 3) & ~3;
        dwDataSize = pInfo->iWidth * dwHeight + iColors * 2;
        break;
    case 24:
        pInfo->iWidth = (pInfo->iWidth + 3) & ~3;
        dwDataSize = pInfo->iWidth * dwHeight * 2;
        break;
    }
    dwWidth = pInfo->iWidth;
    /* "the BMP size is invalid" */
    if (dwWidth * dwHeight == 0) {
        sprintf(szMsg, "BMP\203T\203C\203Y\202\252\225s\220\263\202\305\202\267 x%d,y%d", (int)dwWidth, (int)dwHeight);  /* BMPサイズが不正です x%d,y%d */
        MessageBoxA(NULL, szMsg, "\230A\227\215", MB_TASKMODAL);  /* 連絡 */
        goto error;
    }
    /* replace any earlier image */
    if (pInfo->pData)
        GlobalFree(pInfo->pData);
    pInfo->pData = GlobalAlloc(GMEM_FIXED, dwDataSize);
    /* convert: palette to RGB555, then copy (4/8 bit) or convert (24 bit) the pixels */
    switch (dwBitCount) {
    case 1:
        break;
    case 4:
        pPixels4 = (BYTE *)((WORD *)pInfo->pData + pInfo->iColorsUsed + 1);
        pDst = (WORD *)pInfo->pData;
        pPalette = (RGBQUAD *)(pHeader + 1);
        for (dwX = 0; (int)dwX < iColors; dwX++)
            *pDst++ = RGB555(pPalette[dwX].rgbRed, pPalette[dwX].rgbGreen, pPalette[dwX].rgbBlue);
        for (dwY = 0; dwY < dwHeight; dwY++)
            for (dwX = 0; dwX < dwWidth >> 1; dwX++)
                *pPixels4++ = *pBits++;
        break;
    case 8:
        /* matching: case 8 has a pixel pointer of its own, which gives the original's stack-slot order */
        pPixels8 = (BYTE *)((WORD *)pInfo->pData + pInfo->iColorsUsed + 1);
        pDst = (WORD *)pInfo->pData;
        pPalette = (RGBQUAD *)(pHeader + 1);
        for (dwX = 0; (int)dwX < iColors; dwX++)
            *pDst++ = RGB555(pPalette[dwX].rgbRed, pPalette[dwX].rgbGreen, pPalette[dwX].rgbBlue);
        for (dwY = 0; dwY < dwHeight; dwY++)
            for (dwX = 0; dwX < dwWidth; dwX++)
                *pPixels8++ = *pBits++;
        break;
    case 24:
        pDst = (WORD *)pInfo->pData;
        for (dwY = 0; dwY < pInfo->iHeight; dwY++) {
            for (dwX = 0; dwX < dwWidth; dwX++) {
                /* B, G, R bytes -> RGB555 */
                *pDst++ = ((((WORD)(pBits[2] >> 3) << 5) + (WORD)(pBits[1] >> 3)) << 5) + (WORD)(pBits[0] >> 3);
                pBits += 3;
            }
        }
        break;
    default:
        /* any other depth (possible only with biClrUsed set): "unsupported bitmap file" */
        sprintf(szMsg, "\203T\203|\201[\203g\212O\202\314\203r\203b\203g\203}\203b\203v\203t\203@\203C\203\213\202\305\202\267\201B(%dbit)", (int)dwBitCount);  /* サポート外のビットマップファイルです。(%dbit) */
        MessageBoxA(NULL, szMsg, "error", MB_TASKMODAL);
        return 1;
    }
    /* the original also called GlobalUnlock(GlobalHandle(p)), GlobalFree(GlobalHandle(p)) and
       FreeResource on the resource pointer (16-bit Windows habits, harmless for its image-mapped
       resource: the calls just fail).  Here that pointer is into the executable's data, and with this
       build the GlobalFree of it ends the process with a heap-corruption error, so only the file
       block is freed. */
    if (!bFromResource) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    /* raw data */
    pInfo->iCompressedSize = 0;
    return 0;

/* error exits (res_error and file_error are also jumped to directly): free and return -1 */
error:
    if (bFromResource) {
res_error:
        return -1;      /* (the resource pointer is not freed, see above) */
    }
file_error:
    GlobalUnlock(hMem);
    GlobalFree(hMem);
    return -1;
}

/* ------------------------------------------------------------------------------------------ */
/* DirectDraw                                                                                  */
/* ------------------------------------------------------------------------------------------ */

/*
 * Leaves the current display mode: saves the window rectangle first if the window mode was active
 * (gbSaveWindowRect), then releases the full-screen DirectDraw objects (back buffer, primary surface,
 * DirectDraw) and shows the cursor again.
 * Globals: changes grWindowRect, gbSaveWindowRect, gpDDSBack, gpDDSPrimary, gpDirectDraw,
 * gbDdrawInitialized; reads ghWnd.
 */
void vReleaseDdrawInterfaces(void)
{
    if (gbSaveWindowRect) {
        GetWindowRect(ghWnd, &grWindowRect);
        gbSaveWindowRect = 0;
    }
    /* full screen was active */
    if (gbDdrawInitialized) {
        if (gpDDSBack) {
            IDirectDrawSurface_Release(gpDDSBack);
            gpDDSBack = NULL;
        }
        if (gpDDSPrimary) {
            IDirectDrawSurface_Release(gpDDSPrimary);
            gpDDSPrimary = NULL;
        }
        if (gpDirectDraw) {
            IDirectDraw_Release(gpDirectDraw);
            gpDirectDraw = NULL;
        }
        ShowCursor(TRUE);
        gbDdrawInitialized = 0;
    }
}

/*
 * Enters the display mode in giScreenMode (F4, Alt+Enter and start-up).
 * Full screen (1): clips the cursor to a 1x1 rectangle and hides it, creates DirectDraw, takes exclusive
 * full-screen control, sets 640x480 with 16 bits per pixel and creates a primary surface with two back
 * buffers (in video memory if possible); any failure falls back to window mode and starts over.
 * Window (0): blackens the client area, frees the cursor and restores the saved window rectangle.
 * Globals: changes giScreenMode, gpDirectDraw, gpDDSPrimary, gpDDSBack, gDDSurfaceDesc, grClipRect,
 * gbDdrawInitialized, gbSaveWindowRect, giDisplayModeSwitches, gbSwitchingDisplayMode,
 * gbDisplayModeReady, giModeSwitchFrames; reads ghWnd, grWindowRect.
 */
void vSetupDdrawPrimarySurface(void)
{
    DDSCAPS ddsCaps;
    HDC hWindowDc;
    HRESULT hrResult;

retry:
    /* leave the current mode */
    giDisplayModeSwitches++;
    gbSwitchingDisplayMode = 1;
    vReleaseDdrawInterfaces();
    gbDisplayModeReady = 0;
    if (giScreenMode) {
        /* full screen: the hidden cursor is kept in a 1x1 rectangle */
        grClipRect.left = 100;
        grClipRect.right = 101;
        grClipRect.top = 100;
        grClipRect.bottom = 101;
        ClipCursor(&grClipRect);
        ShowCursor(FALSE);
        /* DirectDraw of the primary display driver; every failure below falls back to the window */
        if (DirectDrawCreate(NULL, &gpDirectDraw, NULL) != DD_OK) {
            giScreenMode = 0;
            goto retry;
        }
        if (IDirectDraw_SetCooperativeLevel(gpDirectDraw, ghWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWMODEX) != DD_OK) {
            giScreenMode = 0;
            goto retry;
        }
        IDirectDraw_SetDisplayMode(gpDirectDraw, 640, 480, 16);  /* result not checked */
        /* a flipping chain: primary surface + 2 back buffers, first in video memory, then anywhere */
        memset(&gDDSurfaceDesc, 0, sizeof(gDDSurfaceDesc));
        gDDSurfaceDesc.dwSize = sizeof(gDDSurfaceDesc);
        gDDSurfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        gDDSurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_VIDEOMEMORY;
        gDDSurfaceDesc.dwBackBufferCount = 2;
        hrResult = IDirectDraw_CreateSurface(gpDirectDraw, &gDDSurfaceDesc, &gpDDSPrimary, NULL);
        if (hrResult != DD_OK) {
            gDDSurfaceDesc.ddsCaps.dwCaps &= ~DDSCAPS_VIDEOMEMORY;
            hrResult = IDirectDraw_CreateSurface(gpDirectDraw, &gDDSurfaceDesc, &gpDDSPrimary, NULL);
        }
        if (hrResult != DD_OK) {
            giScreenMode = 0;
            goto retry;
        }
        /* the back buffer the frames are copied to; failing here quits the game */
        ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;
        if (IDirectDrawSurface_GetAttachedSurface(gpDDSPrimary, &ddsCaps, &gpDDSBack) != DD_OK) {
            PostQuitMessage(0);
            return;
        }
        gbDdrawInitialized = 1;
    } else {
        /* window mode: blacken the window, free the cursor, restore the window rectangle */
        hWindowDc = GetDC(ghWnd);
        PatBlt(hWindowDc, 0, 0, 640, 480, BLACKNESS);
        ReleaseDC(ghWnd, hWindowDc);
        ClipCursor(NULL);
        MoveWindow(ghWnd, grWindowRect.left, grWindowRect.top, grWindowRect.right - grWindowRect.left, grWindowRect.bottom - grWindowRect.top, TRUE);
        gbSaveWindowRect = 1;
    }
    /* done */
    giModeSwitchFrames = 3;
    UpdateWindow(ghWnd);
    gbSwitchingDisplayMode = 0;
    gbDisplayModeReady++;
}

/*
 * Restores the full-screen surfaces after they were lost (the game was switched away from and
 * back to); called on WM_ACTIVATEAPP.
 * Globals: reads gpDDSPrimary, gpDDSBack.
 */
void vRestoreDdrawInterfaces(void)
{
    if (IDirectDrawSurface_IsLost(gpDDSPrimary))
        IDirectDrawSurface_Restore(gpDDSPrimary);
    if (IDirectDrawSurface_IsLost(gpDDSBack))
        IDirectDrawSurface_Restore(gpDDSBack);
}

/*
 * Window mode: copies the 640x480 frame buffer DIB (ghFrameDc) to the window's client area, 1:1 when
 * the client is 640x480, else stretched (COLORONCOLOR: rows and columns dropped or repeated, no
 * blending).  Does nothing in full screen or while the display mode changes.
 * Globals: reads gbSwitchingDisplayMode, giScreenMode, ghWnd, ghFrameDc, giConfigGameWindowSizeX/Y.
 */
void vWindowBltFuncs(void)
{
    HDC hWindowDc;

    if (gbSwitchingDisplayMode == 0 && giScreenMode == 0) {
        hWindowDc = GetDC(ghWnd);
        SetStretchBltMode(hWindowDc, COLORONCOLOR);
        if (giConfigGameWindowSizeX == 640 && giConfigGameWindowSizeY == 480)
            BitBlt(hWindowDc, 0, 0, 640, 480, ghFrameDc, 0, 0, SRCCOPY);
        else
            StretchBlt(hWindowDc, 0, 0, giConfigGameWindowSizeX, giConfigGameWindowSizeY, ghFrameDc, 0, 0, 640, 480, SRCCOPY);
        ReleaseDC(ghWnd, hWindowDc);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* frame                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/*
 * One game tick.  Sorts every active engine object into the draw list of its layer (iDepth, 0-127),
 * in slot order, then walks the layers from 0 to 127 and runs each object's handler (vJumptableJump).
 * While a handler runs, gpDrawNodeNext is its node, and the walk continues after the node it points to
 * afterwards.  Then the hit processing (vHitboxHandling) and the countdown of the colour flashes of the
 * characters, the system and the stage.
 * Globals: changes giObjectCount, gkgtDrawLayers, gkgtDrawNodes, gpDrawNodeNext, giEngineObjectIter,
 * gpkgtCurrentEngineObject, gkgtLoadedCharacter[].iFlashTimeLeft, giSystemFlashTimeLeft,
 * giStageFlashTimeLeft; everything the handlers change.
 */
void vProcessEngineObjects(void)
{
    kgtDrawNode *pNode;
    int i;

    giObjectCount = 0;
    vTickDebugEventTimers();  /* count down the debug message timers */
    /* empty the 128 layer lists; nodes are handed out from gkgtDrawNodes in order */
    for (i = 0; i < 128; i++)
        gkgtDrawLayers[i].pTail = NULL;
    gpDrawNodeNext = gkgtDrawNodes;
    gpkgtCurrentEngineObject = gkgtEngineObjects;
    for (giEngineObjectIter = 0; giEngineObjectIter < 1024; giEngineObjectIter++, gpkgtCurrentEngineObject++) {
        if (gpkgtCurrentEngineObject->iJumpIdx != EMPTY) {
            /* append the object to its layer's list.  matching: the node stores are written out in both
               arms; VC6 merges them again, but the extra uses give the original's registers (vc6-matching-notes/matching-techniques.md) */
            if (gkgtDrawLayers[gpkgtCurrentEngineObject->iDepth].pTail == NULL) {
                gkgtDrawLayers[gpkgtCurrentEngineObject->iDepth].pHead = gpDrawNodeNext;
                gpDrawNodeNext->pObj = gpkgtCurrentEngineObject;
                gkgtDrawLayers[gpkgtCurrentEngineObject->iDepth].pTail = gpDrawNodeNext;
                gpDrawNodeNext->pNext = NULL;
            } else {
                gkgtDrawLayers[gpkgtCurrentEngineObject->iDepth].pTail->pNext = gpDrawNodeNext;
                gpDrawNodeNext->pObj = gpkgtCurrentEngineObject;
                gkgtDrawLayers[gpkgtCurrentEngineObject->iDepth].pTail = gpDrawNodeNext;
                gpDrawNodeNext->pNext = NULL;
            }
            gpDrawNodeNext++;
        }
    }
    /* run the handlers, layer by layer */
    for (i = 0; i < 128; i++) {
        if (gkgtDrawLayers[i].pTail) {
            pNode = gkgtDrawLayers[i].pHead;
            do {
                gpkgtCurrentEngineObject = pNode->pObj;
                gpDrawNodeNext = pNode;
                if (gpkgtCurrentEngineObject->iJumpIdx != EMPTY) {
                    giObjectCount++;
                    vJumptableJump();
                    pNode = gpDrawNodeNext;
                }
                pNode = pNode->pNext;
            } while (pNode);
        }
    }
    /* hits, clashes and pushing (battle.c) */
    vHitboxHandling();
    /* count down the colour flashes */
    for (i = 0; i < 8; i++) {
        if (gkgtLoadedCharacter[i].iFlashTimeLeft)
            gkgtLoadedCharacter[i].iFlashTimeLeft--;
    }
    if (giSystemFlashTimeLeft)
        giSystemFlashTimeLeft--;
    if (giStageFlashTimeLeft)
        giStageFlashTimeLeft--;
}

/*
 * Draws one frame into the frame buffer and shows it.  Advances the screen shake, draws the active
 * objects layer by layer (layer 0 first, i.e. at the back), then the overlays: in a story mode battle
 * small life bars over the CPU opponents; with the hit-judge display (test play, F1) the team rosters
 * and a status line per player (life and damage bars, skill and step, cancel state, stocks, wins,
 * losses, win points, used M-number slots, and the skill name in a GDI font); with the status display
 * (F3) FPS, skipped frames and object count and the debug messages.  Finally the frame is copied to
 * the back buffer and flipped (full screen) or blitted to the window.
 * Globals: reads the draw lists, gkgtGameState, gkgtLoadedCharacter, gkgtKgtSystem, the camera, the
 * timing counters and the surfaces; changes giShakeXMode/giShakeYMode (shake state),
 * giPixelFormat565 (see below), giReverseShakeDirection, giModeSwitchFrames, gDDSurfaceDesc,
 * gpkgtCurrentEngineObject, gpDrawNodeNext.
 */
void vHandleDrawing(void)
{
    kgtDrawNode *pNode;
    kgt_character_struct *pChar;
    kgtEngineObject *pObj;
    kgtEngineObject **ppMNumberObj;
    HDC hFrameDc;
    HFONT hFont;
    HGDIOBJ hOldFont;
    int i, j, iBarX, iBarY, iLifeWidth;
    char *pSlotText;
    char szSlotTexts[10][2];
    char szBuf[256];
    char szLine[256];

    /* advance the screen shake in x and y */
    vCalculateShake(&giShakeXMode);
    vCalculateShake(&giShakeYMode);
    /* the objects, back layer first, in the order vProcessEngineObjects ran them */
    for (i = 0; i < 128; i++) {
        if (gkgtDrawLayers[i].pTail) {
            pNode = gkgtDrawLayers[i].pHead;
            do {
                gpkgtCurrentEngineObject = pNode->pObj;
                gpDrawNodeNext = pNode;
                if (gpkgtCurrentEngineObject->iJumpIdx != EMPTY) {
                    vDrawCurrentEngineObject();
                    pNode = gpDrawNodeNext;
                }
                pNode = pNode->pNext;
            } while (pNode);
        }
    }

    /* story mode battle (game states 3000-3999): small life bars over the CPU opponents (slots 1-7)
   whose script switched them on */
    if (gkgtGameState.kgtGameMode == GAME_MODE_STORY
            && gkgtGameState.iGameStateNumber >= 3000 && gkgtGameState.iGameStateNumber < 4000) {
        for (i = 1; i < 8; i++) {
            pChar = &gkgtLoadedCharacter[i];
            if (pChar && pChar->iOnlineState && pChar->iHealth && pChar->iShowLife && pChar->bImageShown) {
                /* a 50-pixel bar centred over the character (16.16 position -> pixels, minus the camera):
                   darkened backdrop (blend 1 = 50 % mix), life in green (RGB555 0x3e0; the red term iLifeWidth / 323 is
                   always 0 for these widths), then the recent damage in red (0x7c00) */
                iLifeWidth = pChar->iHealth * 50 / pChar->iLifeMax;
                iBarX = pChar->iCurrentXPos / 0x10000 - giCameraX - 25;
                iBarY = pChar->shYPosOfSideHp + pChar->iCurrentYPos / 0x10000 - giCameraY;
                vFillRect16(iBarX, iBarY, 50, 10, 1, 0);
                vFillRect16(iBarX, iBarY + 1, iLifeWidth, 8, 0, (iLifeWidth / 323 << 10) + 0x3e0);
                vFillRect16(iBarX + iLifeWidth, iBarY + 1, pChar->iDamageBar * 50 / pChar->iLifeMax, 8, 0, 0x7c00);
                /* system option "numbers on the life bar" */
                if (gkgtKgtSystem.cSystemBitmask & 0x20) {
                    vDrawNumberSmall(pChar->iHealth, iBarX - 18, iBarY, 0, 0, 0, 0);
                    vDrawNumberSmall(pChar->iDamageBar, iBarX + 40, iBarY, 0, 0x20, 0, 0);
                }
            }
        }
    }

    /* hit-judge display (test play, F1): team rosters and a status line per player */
    if (giHitJudge) {
        /* team battle: the character files of both teams */
        if (gkgtGameState.kgtGameMode == GAME_MODE_VS_TEAM) {
            for (i = 0; i < 2; i++) {
                sprintf(szBuf, "%d - ", i);
                for (j = 0; j < 4; j++)
                    sprintf(szBuf + strlen(szBuf), " %2d", gkgtGameState.aiTeamRoster[i * 4 + j]);  /* the original: sprintf(szBuf, "%s %2d", szBuf, ...), printing onto its own argument (worked with VC6's CRT; undefined behaviour in C) */
                vDrawTextSmall(szBuf, 500, 50 + i * 16, 0, 0, 0, 0);
            }
        }
        /* in battle: one 16-pixel row per active player */
        if (gkgtGameState.iGameStateNumber >= 3000 && gkgtGameState.iGameStateNumber < 4000) {
            for (i = 0; i < 8; i++) {
                pChar = &gkgtLoadedCharacter[i];
                pObj = pChar->pkgtoSelf;
                if (pChar->iOnlineState) {
                    iBarY = 9 + i * 16;
                    /* a 200-pixel life bar with its numbers, as above */
                    iLifeWidth = pChar->iHealth * 200 / pChar->iLifeMax;
                    vFillRect16(64, iBarY - 1, 200, 10, 1, 0);
                    vFillRect16(64, iBarY, iLifeWidth, 8, 0, (iLifeWidth / 323 << 10) + 0x3e0);
                    vFillRect16(iLifeWidth + 64, iBarY, pChar->iDamageBar * 200 / pChar->iLifeMax, 8, 0, 0x7c00);
                    vDrawNumberSmall(pChar->iHealth, 46, iBarY - 1, 0, 0, 0, 0);
                    vDrawNumberSmall(pChar->iDamageBar, 254, iBarY - 1, 0, 0x20, 0, 0);
                    /* M-number slots 0-9: the digit if an object is attached, else "-" */
                    for (j = 0, pSlotText = szSlotTexts[0], ppMNumberObj = pChar->pMNumberObjs; j < 10; j++, ppMNumberObj++, pSlotText += 2) {
                        if (*ppMNumberObj)
                            sprintf(pSlotText, "%d", j);
                        else
                            sprintf(pSlotText, "-");
                    }
                    /* skill-step:cancel state / S: stocks / W: wins L: losses / WP: win points / OBJ: slots */
                    sprintf(szLine, "%3d-%4d:%1d / S:%3d / W:%2d L:%2d / WP:%3d / OBJ:%s%s%s%s%s%s%s%s%s%s",
                            pObj->iSkillIdx, pObj->iSkillScriptIdx, pChar->iCurrentActionCancellableFlag,
                            pChar->iSpecialGaugeTokens, pChar->iWins, pChar->iLosses,
                            pChar->iWinPoints,
                            szSlotTexts[0], szSlotTexts[1], szSlotTexts[2], szSlotTexts[3], szSlotTexts[4], szSlotTexts[5], szSlotTexts[6], szSlotTexts[7], szSlotTexts[8], szSlotTexts[9]);
                    vDrawTextSmall(szLine, 272, iBarY, 4, 0, 0, 0);

                    /* animate the three values of giPixelFormat565 between 8 and 0x600 with the speeds in [3..5]
                       (bouncing at the ends); nothing reads the result */
                    giPixelFormat565[0] += giPixelFormat565[3];
                    giPixelFormat565[1] += giPixelFormat565[4];
                    giPixelFormat565[2] += giPixelFormat565[5];
                    if (giPixelFormat565[0] < 8) {
                        giPixelFormat565[3] = -giPixelFormat565[3];
                        giPixelFormat565[0] += giPixelFormat565[3];
                    }
                    if (giPixelFormat565[1] < 8) {
                        giPixelFormat565[4] = -giPixelFormat565[4];
                        giPixelFormat565[1] += giPixelFormat565[4];
                    }
                    if (giPixelFormat565[2] < 8) {
                        giPixelFormat565[5] = -giPixelFormat565[5];
                        giPixelFormat565[2] += giPixelFormat565[5];
                    }
                    if (giPixelFormat565[0] > 0x600) {
                        giPixelFormat565[3] = -giPixelFormat565[3];
                        giPixelFormat565[0] += giPixelFormat565[3];
                    }
                    if (giPixelFormat565[1] > 0x600) {
                        giPixelFormat565[4] = -giPixelFormat565[4];
                        giPixelFormat565[1] += giPixelFormat565[4];
                    }
                    if (giPixelFormat565[2] > 0x600) {
                        giPixelFormat565[5] = -giPixelFormat565[5];
                        giPixelFormat565[2] += giPixelFormat565[5];
                    }
                    /* the current skill's name in a 13-pixel Terminal font (Shift-JIS), drawn with GDI into the
                       frame buffer DIB: a black outline (four offset copies), then white */
                    hFrameDc = ghFrameDc;
                    hFont = CreateFontA(13, 6, 0, 0, 0, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, 0, FF_MODERN, "Terminal");
                    hOldFont = SelectObject(hFrameDc, hFont);
                    SetBkMode(hFrameDc, TRANSPARENT);
                    sprintf(szBuf, "%s", (char *)(pChar->kgtCore.pSkillsAlloc + pObj->iSkillIdx));   /* the skill header starts with its name */
                    SetTextColor(hFrameDc, 0);
                    TextOutA(hFrameDc, 100, iBarY + 4, szBuf, lstrlenA(szBuf));
                    TextOutA(hFrameDc, 100, iBarY + 2, szBuf, lstrlenA(szBuf));
                    TextOutA(hFrameDc, 101, iBarY + 3, szBuf, lstrlenA(szBuf));
                    TextOutA(hFrameDc, 99, iBarY + 3, szBuf, lstrlenA(szBuf));
                    SetTextColor(hFrameDc, 0xffffff);
                    TextOutA(hFrameDc, 100, iBarY + 3, szBuf, lstrlenA(szBuf));
                    SelectObject(hFrameDc, hOldFont);
                    DeleteObject(hFont);
                }
            }
        }
    }
    /* backdrop of a debug status line (gbShowDebugStatus is never set) */
    if (gbShowDebugStatus)
        vFillRect16(8, 456, 624, 16, 1, 0);

    giReverseShakeDirection++;  /* frame counter; its low bit alternates the shake direction */
    /* status display (F3): FPS, skipped frames, object count (story mode: win points), then the
       debug messages.  The GDI font made here is not used: the text is drawn from text.bmp */
    if (gkgtGameState.uStatusDisplay) {
        hFrameDc = ghFrameDc;
        hFont = CreateFontA(26, 12, 0, 0, 0, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, 0, FF_MODERN, "Terminal");
        hOldFont = SelectObject(hFrameDc, hFont);
        SetBkMode(hFrameDc, TRANSPARENT);
        sprintf(szBuf, "FPS:%3d SkipFrame:%3d Object:%3d", (int)(1000 / gdwFrameTimeDiff), giSkipframeCount - 1, giObjectCount);
        vDrawTextMedium(szBuf, 4, 460, 1, 0, 0, 0);
        if (gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
            sprintf(szBuf, "WinPoint:%3d", gkgtLoadedCharacter[0].iWinPoints);
            vDrawTextMedium(szBuf, 450, 460, 1, 0, 0, 0);
        }
        SelectObject(hFrameDc, hOldFont);
        DeleteObject(hFont);
        vDrawDebugInfo();
    }

    /* show the frame.  Full screen: restore lost surfaces, lock the back buffer, copy the 640x480
       frame buffer into it (1280 bytes per row, the surface's own pitch) and flip */
    if (giScreenMode) {
        if (giModeSwitchFrames)
            giModeSwitchFrames--;
        if ((gpDDSPrimary && IDirectDrawSurface_IsLost(gpDDSPrimary) == DDERR_SURFACELOST)
                || (gpDDSBack && IDirectDrawSurface_IsLost(gpDDSBack) == DDERR_SURFACELOST)) {
            if (gpDDSPrimary)
                IDirectDrawSurface_Restore(gpDDSPrimary);
            if (gpDDSBack)
                IDirectDrawSurface_Restore(gpDDSBack);
        }
        memset(&gDDSurfaceDesc, 0, sizeof(gDDSurfaceDesc));
        gDDSurfaceDesc.dwSize = sizeof(gDDSurfaceDesc);
        if (IDirectDrawSurface_Lock(gpDDSBack, NULL, &gDDSurfaceDesc, 0, NULL) == DD_OK) {
            copy_ppvBits_to_lpSurface(gDDSurfaceDesc.lpSurface, gpFrameBits, 1280, 480, gDDSurfaceDesc.lPitch, 1280);
            IDirectDrawSurface_Unlock(gpDDSBack, gDDSurfaceDesc.lpSurface);
            IDirectDrawSurface_Flip(gpDDSPrimary, NULL, DDFLIP_WAIT);
        }
    } else
        vWindowBltFuncs();  /* window: blit or stretch the DIB to the window */
}

/* ------------------------------------------------------------------------------------------ */
/* window                                                                                      */
/* ------------------------------------------------------------------------------------------ */

/*
 * Window mode: moves the window back into the desktop work area (the screen without the taskbar)
 * when it sticks out on the right or at the bottom or has a negative position; the size is kept.
 * Parameters: hWnd - the game window.
 * Globals: reads giScreenMode.
 */
void vCheckWindowBounds(HWND hWnd)
{
    RECT rcWindow;
    RECT rcWork;
    int bMove = 0;

    if (giScreenMode == 0) {
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &rcWork, 0);
        GetWindowRect(hWnd, &rcWindow);
        if (rcWork.right < rcWindow.right) {
            rcWindow.left += rcWork.right - rcWindow.right;
            bMove = 1;
        }
        if (rcWork.bottom < rcWindow.bottom) {
            rcWindow.top += rcWork.bottom - rcWindow.bottom;
            bMove = 1;
        }
        if (rcWindow.left < 0) {
            rcWindow.left = 0;
            bMove = 1;
        }
        if (rcWindow.top < 0) {
            rcWindow.top = 0;
            bMove = 1;
        }
        if (bMove)
            SetWindowPos(hWnd, NULL, rcWindow.left, rcWindow.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

/*
 * Start-up: allocates the decompression buffer, takes the CD drive letter from the current
 * directory, reads the ini settings, clears the game's structures, sets up the input dialog classes,
 * the debug messages, netplay and the joysticks, registers the window class and creates the game
 * window, creates the two 16-bit DIB sections (the 640x480 frame buffer and a 640x16 strip), enters the
 * configured display mode, sets up DirectSound and empties the engine object table.
 * Globals: changes gpGlobalMemoryAlloc, gszCdDrive, the config globals, gBmiFrame, gBmiStrip,
 * gkgtBitmaps, gkgtGameState, gkgtLoadedCharacter, gkgtKgtSystem, gkgtLoadedDemo, gkgtLoadedStage,
 * ghWnd, grWindowRect, the frame buffer and strip DIB globals, giScreenMode, giPlayerFileIndices.
 */
void vInitializeWindowsAndMemory(void)
{
    WNDCLASSA wndClass;
    char szCurrentDir[260];
    HDC hWindowDc;

    gpGlobalMemoryAlloc = GlobalAlloc(GMEM_FIXED, 0x138800);  /* 0x138800 = 1,280,000 bytes */
    GetCurrentDirectoryA(259, szCurrentDir);
    gszCdDrive[0] = szCurrentDir[0];  /* the CD is taken to be the drive the game runs from */
    /* settings from the ini files */
    vLoadKgt2kConfig();
    vLoadGameConfig();
    /* 0x438 bytes: gBmiFrame with room for 256 colours, and the frame buffer DC, bitmap and pixel
       pointer globals right after it */
    vMemzero(&gBmiFrame, 0x438);
    vMemzero(gkgtBitmaps, sizeof(gkgtBitmaps));
    vMemzero(&gkgtGameState, sizeof(gkgtGameState));
    vMemzero(gkgtLoadedCharacter, sizeof(gkgtLoadedCharacter));
    vMemzero(&gkgtKgtSystem, sizeof(gkgtKgtSystem));
    vMemzero(&gkgtLoadedDemo, sizeof(gkgtLoadedDemo));
    vMemzero(&gkgtLoadedStage, sizeof(gkgtLoadedStage));
    gkgtGameState.uStatusDisplay = giConfigTestplayGameinfo;  /* status display from the ini */
    /* dialog classes, debug messages, netplay, joysticks */
    vRegisterInputWindowClasses();
    vMemzeroDebugStructs();
    iInitOnline();
    iCheckJoystickOne();
    iCheckJoystickTwo();
    /* the game window: icon "exe_ico", menu "cupid_menu"; the client area from the ini (default
       640x480) plus frame, caption and menu */
    if (giConfigGameWindowSizeX <= 0 || giConfigGameWindowSizeY <= 0) {
        giConfigGameWindowSizeX = 640;
        giConfigGameWindowSizeY = 480;
    }
    wndClass.style = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wndClass.lpfnWndProc = iMainWndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = ghInstance;
    wndClass.hIcon = hLoadEmbeddedIcon("exe_ico");     /* LoadIconA(ghInstance, "exe_ico") in the original */
    wndClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wndClass.hbrBackground = NULL;
    wndClass.lpszMenuName = NULL;       /* "cupid_menu" in the original; here the menu is passed to CreateWindowExA */
    wndClass.lpszClassName = gszGameWindow;
    if (RegisterClassA(&wndClass)) {
        ghWnd = CreateWindowExA(WS_EX_APPWINDOW, gszGameWindow, "", WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_SYSMENU,
                                giConfigGameWindowPointX, giConfigGameWindowPointY,
                                giConfigGameWindowSizeX + GetSystemMetrics(SM_CXFRAME) * 2,
                                GetSystemMetrics(SM_CXFRAME) * 2 + GetSystemMetrics(SM_CYMENU) + giConfigGameWindowSizeY + GetSystemMetrics(SM_CYCAPTION),
                                NULL, hLoadEmbeddedMenu("cupid_menu"), ghInstance, NULL);
        ShowWindow(ghWnd, SW_SHOW);
        GetWindowRect(ghWnd, &grWindowRect);
    }
    /* the frame buffer: a 640x480 16-bit DIB section (BI_RGB 16 bit = RGB555), top-down because of
       the negative height; everything is drawn into gpFrameBits */
    hWindowDc = GetDC(ghWnd);
    gBmiFrame.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gBmiFrame.bmiHeader.biPlanes = 1;
    gBmiFrame.bmiHeader.biBitCount = 16;
    gBmiFrame.bmiHeader.biCompression = BI_RGB;
    gBmiFrame.bmiHeader.biSizeImage = 0;
    gBmiFrame.bmiHeader.biClrUsed = 0;
    gBmiFrame.bmiHeader.biClrImportant = 0;
    gBmiFrame.bmiHeader.biWidth = 640;
    gBmiFrame.bmiHeader.biHeight = -480;
    ghFrameDc = CreateCompatibleDC(hWindowDc);
    ghFrameBitmap = CreateDIBSection(ghFrameDc, &gBmiFrame, DIB_RGB_COLORS, &gpFrameBits, NULL, 0);
    ghFrameOldBitmap = SelectObject(ghFrameDc, ghFrameBitmap);
    /* a second, 640x16 DIB section (nothing draws into it) */
    gBmiStrip.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gBmiStrip.bmiHeader.biPlanes = 1;
    gBmiStrip.bmiHeader.biBitCount = 16;
    gBmiStrip.bmiHeader.biCompression = BI_RGB;
    gBmiStrip.bmiHeader.biSizeImage = 0;
    gBmiStrip.bmiHeader.biClrUsed = 0;
    gBmiStrip.bmiHeader.biClrImportant = 0;
    gBmiStrip.bmiHeader.biWidth = 640;
    gBmiStrip.bmiHeader.biHeight = -16;
    ghStripDc = CreateCompatibleDC(hWindowDc);
    ghStripBitmap = CreateDIBSection(ghStripDc, &gBmiStrip, DIB_RGB_COLORS, &gpStripBits, NULL, 0);
    ghStripOldBitmap = SelectObject(ghStripDc, ghStripBitmap);
    ReleaseDC(ghWnd, hWindowDc);
    /* display mode, sound, engine objects; no character in any player slot yet (-1) */
    giScreenMode = giConfigGameScreenMode;
    vSetupDdrawPrimarySurface();
    vSetupDsound();
    vEmptyEngineObjects();
    memset(giPlayerFileIndices, -1, sizeof(giPlayerFileIndices));
}

/*
 * Shutdown: stops all sound, frees the external bitmaps, deletes the two DIB sections, releases
 * DirectSound and DirectDraw, closes the netplay session, unloads all KGT files, saves the display mode,
 * the ini settings and the netplay names, and frees the decompression buffer.
 * Globals: changes gkgtBitmaps, the DIB globals, the loaded files, giConfigGameScreenMode,
 * gpGlobalMemoryAlloc (freed).
 */
void vShutdownAndFreeResources(void)
{
    int i;

    vHandleStoppingAllWavs(0);
    for (i = 0; i < 128; i++) {
        if (gkgtBitmaps[i].pData)
            GlobalFree(gkgtBitmaps[i].pData);
    }
    /* the DIB sections: select the old bitmap back before deleting */
    SelectObject(ghFrameDc, ghFrameOldBitmap);
    DeleteObject(ghFrameBitmap);
    DeleteDC(ghFrameDc);
    SelectObject(ghStripDc, ghStripOldBitmap);
    DeleteObject(ghStripBitmap);
    DeleteDC(ghStripDc);
    /* sound, display, netplay */
    vReleaseDsound();
    vReleaseDdrawInterfaces();
    iOnlineCloseSession();
    /* unload the KGT files */
    iClearKgtSystemFile();
    for (i = 0; i < 8; i++)
        iClearCharacterFile(i);
    iClearDemoFile();
    iClearStageFile();
    /* settings back to the ini */
    giConfigGameScreenMode = giScreenMode;
    vSaveGameConfig();
    vSaveOnlineNamesToIni();
    GlobalFree(gpGlobalMemoryAlloc);
}

/*
 * The main loop, until WM_QUIT.  Runs 8 ticks first.  Then, whenever no window message is waiting and
 * a tick period (giFrameMs, 10 ms here: 100 ticks per second) has passed since the last tick, it runs
 * the ticks that are due (at most 9; the rest are skipped), each one reading the inputs, running the
 * engine and sending the local input to the netplay peers, then draws one frame and handles the
 * received netplay messages until none are left.
 * gdwFrameTimeDiff (the FPS display) is the time from the last tick's scheduled time to the first check
 * after the previous frame.  When timeGetTime wraps around, one tick runs per pass and the schedule
 * moves on by a period each time.
 * Globals: changes giFrameMs, gdwSystemTime (scheduled time of the last tick run), gdwFrameTimeDiff,
 * giTimeAdjustmentFlag, giSkipframeCount, gkgtLoadedCharacter[].iOnlineLag / iInputBufferPos; reads
 * giInputBuffer, giInputBufferPos.
 */
void vGameLoop(void)
{
    MSG msg;
    int i;
    int iTicksToRun;
    int iTickMs;
    DWORD dwNow;
    DWORD dwNextTick;
    DWORD dwElapsed;
    DWORD dwLastTick;

    giFrameMs = 10;  /* 10 ms per tick (the initial 40 is never used) */
    gdwSystemTime = timeGetTime();
    /* 8 ticks before the first frame */
    for (i = 0; i < 8; i++)
        vProcessEngineObjects();
    gdwSystemTime = timeGetTime();
    for (;;) {
        /* window messages first; GetMessage returns 0 on WM_QUIT */
        if (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE)) {
            if (!GetMessageA(&msg, NULL, 0, 0))
                return;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            continue;
        }
        /* no message: is a tick due? */
        iTickMs = giFrameMs;
        iTicksToRun = 0;
        dwNow = timeGetTime();
        /* first check after a frame: measure the time since the last tick */
        if (giTimeAdjustmentFlag == 0) {
            dwLastTick = gdwSystemTime;
            if (dwNow >= dwLastTick) {
                dwElapsed = dwNow - dwLastTick;
                gdwFrameTimeDiff = dwElapsed;
            } else {
                dwElapsed = dwLastTick - dwNow;
                gdwFrameTimeDiff = dwElapsed;
            }
            giTimeAdjustmentFlag = 1;
            dwNextTick = dwLastTick + iTickMs;
        } else {
            dwLastTick = gdwSystemTime;
            dwNextTick = dwLastTick + iTickMs;
        }
        /* a tick is due (or timeGetTime wrapped around) */
        if (dwNow >= dwLastTick + iTickMs || dwNow + iTickMs <= dwLastTick) {
            /* count the ticks due (only the first 9 run) and move the schedule to the last of them */
            if (dwNow >= dwNextTick) {
                i = 0;
                do {
                    i++;
                    if (i < 10)
                        iTicksToRun++;
                    dwNextTick += iTickMs;
                    dwLastTick += iTickMs;
                } while (dwNextTick <= dwNow);
                gdwSystemTime = dwLastTick;
                giSkipframeCount = i;
            } else {
                /* wrapped: one tick, the schedule a period on */
                iTicksToRun = 1;
                gdwSystemTime = dwNextTick;
            }
            /* netplay bookkeeping for this batch of ticks */
            for (i = 0; i < 8; i++) {
                gkgtLoadedCharacter[i].iOnlineLag = 0;
                gkgtLoadedCharacter[i].iInputBufferPos = giInputBufferPos;
            }
            /* the ticks: inputs, engine, local input to the peers */
            while (iTicksToRun--) {
                vGetPlayerInputs();
                vProcessEngineObjects();
                vOnlineBroadcastInput(giInputBuffer[0][giInputBufferPos]);
            }
            /* then one frame */
            vHandleDrawing();
            giTimeAdjustmentFlag = 0;
            /* handle the netplay messages until none are left, still pumping window messages */
            while (iOnlineProcessReceivedMessages()) {
                if (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE)) {
                    if (!GetMessageA(&msg, NULL, 0, 0))
                        return;
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }
    }
}

/*
 * Window mode: stores the window's normal (not minimized or maximized) position in
 * giConfigGameWindowPointX/Y for the ini; uses GetWindowRect when GetWindowPlacement fails.
 * Parameters: hWnd - the game window.
 * Globals: reads giScreenMode; changes giConfigGameWindowPointX, giConfigGameWindowPointY.
 */
void vGetWindowPos(HWND hWnd)
{
    RECT rcWindow;
    WINDOWPLACEMENT wndPlacement;

    if (giScreenMode == 0) {
        if (GetWindowPlacement(hWnd, &wndPlacement)) {
            giConfigGameWindowPointX = wndPlacement.rcNormalPosition.left;
            giConfigGameWindowPointY = wndPlacement.rcNormalPosition.top;
        } else {
            GetWindowRect(hWnd, &rcWindow);
            giConfigGameWindowPointX = rcWindow.left;
            giConfigGameWindowPointY = rcWindow.top;
        }
    }
}

/*
 * Program entry.  Only one instance runs: if a game window exists already it returns 1 at once.
 * The command line may hold a switch -t, -s, -f or -d (or /t ...): the game was started by the editor
 * for a test play, giAppmode 1-4 (1 starts at the menu, 3 goes straight to a VS battle of the
 * configured players, see engine.c; 2 and 4 were not traced).  Digits are collected into iNumber,
 * which is not used.  After choosing the ini section ("TestPlay" or "GamePlay") it initialises
 * everything and runs the main loop; after a test play it deletes the editor's temporary .t copies of
 * all characters, stages and demos, which lie in the directory of the system file.
 * Parameters: the usual WinMain ones; only hInstance and szCmdLine are used.
 * Returns 0 (1 when another instance runs).
 * Globals: changes gszCurrentDirectory, ghInstance, giAppmode, gpsLpAppName; reads gkgtKgtSystem,
 * gszConfigReturnedFilename.
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int nCmdShow)
{
    static char *psTestplay = "TestPlay";
    static char *psGameplay = "GamePlay";
    MSG msg;
    char *pCh;
    int iNumber;
    int i;
    int bFoundSlash;
    char szTestFile[256];
    char szProjectDir[256];

    GetCurrentDirectoryA(255, gszCurrentDirectory);
    /* one instance only */
    if (FindWindowA(gszGameWindow, NULL))
        return 1;
    ghInstance = hInstance;
    /* always NULL on Win32; msg is not set here (as in the original) */
    if (hPrevInstance)
        return msg.wParam;
    giAppmode = 0;
    iNumber = 0;
    /* the command line switches */
    if (lstrlenA(szCmdLine)) {
        pCh = szCmdLine;
        while (*pCh) {
            if (*pCh >= '0' && *pCh <= '9')
                iNumber = iNumber * 10 + *pCh - '0';
            if (*pCh == '-' || *pCh == '/') {
                pCh++;
                /* matching: each case repeats "pCh++; continue;"; VC6 merges these tails again (vc6-matching-notes/matching-techniques.md
                   lists a duplicated tail among the levers that fixed WinMain's register choice) */
                switch (*pCh) {
                case 'T':
                case 't':
                    giAppmode = 1;
                    pCh++;
                    continue;
                case 'S':
                case 's':
                    giAppmode = 2;
                    pCh++;
                    continue;
                case 'F':
                case 'f':
                    giAppmode = 3;
                    pCh++;
                    continue;
                case 'D':
                case 'd':
                    giAppmode = 4;
                    pCh++;
                    continue;
                default:
                    continue;
                }
            }
            pCh++;
        }
    }
    /* ini section of the settings */
    if (giAppmode)
        gpsLpAppName = psTestplay;
    else
        gpsLpAppName = psGameplay;
    /* run the game */
    vInitializeWindowsAndMemory();
    vGameLoop();
    if (giAppmode) {
        /* after a test play: the directory of the system file (gszConfigReturnedFilename up to its
           last backslash), then delete the .t copies found there */
        bFoundSlash = 0;
        for (i = 254; i >= 0; i--) {
            szProjectDir[i] = gszConfigReturnedFilename[i];
            if (!bFoundSlash && gszConfigReturnedFilename[i] == '\\') {
                bFoundSlash = 1;
                szProjectDir[i + 1] = '\0';
            }
        }
        for (i = 0; i < 50; i++) {
            if (gkgtKgtSystem.szCharacterNames[i][0]) {
                sprintf(szTestFile, "%s%s.player.t", szProjectDir, gkgtKgtSystem.szCharacterNames[i]);
                DeleteFileA(szTestFile);
            }
        }
        for (i = 0; i < 50; i++) {
            if (gkgtKgtSystem.szStageNames[i][0]) {
                sprintf(szTestFile, "%s%s.stage.t", szProjectDir, gkgtKgtSystem.szStageNames[i]);
                DeleteFileA(szTestFile);
            }
        }
        for (i = 0; i < 100; i++) {
            if (gkgtKgtSystem.szDemoNames[i][0]) {
                sprintf(szTestFile, "%s%s.demo.t", szProjectDir, gkgtKgtSystem.szDemoNames[i]);
                DeleteFileA(szTestFile);
            }
        }
    }
    vShutdownAndFreeResources();
    return 0;
}

/*
 * Window procedure of the game window.
 *   WM_CREATE, WM_MOVE, WM_SIZE, WM_PAINT: keep the window in the work area, remember its position and
 *     client size (the stretch target) and redraw it;
 *   WM_ACTIVATEAPP: restore the full-screen surfaces when the game is activated again; in test play
 *     with the "exit" option, losing the focus quits;
 *   F4 and Alt+Enter toggle full screen / window;
 *   test play only: F1 hit-judge display, F3 status display, F5 win (story mode: 100 win points,
 *     VS: player 2's life to 0), F6 lose (player 1's life to 0), F12 end the round (giForceRoundEnd);
 *   WM_GETMINMAXINFO: minimum client area 160x120;
 *   MM_MCINOTIFY: the CD track or the MIDI file ended: play it again (loop) or close the device;
 *   WM_COMMAND: menu commands (vHandleWmCommand, dialogs.c).
 * Returns 0 for handled messages, else DefWindowProc's result.
 * Globals: changes giConfigGameWindowSizeX/Y, giScreenMode, gdwSystemTime, giHitJudge,
 * gkgtGameState.uStatusDisplay, gkgtLoadedCharacter[0..1], giForceRoundEnd, giCdAudioError; reads
 * giAppmode, giConfigTestplayExit, the display mode flags and the MCI device state.
 */
LRESULT CALLBACK iMainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    MINMAXINFO *pMinMaxInfo;

    switch (uMsg) {
    case WM_CREATE:
        vCheckWindowBounds(hWnd);
        return 0;
    case WM_PAINT:
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        if (giScreenMode == 0)
            vWindowBltFuncs();
        return 0;
    case WM_SIZE:
        if (giScreenMode == 0) {
            /* window mode: the new client size */
            giConfigGameWindowSizeX = LOWORD(lParam);
            giConfigGameWindowSizeY = HIWORD(lParam);
            vCheckWindowBounds(hWnd);
            vGetWindowPos(hWnd);
            SendMessageA(hWnd, WM_PAINT, 0, 0);  /* redraw at the new size */
            return DefWindowProcA(hWnd, uMsg, wParam, lParam);
        }
        return 0;
    case WM_MOVE:
        vCheckWindowBounds(hWnd);
        vGetWindowPos(hWnd);
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    case WM_ACTIVATEAPP:
        /* activated again in full screen: restore the lost surfaces */
        if (wParam && gbDisplayModeReady && gbSwitchingDisplayMode == 0 && giScreenMode)
            vRestoreDdrawInterfaces();
        /* test play option: quit when the window loses the focus */
        if (giAppmode && giConfigTestplayExit && !wParam)
            PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F4) {
            giScreenMode = (giScreenMode - 1) & 1;  /* toggle 0 <-> 1 */
            vSetupDdrawPrimarySurface();
            gdwSystemTime = timeGetTime();  /* restart the tick schedule after the switch */
        }
        /* the other keys only in test play */
        if (giAppmode == 0)
            return 0;
        switch (wParam) {
        case VK_F1:
            giHitJudge ^= 1;
            if (giHitJudge)
                iSetDebugInfo("\223\226\202\275\202\350\224\273\222\350\230g\225\\\216\246\201@\202n\202m", 0xefefff);  /* 当たり判定枠表示　ＯＮ */
            else
                iSetDebugInfo("\223\226\202\275\202\350\224\273\222\350\230g\225\\\216\246\201@\202n\202e\202e", 0xefefff);  /* 当たり判定枠表示　ＯＦＦ */
            return 0;
        case VK_F3:
            gkgtGameState.uStatusDisplay ^= 1;
            if (gkgtGameState.uStatusDisplay)
                iSetDebugInfo("\203X\203e\201[\203^\203X\225\\\216\246\201F\202n\202m", 0xdfffff);  /* ステータス表示：ＯＮ */
            else
                iSetDebugInfo("\203X\203e\201[\203^\203X\225\\\216\246\201F\202n\202e\202e", 0xdfffff);  /* ステータス表示：ＯＦＦ */
            return 0;
        case VK_F5:
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                gkgtLoadedCharacter[0].iWinPoints = 100;
                break;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                gkgtLoadedCharacter[1].iHealth = 0;
                break;
            }
            return 0;
        case VK_F6:
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                gkgtLoadedCharacter[0].iHealth = 0;
                break;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                gkgtLoadedCharacter[0].iHealth = 0;
                break;
            }
            return 0;
        case VK_F12:
            giForceRoundEnd++;
            return 0;
        }
        return 0;
    case WM_GETMINMAXINFO:
        pMinMaxInfo = (MINMAXINFO *)lParam;
        /* minimum client area 160x120, plus frame, caption and menu */
        pMinMaxInfo->ptMinTrackSize.x = GetSystemMetrics(SM_CXFRAME) * 2 + 160;
        pMinMaxInfo->ptMinTrackSize.y = GetSystemMetrics(SM_CXFRAME) * 2 + GetSystemMetrics(SM_CYMENU) + 120 + GetSystemMetrics(SM_CYCAPTION);
        return 0;
    case WM_SYSKEYUP:
        /* Alt+Enter: like F4 */
        if (wParam == VK_RETURN) {
            giScreenMode = (giScreenMode - 1) & 1;  /* toggle 0 <-> 1 */
            vSetupDdrawPrimarySurface();
            gdwSystemTime = timeGetTime();  /* restart the tick schedule after the switch */
        }
        if (giScreenMode)
            return 0;
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    case MM_MCINOTIFY:
        /* MCI notification, lParam = the device: the CD track or the MIDI file ended or failed */
        if (gbCdAudioOpen && LOWORD(lParam) == guCdAudioDeviceId) {
            switch (wParam) {
            case MCI_NOTIFY_SUCCESSFUL:
                if (giDiscLoopFlag) {
                    if (iOpenAndPlayCdAudio())
                        giCdAudioError = 1;
                } else {
                    vStopAndCloseCdAudio();
                }
                break;
            case MCI_NOTIFY_FAILURE:
                vStopAndCloseCdAudio();
                giCdAudioError = 1;
                break;
            }
        } else if (gbMidiOpen && LOWORD(lParam) == guMidiDeviceId) {
            switch (wParam) {
            case MCI_NOTIFY_SUCCESSFUL:
                if (giMidiLoopFlag)
                    iPlayAndCloseMidFile();
                else
                    vStopAndCloseMidi();
                break;
            case MCI_NOTIFY_FAILURE:
                vStopAndCloseMidi();
                break;
            }
        }
        return 0;
    case WM_COMMAND:
        /* from the menu or an accelerator (no control window) */
        if (LOWORD(lParam) == 0)
            vHandleWmCommand(hWnd, wParam);
        return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

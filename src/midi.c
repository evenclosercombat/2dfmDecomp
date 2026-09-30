/*
 * midi.c - MIDI playback for the sounds of the game data whose type is "MIDI" (kgtSound.cFlags & 0xf
 * == 2, bit 0x10 = loop; see vHandleLoadingSound in main.c): the Standard MIDI File held in memory
 * is written to a temporary .mid file in the Windows directory and played by the MCI sequencer.
 *
 * Playback notifies the window (MM_MCINOTIFY) when it ends; the main window procedure then calls
 * iPlayAndCloseMidFile again when giMidiLoopFlag is set, or closes the device.
 */
#include "kgt.h"

/* ---- externs not (yet) in globals.h / protos.h ---- */
extern int gbMidiOpen;                      /* 0x424738: the MIDI device is open */
extern MCIDEVICEID guMidiDeviceId;          /* 0x42473c: MCI device of the MIDI sequencer */
extern int giMidiLoopFlag;                  /* 0x424740: loop the MIDI file */
void vDeleteTempMidFile(void);
int iPlayAndCloseMidFile(void);
/* ---- */

char gszMidFileName[] = "\\2dfightermaker2nd20022.mid";  /* 0x41f798: file the MIDI data is written to (appended to the Windows directory) */

/*
 * Stops the MIDI file and closes the MCI sequencer device, if it is open.
 * Globals: reads guMidiDeviceId; changes gbMidiOpen.
 */
void vStopAndCloseMidi(void)
{
    if (gbMidiOpen) {
        mciSendCommandA(guMidiDeviceId, MCI_STOP, 0, 0);
        mciSendCommandA(guMidiDeviceId, MCI_CLOSE, 0, 0);
        gbMidiOpen = 0;
    }
}

/*
 * Deletes the temporary .mid file (<Windows directory>\2dfightermaker2nd20022.mid).
 * Globals: reads gszMidFileName.
 */
void vDeleteTempMidFile(void)
{
    char szPath[264];

    GetWindowsDirectoryA(szPath, MAX_PATH + 1);
    /* sprintf onto its own first argument: appends the file name (works with this CRT) */
    sprintf(szPath, "%s%s", szPath, gszMidFileName);
    DeleteFileA(szPath);
}

/*
 * Plays the temporary .mid file from the start: when the sequencer is already open (a loop
 * restart) it only seeks back to the start, else it opens the "sequencer" device on the file.
 * Playback notifies ghWnd when it ends.  (Despite the Ghidra name, nothing is closed on success.)
 * Returns 0 on success, 1 when the open or the play command failed (the device is then closed).
 * Globals: reads gszMidFileName, ghWnd; changes guMidiDeviceId, gbMidiOpen.
 */
int iPlayAndCloseMidFile(void)
{
    MCI_OPEN_PARMS mciOpen;
    MCI_PLAY_PARMS mciPlay;
    char szPath[264];

    if (gbMidiOpen) {
        /* already open: rewind */
        mciSendCommandA(guMidiDeviceId, MCI_SEEK, MCI_SEEK_TO_START, 0);
    } else {
        /* open the sequencer on <Windows directory>\2dfightermaker2nd20022.mid */
        GetWindowsDirectoryA(szPath, MAX_PATH + 1);
        sprintf(szPath, "%s%s", szPath, gszMidFileName);
        mciOpen.dwCallback = (DWORD)ghWnd;
        mciOpen.lpstrDeviceType = "sequencer";
        mciOpen.lpstrElementName = szPath;
        if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, (DWORD)&mciOpen))
            return 1;
        guMidiDeviceId = mciOpen.wDeviceID;
    }
    /* play to the end, with MM_MCINOTIFY to the window */
    mciPlay.dwCallback = (DWORD)ghWnd;
    if (mciSendCommandA(guMidiDeviceId, MCI_PLAY, MCI_NOTIFY, (DWORD)&mciPlay)) {
        mciSendCommandA(guMidiDeviceId, MCI_STOP, 0, 0);
        mciSendCommandA(guMidiDeviceId, MCI_CLOSE, 0, 0);
        return 1;
    }
    gbMidiOpen = 1;
    return 0;
}

/*
 * Plays a MIDI sound: stops the current MIDI, writes the sound's data (a Standard MIDI File in
 * pSound->pAlloc, pSound->iSize bytes) to the temporary .mid file and starts it.
 * pSound: the sound (type MIDI); bit 0x10 of its cFlags loops it.
 * Globals: changes giMidiLoopFlag (and what vStopAndCloseMidi / iPlayAndCloseMidFile change).
 */
void vWriteAndPlayMidFile(kgtSound *pSound)
{
    HGLOBAL hMidiData;
    LPVOID pMidiData;
    HANDLE hFile;
    DWORD dwWritten;
    char szPath[264];

    /* bit 0x10: loop */
    if (pSound->cFlags & 0x10)
        giMidiLoopFlag = 1;
    else
        giMidiLoopFlag = 0;
    /* drop the previous file and stop it (in this order: the delete comes before the close;
       CREATE_ALWAYS below overwrites the file anyway) */
    vDeleteTempMidFile();
    vStopAndCloseMidi();
    /* write the data to <Windows directory>\2dfightermaker2nd20022.mid */
    dwWritten = 0;
    GetWindowsDirectoryA(szPath, MAX_PATH + 1);
    sprintf(szPath, "%s%s", szPath, gszMidFileName);
    hMidiData = pSound->pAlloc;
    pMidiData = GlobalLock(hMidiData);
    hFile = CreateFileA(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(hFile, pMidiData, pSound->iSize, &dwWritten, NULL);
    CloseHandle(hFile);
    GlobalUnlock(hMidiData);
    /* and play it */
    iPlayAndCloseMidFile();
}

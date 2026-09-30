/*
 * cdaudio.c - CD audio (Red Book) playback through MCI, for the sounds of the game data whose type
 * is "CD track" (kgtSound.cFlags & 0xf == 3, bit 0x10 = loop; see vHandleLoadingSound in main.c).
 *
 * A track is played from its start to the start of the next track with MCI_NOTIFY, so the window
 * receives MM_MCINOTIFY when it ends; the main window procedure then replays it when giDiscLoopFlag
 * is set, or closes the device.  The CD drive is not configured: "d:" is tried first (gszCdDrive in globals.c), then every
 * drive letter whose GetDriveType is DRIVE_CDROM.
 */
#include "kgt.h"

/* ---- externs not (yet) in globals.h / protos.h ---- */
extern int giCdTrack;                       /* 0x41e404: CD track to play, -1 = none */
extern char gszCdDrive[];                   /* 0x41e408: "X:" drive of the CD */
extern MCIDEVICEID guCdAudioDeviceId;       /* 0x424720: MCI device of the CD audio */
extern int gbCdAudioOpen;                   /* 0x424724: the CD audio device is open */
extern int giDiscLoopFlag;                  /* 0x424728: loop the CD track (checked on MM_MCINOTIFY) */
extern int giCdAudioError;                  /* 0x42472c: CD audio failed, do not try again */
extern int giCdAudioRequested;              /* 0x424734: a CD track has been requested */
BOOL bCheckIfDiscDrive(char cDriveIdx);
void vStopCdAudio(void);
int iOpenAndPlayCdAudio(void);
/* ---- */

/*
 * Tells whether a drive is a CD-ROM drive.
 * cDriveIdx: drive number, 0 = A:, 1 = B:, ...
 * Returns TRUE for DRIVE_CDROM.
 */
BOOL bCheckIfDiscDrive(char cDriveIdx)
{
    char szRoot[] = "A:\\";

    /* "A:\" -> "<letter>:\" */
    szRoot[0] = 'A' + cDriveIdx;
    return GetDriveTypeA(szRoot) == DRIVE_CDROM;
}

/*
 * Stops the CD track and closes the MCI cdaudio device, if it is open.
 * Globals: reads guCdAudioDeviceId; changes gbCdAudioOpen.
 */
void vStopAndCloseCdAudio(void)
{
    if (gbCdAudioOpen) {
        mciSendCommandA(guCdAudioDeviceId, MCI_STOP, 0, 0);
        mciSendCommandA(guCdAudioDeviceId, MCI_CLOSE, 0, 0);
        gbCdAudioOpen = 0;
    }
}

/*
 * Stops the CD track but keeps the device open.
 * Globals: reads gbCdAudioOpen, guCdAudioDeviceId.
 */
void vStopCdAudio(void)
{
    if (gbCdAudioOpen)
        mciSendCommandA(guCdAudioDeviceId, MCI_STOP, 0, 0);
}

/*
 * (Re)opens the MCI cdaudio device on gszCdDrive and plays track giCdTrack once, from its start to
 * the start of the next track, notifying ghWnd (MM_MCINOTIFY) when it ends.  Any open device is
 * closed first; giCdTrack == -1 only closes it.
 * Returns 0 on success (or when there is no track), 1 on failure: then giCdAudioError is 1 when the
 * open or the time format failed, 2 when the play command failed.
 * Globals: reads giCdTrack, gszCdDrive, ghWnd; changes guCdAudioDeviceId, gbCdAudioOpen,
 * giCdAudioError.
 */
int iOpenAndPlayCdAudio(void)
{
    BYTE cTrack;
    MCI_OPEN_PARMS mciOpen;
    MCI_SET_PARMS mciSet;
    MCI_PLAY_PARMS mciPlay;
    MCIERROR dwMciError;

    cTrack = (BYTE)giCdTrack;
    /* close what is playing */
    if (gbCdAudioOpen) {
        vStopAndCloseCdAudio();
        gbCdAudioOpen = 0;
    }
    if (giCdTrack != -1) {
        /* open the "cdaudio" device on the drive (element "X:") */
        mciOpen.lpstrDeviceType = "cdaudio";
        mciOpen.lpstrElementName = gszCdDrive;
        dwMciError = mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, (DWORD)&mciOpen);
        guCdAudioDeviceId = mciOpen.wDeviceID;
        if (dwMciError) {
            giCdAudioError = 1;
            return 1;
        }
        /* positions in tracks/minutes/seconds/frames */
        mciSet.dwTimeFormat = MCI_FORMAT_TMSF;
        if (mciSendCommandA(guCdAudioDeviceId, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD)&mciSet)) {
            mciSendCommandA(guCdAudioDeviceId, MCI_CLOSE, 0, 0);
            giCdAudioError = 1;
            return 1;
        }
        /* play the whole track: from track:00:00:00 to (track + 1):00:00:00 */
        mciPlay.dwFrom = MCI_MAKE_TMSF(cTrack, 0, 0, 0);
        mciPlay.dwTo = MCI_MAKE_TMSF(cTrack + 1, 0, 0, 0);
        mciPlay.dwCallback = (DWORD)ghWnd;
        if (mciSendCommandA(guCdAudioDeviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO, (DWORD)&mciPlay)) {
            mciSendCommandA(guCdAudioDeviceId, MCI_CLOSE, 0, 0);
            giCdAudioError = 2;
            return 1;
        }
        gbCdAudioOpen = 1;
    }
    return 0;
}

/*
 * Starts CD track iTrack: tries the current drive (gszCdDrive, "d:" at start-up), then every CD-ROM
 * drive A: to Z:, keeping the first drive where the track plays in gszCdDrive.  When no drive
 * works, asks for the audio CD (the message and its trailing blanks are as in the original).
 * iTrack: CD track number (-1 = none); bLoop: restart the track when it ends.
 * Globals: changes giDiscLoopFlag, giCdTrack, giCdAudioRequested, gszCdDrive (and what
 * iOpenAndPlayCdAudio changes).
 */
void vLoadsoundFromDisc(int iTrack, int bLoop)
{
    int iDrive;

    giDiscLoopFlag = bLoop;
    giCdTrack = iTrack;
    giCdAudioRequested = 1;
    /* keep the drive letter, make sure it reads "X:" */
    gszCdDrive[1] = ':';
    gszCdDrive[2] = 0;
    if (iOpenAndPlayCdAudio()) {
        /* the current drive failed: search the CD-ROM drives */
        for (iDrive = 0; iDrive < 26; iDrive++) {
            if (bCheckIfDiscDrive(iDrive)) {
                gszCdDrive[0] = 'A' + iDrive;
                if (!iOpenAndPlayCdAudio())
                    return;
            }
        }
        MessageBoxA(ghWnd, "Plese insert Audio CD-rom                                                                                              ", "note", MB_ICONEXCLAMATION);
    }
}

/*
 * Stops and closes the CD audio: a wrapper of vStopAndCloseCdAudio, itself wrapped by
 * vStopCdAudio2 in main.c.
 */
void vStopCdAudio1(void)
{
    vStopAndCloseCdAudio();
}

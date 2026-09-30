/*
 * protos.h - prototypes of the functions called across translation units, grouped by the file
 * that defines them.  Each file also declares its own file-local prototypes (and some extra
 * cross-file ones) in its "externs not (yet) in globals.h / protos.h" block.  The parameter names
 * are those of the definitions.
 */
#ifndef PROTOS_H
#define PROTOS_H

/* online.c - DirectPlay netplay */
void vOnlineShowError(LPCSTR szText);
int iFindOnlinePlayerSlotById(DPID dpid);
int iFindFreeOnlinePlayerSlot(void);
int iAllocOnlinePlayerSlot(DPID dpid);
void vDpSendToAllGuaranteed(LPVOID pData, DWORD dwSize);
void vDpSendToPlayerGuaranteed(DPID dpidTo, LPVOID pData, DWORD dwSize);
void vDpSendToAll(LPVOID pData, DWORD dwSize);
void vOnlineBroadcastInput(DWORD dwInput);
int iOnlineProcessReceivedMessages(void);
void vSendOnlineChatMessage(char *szText);
int iEnumDirectPlayServiceProviders(void);
int iInitOnline(void);
int iOnlineCloseSession(void);
int iOnlineEnumSessions(void);
void vOnlineAnnouncePlayerAndSetTitle(char *szSessionName);
int iOnlineHostSession(void);
int iOnlineJoinSession(int iSession);
BOOL CALLBACK iOnlineDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void vSpawnOnlineDialog(HINSTANCE hInstance, HWND hWnd);
void vGetLocalHostIpAddress(void);

/* main.c - memory, sound, KGT file loading */
void vMemzero(void *pAddress, int iSize);
void vSetupDsound(void);
void vReleaseDsound(void);
void vStopAndResetAllWavs(void);
void vStopMidi(void);
void vStopCdAudio2(void);
void vHandleLoadingSound(kgtSound *pSound);
void vHandleStoppingAllWavs(int iSoundKind);
void vFreeKgtCore(kgt_core *pCore);
int bReadKgtCore(kgt_core *pCore, HANDLE hFile);
int iClearCharacterFile(int iPlayerIdx);
int iOpenCharacterFile(int iPlayerIdx, int iCharacterIdx);
int iClearKgtSystemFile(void);
int iOpenKgtSystemFile(char *szFile);
int iClearDemoFile(void);
int iOpenDemoFile(int iDemo);
int iClearStageFile(void);
int iOpenStageFile(int iStage);

/* config.c - warning box and ini files */
void vSpawnTaskModalWithWarning(char *szText);

/* debug.c - on-screen debug log */
int iSetDebugInfo(char *szText, int iColor);

/* cdaudio.c (CD tracks through MCI) and midi.c (MIDI through the MCI sequencer) */
void vStopAndCloseCdAudio(void);
void vStopCdAudio1(void);
void vStopAndCloseMidi(void);
void vLoadsoundFromDisc(int iTrack, int bLoop);
void vWriteAndPlayMidFile(kgtSound *pSound);

/* dsutil.c - DirectSound helpers (DirectX SDK sample) */
kgtWav *kgtwBuildWav(LPDIRECTSOUND pDirectSound, void *pWaveData, int iConcurrent);
void vFreeKgtWav(kgtWav *pWav);
LPDIRECTSOUNDBUFFER kgtdxReturnSoundBuffer(kgtWav *pWav);
int iStopAndResetWav(kgtWav *pWav);

#endif

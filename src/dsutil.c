/*
 * dsutil.c - DirectSound helpers, adapted from the DirectX SDK sample dsutil.c (the SDK function
 * each one comes from is named above it).
 *
 * The SDK loads waves from resources; here they come from wave files in memory (the sounds of the
 * KGT data files: main.c calls kgtwBuildWav for each WAV sound), or from files on disk (the *File*
 * variants, not called by the game).  kgtWav is the SDK's SNDOBJ: a set of iAlloc static buffers of
 * the same sound (the first one filled, the others duplicates) so that it can overlap itself.
 *
 * A wave file is RIFF: "RIFF", size, "WAVE", then chunks (four-character type, DWORD length, data,
 * padded to an even length); iWalkWavFile finds the "fmt " chunk (the WAVEFORMATEX) and the "data"
 * chunk (the samples).
 */
#include "kgt.h"

static const char c_szWAV[] = "WAV";    /* the SDK's resource type; unused, but it is in .rdata (see docs/MATCHING.md) */

/* ---- externs not (yet) in globals.h / protos.h ---- */
#define ghWavFileAlloc BSS(HGLOBAL, ghWavFileAlloc)  /* 0x424790: wave file read by bGetWavFileInformation_Debug */
LPDIRECTSOUNDBUFFER dxReloadSoundBuffer(LPDIRECTSOUND pDirectSound, LPCSTR pWaveData);
BOOL bGetWavFileInformation_Debug(HMODULE hModule, LPCSTR szFileName, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes);
BOOL bGetWavFileInformation(HMODULE hModule, LPCSTR szFileName, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes);
kgtWav *kgtwBuildWavFromFile(LPDIRECTSOUND pDirectSound, LPCSTR szFileName, int iConcurrent);
BOOL bGetWavInformation(HMODULE hModule, LPCSTR pWaveData, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes);
LPDIRECTSOUNDBUFFER dxGetSoundBufferInterface(LPDIRECTSOUND pDirectSound, LPCSTR pWaveData);
BOOL bKgtWavPlay(kgtWav *pWav, DWORD dwPlayFlags);
BOOL iWriteFromSoundAlloc(LPDIRECTSOUNDBUFFER pBuffer, BYTE *pSamples, DWORD dwSampleBytes);
BOOL iWalkWavFile(void *pRiff, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes);
/* ---- */

/*
 * DSLoadSoundBuffer: creates a static sound buffer (default controls: volume, pan, frequency)
 * holding a wave file that is in memory.  The same code as dxGetSoundBufferInterface; this copy is
 * used by kgtwBuildWavFromFile, which passes it the file name (so it fails there, the name not
 * being RIFF data).
 * pDirectSound: the DirectSound object; pWaveData: the wave file in memory.
 * Returns the buffer, or NULL.
 */
LPDIRECTSOUNDBUFFER dxReloadSoundBuffer(LPDIRECTSOUND pDirectSound, LPCSTR pWaveData)
{
    LPDIRECTSOUNDBUFFER pBuffer = NULL;
    DSBUFFERDESC bufferDesc = {0};
    BYTE *pSamples;

    /* format and size from the RIFF chunks */
    if (bGetWavInformation(NULL, pWaveData, &bufferDesc.lpwfxFormat, &pSamples, &bufferDesc.dwBufferBytes)) {
        bufferDesc.dwSize = sizeof(bufferDesc);
        bufferDesc.dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRLDEFAULT;

        /* create it and copy the samples in */
        if (SUCCEEDED(IDirectSound_CreateSoundBuffer(pDirectSound, &bufferDesc, &pBuffer, NULL))) {
            if (!iWriteFromSoundAlloc(pBuffer, pSamples, bufferDesc.dwBufferBytes)) {
                IDirectSoundBuffer_Release(pBuffer);
                pBuffer = NULL;
            }
        } else {
            pBuffer = NULL;
        }
    }
    return pBuffer;
}

/*
 * A debugging version of bGetWavFileInformation (not called): reads a wave file into a global
 * memory block kept in ghWavFileAlloc, showing its size in a message box first, and finds its
 * format and samples.
 * hModule: unused (the SDK's resource module); szFileName: the file; ppWaveHeader, ppSamples,
 * pdwSampleBytes: receive the WAVEFORMATEX, the samples and their size (pointers into the block).
 * Returns TRUE if the file was read and is a wave file.
 * Globals: reads ghWnd; changes ghWavFileAlloc.
 */
BOOL bGetWavFileInformation_Debug(HMODULE hModule, LPCSTR szFileName, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes)
{
    HANDLE hFile;
    DWORD dwFileSize;
    DWORD dwBytesRead;
    char szMsg[256];

    /* open the file (OPEN_ALWAYS: a missing file is created empty) and show its size */
    ghWavFileAlloc = NULL;
    dwBytesRead = 0;
    hFile = CreateFileA(szFileName, GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    dwFileSize = GetFileSize(hFile, NULL);
    sprintf(szMsg, "wave size\201u%d\201v", (int)dwFileSize);  /* wave size「%d」 */
    MessageBoxA(ghWnd, szMsg, "\212m\224F", MB_TASKMODAL | MB_ICONEXCLAMATION);  /* 確認 (confirmation) */
    /* read it whole */
    ghWavFileAlloc = GlobalAlloc(GMEM_FIXED, dwFileSize);
    if (!ghWavFileAlloc)
        goto failed;
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, ghWavFileAlloc, dwFileSize, &dwBytesRead, NULL))
        goto failed;
    CloseHandle(hFile);
    return iWalkWavFile(ghWavFileAlloc, ppWaveHeader, ppSamples, pdwSampleBytes) != 0;

failed:
    MessageBoxA(ghWnd, "DirectSound File Open Error", "\202\276\202\313\201H", MB_ICONHAND);  /* だね？ ("isn't it?") */
    GlobalFree(ghWavFileAlloc);
    CloseHandle(hFile);
    return FALSE;
}

/*
 * Reads a wave file into a new global memory block (never freed: the samples stay in use) and
 * finds its format and samples (not called by the game).  First it passes szFileName to GlobalFree
 * if it is not NULL (sic; harmless for a pointer that is not a global memory handle).
 * hModule: unused; szFileName: the file; ppWaveHeader, ppSamples, pdwSampleBytes: receive the
 * WAVEFORMATEX, the samples and their size (pointers into the block).
 * Returns TRUE if the file was read and is a wave file.
 * Globals: reads ghWnd.
 */
BOOL bGetWavFileInformation(HMODULE hModule, LPCSTR szFileName, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes)
{
    HANDLE hFile;
    DWORD dwFileSize;
    HGLOBAL hFileData;
    DWORD dwBytesRead;

    /* (the main branch reads szFileName here through hModule's address - the next stack slot - to
       get VC6's register allocation; that is undefined behaviour for a modern compiler, so this
       branch names the parameter) */
    if ((HGLOBAL)szFileName)
        GlobalFree((HGLOBAL)szFileName);
    /* open (OPEN_ALWAYS: a missing file is created empty) and read the whole file */
    dwBytesRead = 0;
    hFile = CreateFileA(szFileName, GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    dwFileSize = GetFileSize(hFile, NULL);
    hFileData = GlobalAlloc(GMEM_FIXED, dwFileSize);
    if (!hFileData)
        goto failed;
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (!ReadFile(hFile, hFileData, dwFileSize, &dwBytesRead, NULL))
        goto failed;
    CloseHandle(hFile);
    return iWalkWavFile(hFileData, ppWaveHeader, ppSamples, pdwSampleBytes) != 0;

failed:
    MessageBoxA(ghWnd, "DirectSound File Open Error", "\202\276\202\313\201H", MB_ICONHAND);  /* だね？ ("isn't it?") */
    GlobalFree(hFileData);
    CloseHandle(hFile);
    return FALSE;
    /* (the main branch has an unreachable ReadFile here, only to steer VC6's stack slots) */
}

/*
 * SndObjCreate, loading from a file (not called by the game): builds a kgtWav with iConcurrent
 * buffers of the wave file szFileName.  The buffers are made by dxReloadSoundBuffer from the file
 * *name* (not the data read), so this variant does not work.
 * pDirectSound: the DirectSound object; szFileName: the file; iConcurrent: number of buffers (at
 * least 1).
 * Returns the new kgtWav (LocalAlloc), or NULL.
 * Globals: changes ghWavFileAlloc (set to NULL, then freed).
 */
kgtWav *kgtwBuildWavFromFile(LPDIRECTSOUND pDirectSound, LPCSTR szFileName, int iConcurrent)
{
    kgtWav *pWav = NULL;
    LPWAVEFORMATEX pWaveHeader;
    BYTE *pSamples;
    DWORD dwSampleBytes;

    ghWavFileAlloc = NULL;
    if (bGetWavFileInformation(NULL, szFileName, &pWaveHeader, &pSamples, &dwSampleBytes)) {
        if (iConcurrent < 1)
            iConcurrent = 1;

        /* the 0x10-byte header followed by the buffer pointers */
        if ((pWav = (kgtWav *)LocalAlloc(LPTR, 0x10 + iConcurrent * sizeof(LPDIRECTSOUNDBUFFER))) != NULL) {
            int iBuffer;

            pWav->iAlloc = iConcurrent;
            pWav->pbWaveData = pSamples;
            pWav->cbWaveSize = dwSampleBytes;
            pWav->pBuffers[0] = dxReloadSoundBuffer(pDirectSound, szFileName);

            /* the other buffers share the first one's memory; a new buffer if that fails */
            for (iBuffer = 1; iBuffer < pWav->iAlloc; iBuffer++) {
                if (FAILED(IDirectSound_DuplicateSoundBuffer(pDirectSound, pWav->pBuffers[0], &pWav->pBuffers[iBuffer]))) {
                    pWav->pBuffers[iBuffer] = dxReloadSoundBuffer(pDirectSound, szFileName);
                    if (!pWav->pBuffers[iBuffer]) {
                        vFreeKgtWav(pWav);
                        pWav = NULL;
                        break;
                    }
                }
            }
        }
        GlobalFree(ghWavFileAlloc);
    }
    return pWav;
}

/*
 * DSGetWaveResource: the "resource" is a wave file already in memory.
 * hModule: unused; pWaveData: the wave file; ppWaveHeader, ppSamples, pdwSampleBytes: receive the
 * WAVEFORMATEX, the samples and their size.
 * Returns TRUE if it is a wave file with both chunks.
 */
BOOL bGetWavInformation(HMODULE hModule, LPCSTR pWaveData, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes)
{
    return iWalkWavFile((void *)pWaveData, ppWaveHeader, ppSamples, pdwSampleBytes) != 0;
}

/*
 * DSLoadSoundBuffer: creates a static sound buffer (default controls: volume, pan, frequency)
 * holding a wave file that is in memory (used by kgtwBuildWav).
 * pDirectSound: the DirectSound object; pWaveData: the wave file in memory.
 * Returns the buffer, or NULL.
 */
LPDIRECTSOUNDBUFFER dxGetSoundBufferInterface(LPDIRECTSOUND pDirectSound, LPCSTR pWaveData)
{
    LPDIRECTSOUNDBUFFER pBuffer = NULL;
    DSBUFFERDESC bufferDesc = {0};
    BYTE *pSamples;

    /* format and size from the RIFF chunks */
    if (bGetWavInformation(NULL, pWaveData, &bufferDesc.lpwfxFormat, &pSamples, &bufferDesc.dwBufferBytes)) {
        bufferDesc.dwSize = sizeof(bufferDesc);
        bufferDesc.dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRLDEFAULT;

        /* create it and copy the samples in */
        if (SUCCEEDED(IDirectSound_CreateSoundBuffer(pDirectSound, &bufferDesc, &pBuffer, NULL))) {
            if (!iWriteFromSoundAlloc(pBuffer, pSamples, bufferDesc.dwBufferBytes)) {
                IDirectSoundBuffer_Release(pBuffer);
                pBuffer = NULL;
            }
        } else {
            pBuffer = NULL;
        }
    }
    return pBuffer;
}

/*
 * SndObjCreate: builds a kgtWav with iConcurrent buffers of a wave file in memory (a WAV sound of
 * a KGT data file; the game always asks for 1 buffer).  The kgtWav remembers the samples so that a
 * lost buffer can be refilled (kgtdxReturnSoundBuffer).
 * pDirectSound: the DirectSound object; pWaveData: the wave file (it must stay in memory);
 * iConcurrent: number of buffers (at least 1).
 * Returns the new kgtWav (LocalAlloc), or NULL.
 * Globals: frees ghWavFileAlloc (normally NULL here; a leftover of the file variant).
 */
kgtWav *kgtwBuildWav(LPDIRECTSOUND pDirectSound, void *pWaveData, int iConcurrent)
{
    kgtWav *pWav = NULL;
    LPWAVEFORMATEX pWaveHeader;
    BYTE *pSamples;
    DWORD dwSampleBytes;

    if (bGetWavInformation(NULL, (LPCSTR)pWaveData, &pWaveHeader, &pSamples, &dwSampleBytes)) {
        if (iConcurrent < 1)
            iConcurrent = 1;

        /* the 0x10-byte header followed by the buffer pointers */
        if ((pWav = (kgtWav *)LocalAlloc(LPTR, 0x10 + iConcurrent * sizeof(LPDIRECTSOUNDBUFFER))) != NULL) {
            int iBuffer;

            pWav->iAlloc = iConcurrent;
            pWav->pbWaveData = pSamples;
            pWav->cbWaveSize = dwSampleBytes;
            pWav->pBuffers[0] = dxGetSoundBufferInterface(pDirectSound, (LPCSTR)pWaveData);

            /* the other buffers share the first one's memory; a new buffer if that fails */
            for (iBuffer = 1; iBuffer < pWav->iAlloc; iBuffer++) {
                if (FAILED(IDirectSound_DuplicateSoundBuffer(pDirectSound, pWav->pBuffers[0], &pWav->pBuffers[iBuffer]))) {
                    pWav->pBuffers[iBuffer] = dxGetSoundBufferInterface(pDirectSound, (LPCSTR)pWaveData);
                    if (!pWav->pBuffers[iBuffer]) {
                        vFreeKgtWav(pWav);
                        pWav = NULL;
                        break;
                    }
                }
            }
        }
        GlobalFree(ghWavFileAlloc);
    }
    return pWav;
}

/*
 * SndObjDestroy: releases the buffers of a kgtWav and frees it.
 * pWav: the kgtWav (NULL is allowed).
 */
void vFreeKgtWav(kgtWav *pWav)
{
    if (pWav) {
        int iBuffer;

        for (iBuffer = 0; iBuffer < pWav->iAlloc; iBuffer++) {
            if (pWav->pBuffers[iBuffer]) {
                IDirectSoundBuffer_Release(pWav->pBuffers[iBuffer]);
                pWav->pBuffers[iBuffer] = NULL;
            }
        }
        LocalFree((HANDLE)pWav);
    }
}

/*
 * SndObjGetFreeBuffer: returns a buffer of the kgtWav to play: the current one if it is not
 * playing; else, with several buffers, the next one (stopped and rewound if it was playing too);
 * with a single buffer, none.  A lost buffer (DSBSTATUS_BUFFERLOST) is restored and refilled from
 * the kgtWav's samples.
 * pWav: the kgtWav (NULL is allowed).
 * Returns the buffer, or NULL.
 */
LPDIRECTSOUNDBUFFER kgtdxReturnSoundBuffer(kgtWav *pWav)
{
    LPDIRECTSOUNDBUFFER pBuffer;

    if (pWav == NULL)
        return NULL;

    if ((pBuffer = pWav->pBuffers[pWav->iCurrent])) {
        HRESULT hr;
        DWORD dwStatus;

        hr = IDirectSoundBuffer_GetStatus(pBuffer, &dwStatus);

        if (FAILED(hr))
            dwStatus = 0;

        /* busy: take the next buffer round-robin, cutting it off if needed */
        if ((dwStatus & DSBSTATUS_PLAYING) == DSBSTATUS_PLAYING) {
            if (pWav->iAlloc > 1) {
                if (++pWav->iCurrent >= pWav->iAlloc)
                    pWav->iCurrent = 0;

                pBuffer = pWav->pBuffers[pWav->iCurrent];
                hr = IDirectSoundBuffer_GetStatus(pBuffer, &dwStatus);

                if (SUCCEEDED(hr) && (dwStatus & DSBSTATUS_PLAYING) == DSBSTATUS_PLAYING) {
                    IDirectSoundBuffer_Stop(pBuffer);
                    IDirectSoundBuffer_SetCurrentPosition(pBuffer, 0);
                }
            } else {
                pBuffer = NULL;
            }
        }

        /* lost (e.g. after another application took the sound card): restore and refill */
        if (pBuffer && (dwStatus & DSBSTATUS_BUFFERLOST)) {
            if (FAILED(IDirectSoundBuffer_Restore(pBuffer)) ||
                !iWriteFromSoundAlloc(pBuffer, pWav->pbWaveData, pWav->cbWaveSize)) {
                pBuffer = NULL;
            }
        }
    }
    return pBuffer;
}

/*
 * SndObjPlay (not called; main.c plays the buffer of kgtdxReturnSoundBuffer itself): plays the
 * kgtWav once, or looped (DSBPLAY_LOOPING) if it has a single buffer.
 * pWav: the kgtWav; dwPlayFlags: DSBPLAY_* flags.
 * Returns TRUE if a buffer was started.
 */
BOOL bKgtWavPlay(kgtWav *pWav, DWORD dwPlayFlags)
{
    BOOL bResult = FALSE;

    if (pWav == NULL)
        return FALSE;

    if (!(dwPlayFlags & DSBPLAY_LOOPING) || pWav->iAlloc == 1) {
        LPDIRECTSOUNDBUFFER pBuffer = kgtdxReturnSoundBuffer(pWav);
        if (pBuffer != NULL)
            bResult = SUCCEEDED(IDirectSoundBuffer_Play(pBuffer, 0, 0, dwPlayFlags));
    }
    return bResult;
}

/*
 * SndObjStop: stops all buffers of the kgtWav and rewinds them.
 * pWav: the kgtWav (NULL is allowed).
 * Returns TRUE, or FALSE for NULL.
 */
int iStopAndResetWav(kgtWav *pWav)
{
    int iBuffer;

    if (pWav == NULL)
        return FALSE;

    for (iBuffer = 0; iBuffer < pWav->iAlloc; iBuffer++) {
        IDirectSoundBuffer_Stop(pWav->pBuffers[iBuffer]);
        IDirectSoundBuffer_SetCurrentPosition(pWav->pBuffers[iBuffer], 0);
    }
    return TRUE;
}

/*
 * DSFillSoundBuffer: copies samples into a sound buffer (Lock may return the region in two parts).
 * pBuffer: the buffer; pSamples, dwSampleBytes: the samples.
 * Returns TRUE on success.
 */
BOOL iWriteFromSoundAlloc(LPDIRECTSOUNDBUFFER pBuffer, BYTE *pSamples, DWORD dwSampleBytes)
{
    if (pBuffer && pSamples && dwSampleBytes) {
        LPVOID pLock1, pLock2;
        DWORD dwSize1, dwSize2;

        if (SUCCEEDED(IDirectSoundBuffer_Lock(pBuffer, 0, dwSampleBytes, &pLock1, &dwSize1, &pLock2, &dwSize2, 0))) {
            CopyMemory(pLock1, pSamples, dwSize1);

            if (0 != dwSize2)
                CopyMemory(pLock2, pSamples + dwSize1, dwSize2);

            IDirectSoundBuffer_Unlock(pBuffer, pLock1, dwSize1, pLock2, dwSize2);
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * DSParseWaveResource: walks the chunks of a RIFF WAVE file in memory and returns pointers to its
 * "fmt " chunk (the WAVEFORMATEX) and "data" chunk (samples and size).  An output pointer may be
 * NULL when it is not wanted; the walk stops as soon as all wanted ones are found.
 * pRiff: the file; ppWaveHeader, ppSamples, pdwSampleBytes: the outputs (set to NULL / 0 first).
 * Returns TRUE when all wanted chunks were found.
 */
BOOL iWalkWavFile(void *pRiff, WAVEFORMATEX **ppWaveHeader, BYTE **ppSamples, DWORD *pdwSampleBytes)
{
    DWORD *pdwCur;
    DWORD *pdwEnd;
    DWORD dwRiff;
    DWORD dwType;
    DWORD dwLength;

    if (ppWaveHeader)
        *ppWaveHeader = NULL;

    if (ppSamples)
        *ppSamples = NULL;

    if (pdwSampleBytes)
        *pdwSampleBytes = 0;

    /* RIFF header: "RIFF", length of what follows, form type "WAVE" */
    pdwCur = (DWORD *)pRiff;
    dwRiff = *pdwCur++;
    dwLength = *pdwCur++;
    dwType = *pdwCur++;

    if (dwRiff != mmioFOURCC('R', 'I', 'F', 'F'))
        goto notWave;      /* not even RIFF */

    if (dwType != mmioFOURCC('W', 'A', 'V', 'E'))
        goto notWave;      /* not a WAV */

    /* the chunks end dwLength bytes after the length field (4 of them were the form type) */
    pdwEnd = (DWORD *)((BYTE *)pdwCur + dwLength - 4);

    while (pdwCur < pdwEnd) {
        /* chunk header: type, length */
        dwType = *pdwCur++;
        dwLength = *pdwCur++;

        switch (dwType) {
        case mmioFOURCC('f', 'm', 't', ' '):
            if (ppWaveHeader && !*ppWaveHeader) {
                if (dwLength < sizeof(WAVEFORMAT))
                    goto notWave;      /* not a WAV */

                *ppWaveHeader = (WAVEFORMATEX *)pdwCur;

                if ((!ppSamples || *ppSamples) &&
                    (!pdwSampleBytes || *pdwSampleBytes)) {
                    return TRUE;
                }
            }
            break;

        case mmioFOURCC('d', 'a', 't', 'a'):
            if ((ppSamples && !*ppSamples) ||
                (pdwSampleBytes && !*pdwSampleBytes)) {
                if (ppSamples)
                    *ppSamples = (LPBYTE)pdwCur;

                if (pdwSampleBytes)
                    *pdwSampleBytes = dwLength;

                if (!ppWaveHeader || *ppWaveHeader)
                    return TRUE;
            }
            break;
        }

        /* next chunk: data padded to an even length */
        pdwCur = (DWORD *)((BYTE *)pdwCur + ((dwLength + 1) & ~1));
    }

notWave:
    return FALSE;
}

/*
 * compress.c - the RLE/LZ byte compressor of the KGT data files (0x413d40-0x4141e0).
 *
 * The game only decompresses (iKgtDecompress, used by engine.c to unpack 8-bit images into
 * gpGlobalMemoryAlloc before drawing them); the compressor is the editor's and is linked in but
 * never called.
 *
 * Stream format: a sequence of ops, each starting with a control byte (op << 6 | len):
 *   op 0: len zero bytes
 *   op 1: len literal bytes follow
 *   op 2: len copies of the following byte
 *   op 3: copy len bytes from `offset` bytes back in the output (may overlap); the offset follows
 * Lengths: len 1-0x3f is in the control byte; with len 0 the next byte gives 0x40-0x13e as
 * (byte + 0x3f), and a 0 there means three more bytes (little endian) give len - 0x13f.
 * Offsets: one byte 1-0xff; the encoder would write 0 then (offset - 0x100) as two bytes for more,
 * but its search window is 0xff bytes, so that form never occurs (and the decoder reads it
 * differently, see iKgtDecompress).
 */
#include "kgt.h"

/* ---- externs not in globals.h / protos.h ---- */
extern BYTE *gpKgtCompressOut;              /* 0x424e30: output pointer of the compressor */
extern int giKgtCompressOutSize;            /* 0x424788: bytes written so far by the compressor */

void vKgtCompressEmitOp(int iOp, int iLen);
void vKgtCompressEmitOffset(int iOffset);
int iKgtCompressFlushLiterals(BYTE *pDst, BYTE *pSrc, int iPos, int iLiteralStart);
int iKgtCompressTryRun(BYTE *pDst, BYTE *pSrc, int *pPos, int iEnd);
int iKgtCompressTryBackref(BYTE *pDst, BYTE *pSrc, int *pPos, int iEnd);
int iKgtCompress(BYTE *pDst, BYTE *pSrc, int iSrcLen);
int iKgtDecompress(BYTE *pDst, BYTE *pSrc, int iSrcLen);
/* ---- */

/*
 * Writes the control byte(s) of an op: (iOp << 6 | iLen) for iLen < 0x40; (iOp << 6), iLen - 0x3f
 * for iLen < 0x13f; else (iOp << 6), 0 and iLen - 0x13f in three bytes, little endian.
 * iOp: 0-3 (see the file header); iLen: the op's length (> 0).
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize.
 */
void vKgtCompressEmitOp(int iOp, int iLen)
{
    if (iLen < 0x40) {
        /* short form: length in the control byte */
        *gpKgtCompressOut++ = (BYTE)(iOp << 6) + (BYTE)iLen;
        giKgtCompressOutSize++;
    } else if (iLen < 0x13f) {
        /* one extension byte: 1-0xff = length - 0x3f */
        *gpKgtCompressOut++ = (BYTE)(iOp << 6);
        *gpKgtCompressOut++ = (BYTE)(iLen - 0x3f);
        giKgtCompressOutSize += 2;
    } else {
        /* extension byte 0, then length - 0x13f in 24 bits */
        int iExtra = iLen - 0x13f;
        *gpKgtCompressOut++ = (BYTE)(iOp << 6);
        *gpKgtCompressOut++ = 0;
        *gpKgtCompressOut++ = (BYTE)iExtra;
        *gpKgtCompressOut++ = (BYTE)(iExtra >> 8);
        *gpKgtCompressOut++ = (BYTE)(iExtra >> 16);
        giKgtCompressOutSize += 5;
    }
}

/*
 * Writes the distance of a back-reference (op 3): one byte for 1-0xff, else 0 and
 * (iOffset - 0x100) in two bytes, little endian (never needed, see the file header).
 * iOffset: distance back from the current output position.
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize.
 */
void vKgtCompressEmitOffset(int iOffset)
{
    if (iOffset < 0x100) {
        *gpKgtCompressOut++ = iOffset;
        giKgtCompressOutSize++;
    } else {
        int iExtra = iOffset - 0x100;
        *gpKgtCompressOut++ = 0;
        *gpKgtCompressOut++ = (BYTE)iExtra;
        *gpKgtCompressOut++ = (BYTE)(iExtra >> 8);
        giKgtCompressOutSize += 3;
    }
}

/*
 * Emits the pending literal bytes pSrc[iLiteralStart .. iPos - 1] as one op 1, if there are any.
 * pDst: unused (the output goes through gpKgtCompressOut); pSrc: the input; iPos: current input
 * position; iLiteralStart: first byte not yet emitted.
 * Returns 0.
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize.
 */
int iKgtCompressFlushLiterals(BYTE *pDst, BYTE *pSrc, int iPos, int iLiteralStart)
{
    int iLen = iPos - iLiteralStart;

    pSrc += iLiteralStart;
    if (iPos == iLiteralStart)
        return 0;
    vKgtCompressEmitOp(1, iLen);
    giKgtCompressOutSize += iLen;
    while (iLen--)
        *gpKgtCompressOut++ = *pSrc++;
    return 0;
}

/*
 * Measures the run of equal bytes starting at pSrc[*pPos] (up to iEnd).  With pDst != NULL it
 * also emits it - op 0 for a run of zeros, op 2 plus the byte otherwise - and advances *pPos.
 * pDst: NULL to only measure, else any non-NULL pointer; pSrc: the input; pPos: current input
 * position; iEnd: input length.
 * Returns the run length (at least 1).
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize (when emitting).
 */
int iKgtCompressTryRun(BYTE *pDst, BYTE *pSrc, int *pPos, int iEnd)
{
    int iPos = *pPos;
    int iLen = 0;
    BYTE cValue = pSrc[iPos];

    /* count the bytes equal to the first one */
    for (iLen = 0; iLen < iEnd - iPos; iLen++) {
        if (cValue != pSrc[iPos + iLen])
            break;
    }
    if (pDst) {
        if (cValue) {
            /* op 2: byte run */
            vKgtCompressEmitOp(2, iLen);
            *gpKgtCompressOut++ = cValue;
            giKgtCompressOutSize++;
        } else {
            /* op 0: zero run (no data byte) */
            vKgtCompressEmitOp(0, iLen);
        }
        *pPos += iLen;
    }
    return iLen;
}

/*
 * Finds the longest earlier match for the bytes at pSrc[*pPos]: every start from 0xff bytes back
 * to 2 bytes back is tried (distance 1 is a run, see iKgtCompressTryRun), the matches may overlap
 * the current position, and the first of equally long matches wins.  With pDst != NULL it also
 * emits the match as op 3 (length, distance) and advances *pPos.
 * pDst: NULL to only measure, else any non-NULL pointer; pSrc: the input; pPos: current input
 * position; iEnd: input length.
 * Returns the match length (0 when there is none).
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize (when emitting).
 */
int iKgtCompressTryBackref(BYTE *pDst, BYTE *pSrc, int *pPos, int iEnd)
{
    int iBestLen = 0;
    int iPos = *pPos;
    int iStart, iCandidate, iLen, iBestPos;

    /* the search window: at most 0xff bytes back */
    iStart = iPos - 0xff;
    if (iStart < 0)
        iStart = 0;
    if (iStart == iPos)
        return 0;
    for (iCandidate = iStart; iCandidate < iPos - 1; iCandidate++) {
        BYTE *pCur = pSrc + iPos;
        BYTE *pCandidate = pSrc + iCandidate;
        /* length of the match at this candidate */
        for (iLen = 0; iPos + iLen < iEnd; iLen++) {
            if (pCandidate[iLen] != pCur[iLen])
                break;
        }
        if (iBestLen < iLen) {
            iBestLen = iLen;
            iBestPos = iCandidate;
        }
    }
    if (pDst) {
        /* op 3: length, then the distance back */
        vKgtCompressEmitOp(3, iBestLen);
        vKgtCompressEmitOffset(iPos - iBestPos);
        *pPos += iBestLen;
    }
    return iBestLen;
}

/*
 * Compresses iSrcLen bytes (the editor's encoder; not called by the game).  Greedy: at each
 * position the longer of the byte run and the back-reference is emitted when it is longer than 2
 * bytes (after flushing the pending literals); otherwise the byte stays in the literal run.
 * pDst: output buffer; pSrc: input; iSrcLen: input length.
 * Returns the compressed size.
 * Globals: changes gpKgtCompressOut, giKgtCompressOutSize.
 */
int iKgtCompress(BYTE *pDst, BYTE *pSrc, int iSrcLen)
{
    int iLiteralStart = 0;
    int iPos = 0;
    int iRunLen, iBackrefLen;

    giKgtCompressOutSize = 0;
    gpKgtCompressOut = pDst;
    do {
        /* measure both candidates at this position */
        iRunLen = iKgtCompressTryRun(NULL, pSrc, &iPos, iSrcLen);
        iBackrefLen = iKgtCompressTryBackref(NULL, pSrc, &iPos, iSrcLen);
        if (iRunLen > iBackrefLen) {
            if (iRunLen > 2) {
                /* emit the run */
                iKgtCompressFlushLiterals(pDst, pSrc, iPos, iLiteralStart);
                iKgtCompressTryRun(pDst, pSrc, &iPos, iSrcLen);
                iLiteralStart = iPos;
            } else {
                /* too short: keep the byte as a literal */
                iPos++;
            }
        } else {
            if (iBackrefLen > 2) {
                /* emit the back-reference */
                iKgtCompressFlushLiterals(pDst, pSrc, iPos, iLiteralStart);
                iKgtCompressTryBackref(pDst, pSrc, &iPos, iSrcLen);
                iLiteralStart = iPos;
            } else {
                iPos++;
            }
        }
    } while (iPos < iSrcLen);
    /* the trailing literals (the test is always true) */
    if (iPos <= iSrcLen)
        iKgtCompressFlushLiterals(pDst, pSrc, iPos, iLiteralStart);
    return giKgtCompressOutSize;
}

/*
 * Decompresses a stream of iSrcLen bytes into pDst (no bounds check on the output).  pSrc always
 * points at the last byte consumed; each op ends with pSrc++ onto the next control byte.
 * pDst: output buffer (big enough for the unpacked data); pSrc: compressed data; iSrcLen: its
 * length.
 * Returns 0.
 */
int iKgtDecompress(BYTE *pDst, BYTE *pSrc, int iSrcLen)
{
    BYTE *pEnd = pSrc + iSrcLen;
    BYTE *pCopy;
    unsigned int uLen, uOffset, uOp;
    BYTE cValue;

    do {
        /* control byte: op in bits 6-7, length in bits 0-5 */
        uOp = *pSrc >> 6;
        uLen = *pSrc & 0x3f;
        if (uLen == 0) {
            /* extended length: one byte (+ 0x3f), or 0 and three bytes (+ 0x13f) */
            uLen = *++pSrc;
            if (uLen) {
                uLen += 0x3f;
            } else {
                pSrc++;
                uLen = *pSrc++;
                uLen += *pSrc++ << 8;
                uLen += (*pSrc << 16) + 0x13f;
            }
        }
        switch (uOp) {
        case 0:
            /* zero run */
            while (uLen--)
                *pDst++ = 0;
            break;
        case 1:
            /* literal bytes */
            while (uLen--)
                *pDst++ = *++pSrc;
            break;
        case 2:
            /* byte run */
            cValue = *++pSrc;
            while (uLen--)
                *pDst++ = cValue;
            break;
        case 3:
            /* back-reference: distance byte, 0 = long form */
            uOffset = *++pSrc;
            if (uOffset == 0) {
                uOp = (*++pSrc + 1) << 8;     /* sic: doesn't match vKgtCompressEmitOffset (lo, hi of off - 0x100) */
                if (0) goto boundary1;       /* matching: dead goto; the label keeps "uOffset = uOp" a separate copy before the pSrc++ */
            boundary1:
                uOffset = uOp;
                pSrc++;
            }
            /* copy byte by byte, so an overlapping source repeats a pattern */
            pCopy = pDst - uOffset;
            while (uLen--)
                *pDst++ = *pCopy++;
            break;
        }
        /* on to the next control byte */
        pSrc++;
    } while (pEnd > pSrc);
    return 0;
}

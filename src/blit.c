/*
 * blit.c - the hand-written 8-bit (palette index) blitters, 0x401000-0x402470 in the original: the
 * first object of the link, written in assembler (formerly asm/blit.asm), translated to C with the
 * same behaviour, quirks included (each is marked "NB").  None of them is referenced by the game's
 * C code except copy_ppvBits_to_lpSurface (main.c: the 8-bit frame to the locked DirectDraw
 * surface); the game draws its images into 16-bit surfaces with its own routines (engine.c).  The
 * routine names are those of the Ghidra database (docs/GHIDRA_CHANGES.md), chosen from what the
 * code does.
 *
 * Conventions of all routines:
 *   * cdecl, nothing returned (GetPixel8 returns the pixel).
 *   * pixels are bytes (palette indices); a pitch is the distance in bytes from one row to the next;
 *     the key is the transparent index: source pixels equal to it are not drawn.  Key and colour
 *     arguments are ints of which only the low byte is used.
 *   * a remap table is 256 bytes, new index = table[index] (palette/colour changes); a blend table
 *     is 64 KB, result = table[dst * 256 + src] (translucency); a shade table is 256 bytes applied
 *     to the destination pixel, dst = table[dst] (shadows).
 *   * nothing is clipped: the caller passes an already clipped rectangle.
 *   * counts (widths, heights, rows) are unsigned 32-bit and are counted down after the first
 *     step, as the original's dec/jne loops did: where a count must be at least 1, 0 means 2^32.
 *   * pointer steps are 32-bit (the original is a 32-bit program): a pitch minus a width is
 *     computed modulo 2^32 and added as a signed offset.
 * The original's inner loops handle 4 pixels per step in two (or three) copies - one run while
 * the pixels are transparent, one while they are drawn - which only saves jumps; the C loops
 * visit the pixels in the same order and read/write them in the same order, which is all that
 * matters for the result (also when source and destination overlap).  Where the original reads
 * four source pixels as one dword before drawing any of them, so does the C code.
 */
#include <stdint.h>
#include <string.h>
#include "blit.h"

/* ---- the blitters' uninitialized data (0x4203b0-0x4213c3, the start of the original's .bss;
 * file-local, as in the assembler object) ---- */
static int32_t col_offsets[512];            /* 0x4203b0: affine blits: source offset differences between neighbouring columns */
static const unsigned char *row_ptrs[512];  /* 0x420bb0: affine blits: source pointer of each destination row */
static uint32_t count_x;                    /* 0x4213b0: column / group counter */
/* 0x4213b4 tmp_4213b4: a dword that nothing references (left out) */
static uint32_t count_y;                   /* 0x4213b8: row counter */
[[maybe_unused]] static uint32_t width_px;  /* 0x4213bc: destination width (written, never read) */
static uint32_t base_offset;                /* 0x4213c0: source offset of the first destination pixel */
/* ---- */

/* pointer + a 32-bit value taken modulo 2^32 (the original's add of a register to a pointer) */
#define PTR_ADD(p, n) ((p) + (int32_t)(uint32_t)(n))

/* unaligned 16/32-bit accesses */
static uint32_t uRead32(const unsigned char *p)
{
    uint32_t u;

    memcpy(&u, p, 4);
    return u;
}

static void vWrite32(unsigned char *p, uint32_t u)
{
    memcpy(p, &u, 4);
}

static uint16_t uRead16(const void *p)
{
    uint16_t u;

    memcpy(&u, p, 2);
    return u;
}

static void vWrite16(unsigned char *p, uint16_t u)
{
    memcpy(p, &u, 2);
}

/* a byte in all four bytes of a dword */
static uint32_t uByte4(int iColour)
{
    return (uint32_t)(unsigned char)iColour * 0x01010101u;
}

/*
 * Blit8to16_ColorKey (0x401000): converts an 8-bit image to 16 bits through a palette, two pixels
 * per step, skipping key pixels.
 * pDst: 16-bit destination; pSrc: 8-bit source; iPairs: pixel pairs per row (at least 1); iRows: rows
 * (at least 1); iDstPitch: in pixels; iSrcPitch: in bytes; pPalette: WORD[256], 16-bit colour of each
 * index; iKey: transparent index.
 * NB the end-of-row skips are 2 * (iDstPitch - iPairs) bytes and iSrcPitch - iPairs bytes although a
 * row advances 2 * iPairs pixels: each row starts iDstPitch + iPairs pixels / iSrcPitch + iPairs
 * bytes after the previous one, so the pitches must be passed to suit.
 */
void Blit8to16_ColorKey(unsigned short *pDst, const unsigned char *pSrc, int iPairs, int iRows,
                        int iDstPitch, int iSrcPitch, const unsigned short *pPalette, int iKey)
{
    unsigned char *pbDst = (unsigned char *)pDst;
    const unsigned char *pbPal = (const unsigned char *)pPalette;
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)(((uint32_t)iDstPitch - (uint32_t)iPairs) << 1);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iPairs);
    uint32_t uRows = (uint32_t)iRows;

    do {
        uint32_t uPairs = (uint32_t)iPairs;

        do {
            if (pSrc[0] != bKey)
                vWrite16(pbDst, uRead16(pbPal + pSrc[0] * 2));
            if (pSrc[1] != bKey)
                vWrite16(pbDst + 2, uRead16(pbPal + pSrc[1] * 2));
            pbDst += 4;
            pSrc += 2;
        } while (--uPairs != 0);
        pSrc += iSrcSkip;
        pbDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8to16_ColorKey_Unrolled (0x401086): the same conversion, four pixels per step.
 * pDst: 16-bit destination; pSrc: 8-bit source; iWidth: pixels per row; iRows: rows (at least 1);
 * iDstPitch: in pixels; iSrcPitch: in bytes; pPalette: WORD[256]; iKey: transparent index.
 * NB the iWidth & 3 leftover pixels of each row are neither drawn nor stepped over (the original's
 * leftover loop only counts them down), so only widths that are multiples of 4 work: otherwise each
 * row starts iWidth & 3 pixels / bytes before the pitch says.
 */
void Blit8to16_ColorKey_Unrolled(unsigned short *pDst, const unsigned char *pSrc, int iWidth, int iRows,
                                 int iDstPitch, int iSrcPitch, const unsigned short *pPalette, int iKey)
{
    unsigned char *pbDst = (unsigned char *)pDst;
    const unsigned char *pbPal = (const unsigned char *)pPalette;
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);   /* in pixels */
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iRows;

    do {
        uint32_t uGroups = (uint32_t)iWidth >> 2;
        int i;

        for (; uGroups != 0; uGroups--) {
            for (i = 0; i < 4; i++)
                if (pSrc[i] != bKey)
                    vWrite16(pbDst + i * 2, uRead16(pbPal + pSrc[i] * 2));
            pbDst += 8;
            pSrc += 4;
        }
        /* NB (iWidth & 3) leftover pixels: skipped without stepping over them */
        pSrc += iSrcSkip;
        pbDst += iDstSkip;          /* added twice: pixels -> bytes */
        pbDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * FillRect8 (0x401150): fills a rectangle with one index (see also FillRect8_B).
 * pDst: first pixel; iWidth: bytes per row; iHeight: rows (at least 1); iPitch: destination pitch;
 * iColour: the index.
 * Each row is written as iWidth / 4 dwords, then iWidth & 3 bytes.
 */
void FillRect8(unsigned char *pDst, int iWidth, int iHeight, int iPitch, int iColour)
{
    uint32_t uFill = uByte4(iColour);
    uint32_t uDwords = (uint32_t)iWidth >> 2;
    uint32_t uBytes = (uint32_t)iWidth & 3;
    int32_t iSkip = (int32_t)((uint32_t)iPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;
    uint32_t u;

    do {
        for (u = uDwords; u != 0; u--) {
            vWrite32(pDst, uFill);
            pDst += 4;
        }
        for (u = uBytes; u != 0; u--)
            *pDst++ = (unsigned char)iColour;
        pDst += iSkip;
    } while (--uRows != 0);
}

/*
 * copy_ppvBits_to_lpSurface (0x401191; the name suggests a copy from a DIB section's bits to a
 * locked DirectDraw surface): copies a rectangle of bytes.  main.c copies the 8-bit frame with it.
 * pDst: destination; pSrc: source; iWidth: bytes per row; iRows: rows (at least 1); iDstPitch,
 * iSrcPitch: pitches.
 * Each row is copied forwards as iWidth / 4 dwords, then iWidth & 3 bytes (rep movsd / rep movsb:
 * each dword is read before it is written, which decides the result when the buffers overlap).
 */
void copy_ppvBits_to_lpSurface(void *pDst, void *pSrc, int iWidth, int iRows, int iDstPitch, int iSrcPitch)
{
    unsigned char *pbDst = (unsigned char *)pDst;
    const unsigned char *pbSrc = (const unsigned char *)pSrc;
    uint32_t uDwords = (uint32_t)iWidth >> 2;
    uint32_t uBytes = (uint32_t)iWidth & 3;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iRows;
    uint32_t u;

    do {
        for (u = uDwords; u != 0; u--) {
            vWrite32(pbDst, uRead32(pbSrc));
            pbDst += 4;
            pbSrc += 4;
        }
        for (u = uBytes; u != 0; u--)
            *pbDst++ = *pbSrc++;
        pbSrc += iSrcSkip;
        pbDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * GetPixel8 (0x4011D0): returns the byte at pPixel.
 * (The original returned it in al and left the rest of eax as it was.)
 */
unsigned char GetPixel8(const unsigned char *pPixel)
{
    return *pPixel;
}

/*
 * PutPixel8 (0x4011E4): stores the index iColour at pPixel.
 */
void PutPixel8(unsigned char *pPixel, int iColour)
{
    *pPixel = (unsigned char)iColour;
}

/* ---- affine blits ----
 * An affine blit draws a rotated / scaled image: destination pixel (i, j) (column i, row j) shows
 * the source pixel at (u, v) = (u0 + j * duRow + i * duCol, v0 + j * dvRow + i * dvCol), in 24.8 fixed
 * point, i.e. at the source offset (v >> 8) * srcPitch + (u >> 8).  The source offset of every
 * column relative to column 0 is computed once (col_offsets holds the differences between
 * neighbouring columns) and every row starts from its own source pointer (row_ptrs), so all rows
 * walk the same column pattern:
 *   pixel (i, j) = row_ptrs[j] + col_offsets[0] + ... + col_offsets[i].
 * The width and height must be 1..512 (the tables; the original overran them otherwise).  All sums
 * are modulo 2^32 and the source is not bounds-checked (except the leftover columns of
 * AffineBlit8_ColorKey_Signed).
 */

/* the 24.8 -> integer shifts: unsigned (u >> 8) and signed (magnitude shifted: rounds towards 0) */
static uint32_t uShr8(uint32_t u)
{
    return u >> 8;
}

static uint32_t uShr8Signed(uint32_t u)
{
    if (u >= 0x80000000u)
        return 0u - ((0u - u) >> 8);    /* neg, shr 8, neg */
    return u >> 8;
}

/*
 * Builds col_offsets, row_ptrs and base_offset for the affine blits (the setup shared by all five,
 * which differ only in the shift).  Globals: changes width_px, base_offset, count_x (ends 0),
 * col_offsets[0..iWidth-1], row_ptrs[0..iHeight-1].
 * The row offset is (v >> 8) * srcPitch: the original multiplies with mul in the first two
 * routines and imul in the other three, which give the same low 32 bits.
 */
static void vAffineSetup(const unsigned char *pSrc, int iWidth, int iHeight, int iSrcPitch,
                         int iU0, int iV0, int iDuRow, int iDvRow, int iDuCol, int iDvCol,
                         uint32_t (*pfnShr8)(uint32_t))
{
    uint32_t uPitch = (uint32_t)iSrcPitch;
    uint32_t u, v, uOffset, uPrev, uRows;
    int32_t *pOut;
    const unsigned char **ppRow;

    width_px = (uint32_t)iWidth;
    base_offset = pfnShr8((uint32_t)iV0) * uPitch + pfnShr8((uint32_t)iU0);

    /* col_offsets[i] = offset(column i) - offset(column i - 1), offsets relative to base_offset */
    count_x = (uint32_t)iWidth;
    u = (uint32_t)iU0;
    v = (uint32_t)iV0;
    pOut = col_offsets;
    uPrev = 0;
    do {
        uOffset = pfnShr8(v) * uPitch + pfnShr8(u) - base_offset;
        *pOut++ = (int32_t)(uOffset - uPrev);
        uPrev = uOffset;
        u += (uint32_t)iDuCol;
        v += (uint32_t)iDvCol;
    } while (--count_x != 0);

    /* row_ptrs[j] = src + (v >> 8) * srcPitch + (u >> 8), stepping (u, v) by the row steps */
    uRows = (uint32_t)iHeight;
    u = (uint32_t)iU0;
    v = (uint32_t)iV0;
    ppRow = row_ptrs;
    do {
        *ppRow++ = PTR_ADD(pSrc, pfnShr8(v) * uPitch + pfnShr8(u));
        u += (uint32_t)iDuRow;
        v += (uint32_t)iDvRow;
    } while (--uRows != 0);
}

/*
 * AffineBlit8_ColorKey (0x4011FB): draws a rotated / scaled image (an affine mapping of the source,
 * see above) with a transparent key.
 * pDst: first destination pixel; pSrc: source image; iWidth, iHeight: destination size (1..512);
 * iDstPitch, iSrcPitch: pitches; iKey: transparent index; iU0, iV0: source x, y of the first
 * destination pixel (24.8); iDuRow, iDvRow: source step per destination row; iDuCol, iDvCol: source
 * step per destination column.
 * Coordinates are truncated with unsigned shifts (u >> 8).
 * Globals: changes col_offsets, row_ptrs, count_x, count_y (both end 0), width_px, base_offset.
 */
void AffineBlit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                          int iDuRow, int iDvRow, int iDuCol, int iDvCol)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip;
    const unsigned char **ppRow;

    vAffineSetup(pSrc, iWidth, iHeight, iSrcPitch, iU0, iV0, iDuRow, iDvRow, iDuCol, iDvCol, uShr8);

    iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    count_y = (uint32_t)iHeight;
    ppRow = row_ptrs;
    do {
        const unsigned char *pPixel = *ppRow;
        const int32_t *pCol = col_offsets;
        uint32_t uCols = (uint32_t)iWidth;

        do {
            pPixel += *pCol++;
            if (*pPixel != bKey)
                *pDst = *pPixel;
            pDst++;
        } while (--uCols != 0);
        pDst += iDstSkip;
        ppRow++;
    } while (--count_y != 0);
}

/*
 * AffineBlit8_ColorKey_Signed (0x401377): AffineBlit8_ColorKey for signed coordinates, with a
 * source range.
 * pDst: first destination pixel; pSrc: source image (also the lowest valid source address);
 * pSrcEnd: end of the source (first invalid address); iWidth, iHeight: destination size (1..512);
 * iDstPitch, iSrcPitch: pitches; iKey: transparent index; iU0, iV0: start position (24.8, signed);
 * iDuRow, iDvRow: step per destination row; iDuCol, iDvCol: step per destination column.
 * Coordinates >= 0x80000000 are negative: they are shifted as neg, shr 8, neg (rounding towards
 * zero).
 * NB only the leftover columns (the last iWidth & 3 of each row, which the original draws one at a
 * time after the groups of 4) check that the source pointer lies in [pSrc, pSrcEnd) (an unsigned
 * address comparison); the other columns are not checked.
 * Globals: as AffineBlit8_ColorKey.
 */
void AffineBlit8_ColorKey_Signed(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pSrcEnd,
                                 int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey,
                                 int iU0, int iV0, int iDuRow, int iDvRow, int iDuCol, int iDvCol)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip;
    const unsigned char **ppRow;

    vAffineSetup(pSrc, iWidth, iHeight, iSrcPitch, iU0, iV0, iDuRow, iDvRow, iDuCol, iDvCol, uShr8Signed);

    iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    count_y = (uint32_t)iHeight;
    ppRow = row_ptrs;
    do {
        const unsigned char *pPixel = *ppRow;
        const int32_t *pCol = col_offsets;
        uint32_t uGroups = (uint32_t)iWidth >> 2;
        uint32_t uCols;

        /* groups of 4 columns: not checked */
        for (uCols = uGroups * 4; uCols != 0; uCols--) {
            pPixel += *pCol++;
            if (*pPixel != bKey)
                *pDst = *pPixel;
            pDst++;
        }
        /* leftover columns: drawn only if pSrc <= pointer < pSrcEnd */
        for (uCols = (uint32_t)iWidth & 3; uCols != 0; uCols--) {
            pPixel += *pCol++;
            if ((uintptr_t)pPixel >= (uintptr_t)pSrc && (uintptr_t)pPixel < (uintptr_t)pSrcEnd
                && *pPixel != bKey)
                *pDst = *pPixel;
            pDst++;
        }
        pDst += iDstSkip;
        ppRow++;
    } while (--count_y != 0);
}

/*
 * AffineBlit8_Blend (0x40155B): AffineBlit8_ColorKey drawing translucently through a blend table.
 * pDst ... iDvCol: as AffineBlit8_ColorKey; pBlend: 64 KB table, dst = blend[dst * 256 + src].
 * Globals: as AffineBlit8_ColorKey (count_x is also the column counter while drawing).
 */
void AffineBlit8_Blend(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                       int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pBlend)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    const unsigned char **ppRow;

    vAffineSetup(pSrc, iWidth, iHeight, iSrcPitch, iU0, iV0, iDuRow, iDvRow, iDuCol, iDvCol, uShr8);

    count_y = (uint32_t)iHeight;
    ppRow = row_ptrs;
    do {
        const unsigned char *pPixel = *ppRow;
        const int32_t *pCol = col_offsets;

        count_x = (uint32_t)iWidth;
        do {
            pPixel += *pCol++;
            if (*pPixel != bKey)
                *pDst = pBlend[*pDst * 256 + *pPixel];
            pDst++;
        } while (--count_x != 0);
        pDst += iDstSkip;
        ppRow++;
    } while (--count_y != 0);
}

/*
 * AffineBlit8_Remap (0x40170D): AffineBlit8_ColorKey drawing each pixel through a remap table.
 * pDst ... iDvCol: as AffineBlit8_ColorKey; pRemap: 256-byte table, dst = remap[src].
 * Globals: as AffineBlit8_Blend.
 */
void AffineBlit8_Remap(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                       int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pRemap)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    const unsigned char **ppRow;

    vAffineSetup(pSrc, iWidth, iHeight, iSrcPitch, iU0, iV0, iDuRow, iDvRow, iDuCol, iDvCol, uShr8);

    count_y = (uint32_t)iHeight;
    ppRow = row_ptrs;
    do {
        const unsigned char *pPixel = *ppRow;
        const int32_t *pCol = col_offsets;

        count_x = (uint32_t)iWidth;
        do {
            pPixel += *pCol++;
            if (*pPixel != bKey)
                *pDst = pRemap[*pPixel];
            pDst++;
        } while (--count_x != 0);
        pDst += iDstSkip;
        ppRow++;
    } while (--count_y != 0);
}

/*
 * AffineBlit8_ShadeDst (0x4018B3): the source only gives a shape: where the (affinely mapped)
 * source pixel is not the key, the destination pixel is passed through a shade table (shadows).
 * pDst ... iDvCol: as AffineBlit8_ColorKey; pShade: 256-byte table, dst = shade[dst].
 * Globals: as AffineBlit8_Blend.
 */
void AffineBlit8_ShadeDst(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                          int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pShade)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    const unsigned char **ppRow;

    vAffineSetup(pSrc, iWidth, iHeight, iSrcPitch, iU0, iV0, iDuRow, iDvRow, iDuCol, iDvCol, uShr8);

    count_y = (uint32_t)iHeight;
    ppRow = row_ptrs;
    do {
        const unsigned char *pPixel = *ppRow;
        const int32_t *pCol = col_offsets;

        count_x = (uint32_t)iWidth;
        do {
            pPixel += *pCol++;
            if (*pPixel != bKey)
                *pDst = pShade[*pDst];
            pDst++;
        } while (--count_x != 0);
        pDst += iDstSkip;
        ppRow++;
    } while (--count_y != 0);
}

/*
 * FillDword8 (0x401A53): stores the index iColour in the four bytes at pDst.
 * NB one dword only: the original loads ecx with 0x10000 but its stosd has no rep prefix (a 256 KB
 * fill was probably intended).
 */
void FillDword8(unsigned char *pDst, int iColour)
{
    vWrite32(pDst, uByte4(iColour));
}

/*
 * RleDecodeRow8_Remap (0x401A7A): unpacks run-length coded pixels through a remap table.
 * pDst: destination; pSrc: coded data; pRemap: 256-byte table applied to every pixel;
 * iThreshold: T: a byte below T is one literal pixel; a byte b >= T is a run: the next byte is
 * repeated ((b - (T - 2)) & 0xff) + 1 times, i.e. b - T + 3 times (NB modulo 256: b - T = 254 or 255
 * gives a run of 1 or 2 pixels); iCount: pixels to write (at least 1).
 * NB a run is not cut at the end: the count must end exactly after a code, else the count goes
 * past 0 and the decoding runs on (as in the original).
 */
void RleDecodeRow8_Remap(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                         int iThreshold, int iCount)
{
    unsigned char bThreshold = (unsigned char)iThreshold;
    unsigned char bRunBias = (unsigned char)(bThreshold - 2);
    uint32_t uLeft = (uint32_t)iCount;

    for (;;) {
        unsigned char bCode = *pSrc++;

        if (bCode < bThreshold) {
            *pDst++ = pRemap[bCode];                /* literal */
        } else {
            unsigned char bRunMinus1 = (unsigned char)(bCode - bRunBias);
            unsigned char bPixel;

            uLeft -= bRunMinus1;                    /* the run's last pixel is counted below */
            bPixel = pRemap[*pSrc++];
            memset(pDst, bPixel, (size_t)bRunMinus1 + 1);
            pDst += (size_t)bRunMinus1 + 1;
        }
        if (--uLeft == 0)
            break;
    }
}

/*
 * RleBlit8_ColorKey (0x401AF6): draws a run-length coded image through a remap table, with a key.
 * pDst: first destination pixel; pSrc: coded data, one stream for the whole image (runs may cross
 * rows); pRemap: 256-byte table applied to every pixel; iThreshold: T: a byte below T is one pixel;
 * a byte b >= T is a run: the next byte is drawn b - T + 1 times; iWidth: pixels per row (at least
 * 1); iHeight: rows (at least 1); iDstPitch: destination pitch; iSrcPitch: not used (the stream has
 * no padding); iKey: compared with the remapped pixel, which is then not drawn.
 */
void RleBlit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                       int iThreshold, int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey)
{
    unsigned char bThreshold = (unsigned char)iThreshold;
    unsigned char bKey = (unsigned char)iKey;
    unsigned char bPixel = bKey;                    /* current (remapped) pixel */
    unsigned int uRepeats = 0;                      /* further repeats of bPixel pending */
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    (void)iSrcPitch;
    do {
        uint32_t uCols = (uint32_t)iWidth;

        do {
            if (uRepeats != 0) {
                uRepeats--;                         /* in a run: the same pixel again */
            } else {
                unsigned char bCode = *pSrc++;

                if (bCode >= bThreshold) {          /* run: the pixel follows */
                    uRepeats = (unsigned char)(bCode - bThreshold);
                    bCode = *pSrc++;
                }
                bPixel = pRemap[bCode];
            }
            if (bPixel != bKey)
                *pDst = bPixel;
            pDst++;
        } while (--uCols != 0);
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * RleBlit8_ColorKey_FlipX (0x401B66): RleBlit8_ColorKey mirrored horizontally: pDst points at the
 * rightmost pixel of the first row and each row is drawn right to left (the end-of-row step is
 * iDstPitch + iWidth).  Same arguments.
 */
void RleBlit8_ColorKey_FlipX(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                             int iThreshold, int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey)
{
    unsigned char bThreshold = (unsigned char)iThreshold;
    unsigned char bKey = (unsigned char)iKey;
    unsigned char bPixel = bKey;
    unsigned int uRepeats = 0;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch + (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    (void)iSrcPitch;
    do {
        uint32_t uCols = (uint32_t)iWidth;

        do {
            if (uRepeats != 0) {
                uRepeats--;
            } else {
                unsigned char bCode = *pSrc++;

                if (bCode >= bThreshold) {
                    uRepeats = (unsigned char)(bCode - bThreshold);
                    bCode = *pSrc++;
                }
                bPixel = pRemap[bCode];
            }
            if (bPixel != bKey)
                *pDst = bPixel;
            pDst--;                                 /* right to left */
        } while (--uCols != 0);
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * The "all four transparent" test of Blit8_ColorKey, Blit8_ColorKey_FlipX and Blit8_ShadeDst.
 * Their groups of four source pixels are read as one dword; while the previous group ended with a
 * transparent pixel (and at the start of each row) the dword is first compared with a pattern that
 * is the key in all four bytes, and a match skips the whole group.  NB the leftover loops load
 * their pixel into the pattern's low byte (al), so after the first leftover pixel of the call the
 * pattern is (last leftover byte, key, key, key): a later group whose first pixel equals that byte
 * and whose other three are the key is skipped entirely, its first pixel not drawn although it is
 * not the key.  (The byte is the source pixel for the first two, the shaded destination pixel for
 * Blit8_ShadeDst.)  The pattern lives for the whole call, across rows.
 */
#define PATTERN_LOW(uPattern, b) (((uPattern) & 0xffffff00u) | (unsigned char)(b))

/*
 * Blit8_ColorKey (0x401BD6): copies an image, skipping key pixels.
 * pDst: first destination pixel; pSrc: source; iWidth: pixels per row; iHeight: rows (at least 1);
 * iDstPitch, iSrcPitch: pitches; iKey: transparent index.
 * The groups of four pixels are read as one dword before any of them is drawn.  NB see the "all
 * four transparent" test above: after a leftover pixel, a non-key pixel can be left undrawn.
 */
void Blit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                    int iDstPitch, int iSrcPitch, int iKey)
{
    unsigned char bKey = (unsigned char)iKey;
    uint32_t uPattern = uByte4(iKey);               /* eax: key * 01010101h, al spoiled later */
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        int bSkipping = 1;                          /* the previous pixel was transparent */
        uint32_t u;

        for (u = (uint32_t)iWidth >> 2; u != 0; u--) {
            uint32_t uPixels = uRead32(pSrc);       /* 4 source pixels */
            int i;

            if (!bSkipping || uPixels != uPattern) {
                bSkipping = (unsigned char)(uPixels >> 24) == bKey;
                for (i = 0; i < 4; i++, uPixels >>= 8)
                    if ((unsigned char)uPixels != bKey)
                        pDst[i] = (unsigned char)uPixels;
            }
            pDst += 4;
            pSrc += 4;
        }
        for (u = (uint32_t)iWidth & 3; u != 0; u--) {
            unsigned char bPixel = *pSrc++;

            uPattern = PATTERN_LOW(uPattern, bPixel);
            pDst++;
            if (bPixel != bKey)
                pDst[-1] = bPixel;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_ColorKey_FlipX (0x401C87): Blit8_ColorKey mirrored horizontally: pDst points at the rightmost
 * pixel of the first destination row, rows are drawn right to left and the end-of-row step is
 * iDstPitch + iWidth.  Same arguments, same NB about the "all four transparent" test.
 */
void Blit8_ColorKey_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey)
{
    unsigned char bKey = (unsigned char)iKey;
    uint32_t uPattern = uByte4(iKey);
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch + (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        int bSkipping = 1;
        uint32_t u;

        for (u = (uint32_t)iWidth >> 2; u != 0; u--) {
            uint32_t uPixels = uRead32(pSrc);       /* source pixels 0-3 go to dst, dst-1, dst-2, dst-3 */
            int i;

            if (!bSkipping || uPixels != uPattern) {
                bSkipping = (unsigned char)(uPixels >> 24) == bKey;
                for (i = 0; i < 4; i++, uPixels >>= 8)
                    if ((unsigned char)uPixels != bKey)
                        pDst[-i] = (unsigned char)uPixels;
            }
            pDst -= 4;
            pSrc += 4;
        }
        for (u = (uint32_t)iWidth & 3; u != 0; u--) {
            unsigned char bPixel = *pSrc++;

            uPattern = PATTERN_LOW(uPattern, bPixel);
            pDst--;
            if (bPixel != bKey)
                pDst[1] = bPixel;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_Blend_FlipX (0x401D37): mirrored translucent copy (see Blit8_Blend).
 * pDst: rightmost pixel of the first destination row; pSrc: source; iWidth: pixels per row;
 * iHeight: rows (at least 1); iDstPitch: pitch (the end-of-row step is iDstPitch + iWidth);
 * iSrcPitch: source pitch; iKey: transparent index; pBlend: 64 KB table, dst = blend[dst * 256 + src].
 * NB the leftover pixels (the last iWidth & 3 of each row) land two pixels too far left: the
 * original decrements the destination pointer before using [edi-1] (compare Blit8_ColorKey_FlipX).
 */
void Blit8_Blend_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pBlend)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch + (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;
        int i;

        for (u = (uint32_t)iWidth >> 2; u != 0; u--) {
            for (i = 0; i < 4; i++)
                if (pSrc[i] != bKey)
                    pDst[-i] = pBlend[pDst[-i] * 256 + pSrc[i]];
            pDst -= 4;
            pSrc += 4;
        }
        for (u = (uint32_t)iWidth & 3; u != 0; u--) {
            unsigned char bPixel;

            pSrc++;
            pDst--;
            bPixel = pSrc[-1];
            if (bPixel != bKey)
                pDst[-1] = pBlend[pDst[-1] * 256 + bPixel];     /* NB sic: should be pDst[1] */
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_Silhouette (0x401E0B): draws the shape of an image in one colour: every non-key source pixel
 * becomes the colour.
 * pDst ... iKey: as Blit8_ColorKey; iColour: the index drawn.
 */
void Blit8_Silhouette(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                      int iDstPitch, int iSrcPitch, int iKey, int iColour)
{
    unsigned char bKey = (unsigned char)iKey;
    unsigned char bColour = (unsigned char)iColour;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;

        for (u = (uint32_t)iWidth; u != 0; u--) {
            if (*pSrc != bKey)
                *pDst = bColour;
            pSrc++;
            pDst++;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_Silhouette_FlipX (0x401EAD): Blit8_Silhouette mirrored horizontally (pDst = rightmost pixel
 * of the first row, rows drawn right to left, end-of-row step iDstPitch + iWidth).  Same arguments.
 */
void Blit8_Silhouette_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                            int iDstPitch, int iSrcPitch, int iKey, int iColour)
{
    unsigned char bKey = (unsigned char)iKey;
    unsigned char bColour = (unsigned char)iColour;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch + (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;

        for (u = (uint32_t)iWidth; u != 0; u--) {
            if (*pSrc != bKey)
                *pDst = bColour;
            pSrc++;
            pDst--;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * FillRect8_B (0x401F4C): a second rectangle fill, same arguments and result as FillRect8 (pDst,
 * iWidth, iHeight, iPitch, iColour).
 */
void FillRect8_B(unsigned char *pDst, int iWidth, int iHeight, int iPitch, int iColour)
{
    uint32_t uFill = uByte4(iColour);
    int32_t iSkip = (int32_t)((uint32_t)iPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;
    uint32_t u;

    do {
        for (u = (uint32_t)iWidth >> 2; u != 0; u--) {
            vWrite32(pDst, uFill);
            pDst += 4;
        }
        for (u = (uint32_t)iWidth & 3; u != 0; u--)
            *pDst++ = (unsigned char)iColour;
        pDst += iSkip;
    } while (--uRows != 0);
}

/*
 * FlipVertical8_Remap (0x401F9A): flips an image upside down in place, remapping every pixel it
 * moves.
 * pImage: the image; iWidth: bytes per row (at least 1), also the pitch; iHeight: rows, at least 2
 * (the original's loops run 2^32 times otherwise); pRemap: 256-byte table.
 * Rows j and iHeight-1-j are swapped for j < iHeight / 2; NB with an odd height the middle row is
 * not remapped.
 */
void FlipVertical8_Remap(unsigned char *pImage, int iWidth, int iHeight, const unsigned char *pRemap)
{
    unsigned char *pTop = pImage;
    /* the original adds the width height - 1 times (a loop instruction: 2^32 times for height 1) */
    unsigned char *pBottom = PTR_ADD(pImage, ((uint32_t)iHeight - 1) * (uint32_t)iWidth);
    uint32_t uPairs = (uint32_t)iHeight >> 1;

    do {
        unsigned char *pT = pTop, *pB = pBottom;
        uint32_t u = (uint32_t)iWidth;

        do {
            unsigned char bBottom = *pB;
            unsigned char bTop = *pT;

            *pB = pRemap[bTop];
            *pT = pRemap[bBottom];
            pB++;
            pT++;
        } while (--u != 0);
        pTop = PTR_ADD(pTop, iWidth);               /* top row down */
        pBottom = PTR_ADD(pBottom, 0u - (uint32_t)iWidth);  /* bottom row up */
    } while (--uPairs != 0);
}

/*
 * RemapBytes8 (0x401FE7): remaps a buffer in place.
 * pBuffer: the bytes; iCount: how many (at least 1); pRemap: 256-byte table.
 */
void RemapBytes8(unsigned char *pBuffer, int iCount, const unsigned char *pRemap)
{
    uint32_t u = (uint32_t)iCount;

    do {
        *pBuffer = pRemap[*pBuffer];
        pBuffer++;
    } while (--u != 0);
}

/*
 * CopyRows640 (0x40200C): copies whole 640-byte rows between two 640-pitch buffers (an 8-bit 640x480
 * frame), forwards, one dword at a time.
 * pSrc: source (NB it comes first); pDst: destination; iUnused: not used; iRows: rows (at least 1).
 */
void CopyRows640(const unsigned char *pSrc, unsigned char *pDst, int iUnused, int iRows)
{
    uint32_t uRows = (uint32_t)iRows;

    (void)iUnused;
    do {
        int i;

        for (i = 0; i < 640; i += 4)
            vWrite32(pDst + i, uRead32(pSrc + i));
        pSrc += 640;
        pDst += 640;
    } while (--uRows != 0);
}

/*
 * StretchDouble320x240To640x480 (0x40203E): doubles a 320x240 8-bit image (pitch 320) into a 640x480
 * one (pitch 640): every source pixel becomes a 2x2 block.
 * pDst: 640x480 destination; pSrc: 320x240 source; iUnused: not used (the original decrements its
 * argument slot by 640).
 * Each pair of source pixels a, b becomes the dword a a b b, stored first in the row below, then in
 * this row.
 */
void StretchDouble320x240To640x480(unsigned char *pDst, const unsigned char *pSrc, int iUnused)
{
    int iRow, iPair;

    (void)iUnused;
    for (iRow = 0; iRow < 240; iRow++) {
        for (iPair = 0; iPair < 160; iPair++) {
            uint32_t uA = pSrc[0], uB = pSrc[1];
            uint32_t uBlock = uA | (uA << 8) | (uB << 16) | (uB << 24);

            pSrc += 2;
            vWrite32(pDst + 640, uBlock);   /* the row below */
            vWrite32(pDst, uBlock);         /* this row */
            pDst += 4;
        }
        pDst += 640;                        /* skip the row already written */
    }
}

/*
 * Clear640x480 (0x40208C): fills a 640x480 8-bit frame (307200 bytes, 76800 dwords) with the dword
 * uValue (normally one index in all four bytes).
 */
void Clear640x480(unsigned char *pFrame, unsigned int uValue)
{
    int i;

    for (i = 0; i < 76800; i++) {
        vWrite32(pFrame, (uint32_t)uValue);
        pFrame += 4;
    }
}

/*
 * Blit8_ShadeDst (0x4020A8): the source only gives a shape: where the source pixel is not the key,
 * the destination pixel is passed through a shade table (shadows).
 * pDst ... iKey: as Blit8_ColorKey; pShade: 256-byte table, dst = shade[dst].
 * The groups of four source pixels are read as one dword, as in Blit8_ColorKey.  NB see the "all
 * four transparent" test above Blit8_ColorKey: here the leftover loop puts each shaded destination
 * pixel into the pattern's low byte (not when the leftover source pixel is the key).
 */
void Blit8_ShadeDst(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                    int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pShade)
{
    unsigned char bKey = (unsigned char)iKey;
    uint32_t uPattern = uByte4(iKey);
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        int bSkipping = 1;
        uint32_t u;

        for (u = (uint32_t)iWidth >> 2; u != 0; u--) {
            uint32_t uPixels = uRead32(pSrc);
            int i;

            if (!bSkipping || uPixels != uPattern) {
                bSkipping = (unsigned char)(uPixels >> 24) == bKey;
                for (i = 0; i < 4; i++, uPixels >>= 8)
                    if ((unsigned char)uPixels != bKey)
                        pDst[i] = pShade[pDst[i]];
            }
            pDst += 4;
            pSrc += 4;
        }
        for (u = (uint32_t)iWidth & 3; u != 0; u--) {
            pSrc++;
            pDst++;
            if (pSrc[-1] != bKey) {
                unsigned char bShaded = pShade[pDst[-1]];

                uPattern = PATTERN_LOW(uPattern, bShaded);
                pDst[-1] = bShaded;
            }
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_Remap (0x402185): Blit8_ColorKey drawing each pixel through a remap table.
 * pDst ... iKey: as Blit8_ColorKey; pRemap: 256-byte table, dst = remap[src].
 */
void Blit8_Remap(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                 int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pRemap)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;

        for (u = (uint32_t)iWidth; u != 0; u--) {
            if (*pSrc != bKey)
                *pDst = pRemap[*pSrc];
            pSrc++;
            pDst++;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_Blend (0x40224C): translucent copy through a blend table.
 * pDst ... iKey: as Blit8_ColorKey; pBlend: 64 KB table, dst = blend[dst * 256 + src].
 */
void Blit8_Blend(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                 int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pBlend)
{
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;

        for (u = (uint32_t)iWidth; u != 0; u--) {
            if (*pSrc != bKey)
                *pDst = pBlend[*pDst * 256 + *pSrc];
            pSrc++;
            pDst++;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

/*
 * Blit8_ColorKey_ShadeKey (0x40231F): Blit8_ColorKey with a second special index that shades the
 * destination instead of being drawn (a sprite with a built-in shadow).
 * pDst ... iSrcPitch: as Blit8_ColorKey; iShadeKey: source pixels of this index shade the
 * destination, dst = shade[dst] (tested first: it wins if it equals iKey); iKey: source pixels of
 * this index are not drawn; pShade: 256-byte table.  Other pixels are copied.
 */
void Blit8_ColorKey_ShadeKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                             int iDstPitch, int iSrcPitch, int iShadeKey, int iKey, const unsigned char *pShade)
{
    unsigned char bShadeKey = (unsigned char)iShadeKey;
    unsigned char bKey = (unsigned char)iKey;
    int32_t iDstSkip = (int32_t)((uint32_t)iDstPitch - (uint32_t)iWidth);
    int32_t iSrcSkip = (int32_t)((uint32_t)iSrcPitch - (uint32_t)iWidth);
    uint32_t uRows = (uint32_t)iHeight;

    do {
        uint32_t u;

        for (u = (uint32_t)iWidth; u != 0; u--) {
            unsigned char bPixel = *pSrc;

            if (bPixel == bShadeKey)
                *pDst = pShade[*pDst];
            else if (bPixel != bKey)
                *pDst = bPixel;
            pSrc++;
            pDst++;
        }
        pSrc += iSrcSkip;
        pDst += iDstSkip;
    } while (--uRows != 0);
}

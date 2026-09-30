/*
 * blit.h - the 8-bit (palette index) blitters of blit.c (0x401000-0x402470 in the original, which
 * wrote them in assembler).  All are cdecl.  Of these only copy_ppvBits_to_lpSurface is called by
 * the game (main.c); the rest were linked in but never referenced.
 *
 * Conventions: pixels are bytes (palette indices); a pitch is the distance in bytes from one row to
 * the next (it may be negative); a key/colour argument is an int of which only the low byte is used.
 * Counts are taken as unsigned 32-bit values and counted down after the first step, so a count of 0
 * where the description says "at least 1" means 2^32 (as in the original: it crashes).
 */
#ifndef BLIT_H
#define BLIT_H

/* 8 -> 16 bit conversion through a palette */
void Blit8to16_ColorKey(unsigned short *pDst, const unsigned char *pSrc, int iPairs, int iRows,
                        int iDstPitch, int iSrcPitch, const unsigned short *pPalette, int iKey);
void Blit8to16_ColorKey_Unrolled(unsigned short *pDst, const unsigned char *pSrc, int iWidth, int iRows,
                                 int iDstPitch, int iSrcPitch, const unsigned short *pPalette, int iKey);

/* fills, copies, single pixels */
void FillRect8(unsigned char *pDst, int iWidth, int iHeight, int iPitch, int iColour);
void copy_ppvBits_to_lpSurface(void *pDst, void *pSrc, int iWidth, int iRows, int iDstPitch, int iSrcPitch);
unsigned char GetPixel8(const unsigned char *pPixel);
void PutPixel8(unsigned char *pPixel, int iColour);

/* rotated / scaled images (24.8 fixed-point source coordinates) */
void AffineBlit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                          int iDuRow, int iDvRow, int iDuCol, int iDvCol);
void AffineBlit8_ColorKey_Signed(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pSrcEnd,
                                 int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey,
                                 int iU0, int iV0, int iDuRow, int iDvRow, int iDuCol, int iDvCol);
void AffineBlit8_Blend(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                       int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pBlend);
void AffineBlit8_Remap(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                       int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pRemap);
void AffineBlit8_ShadeDst(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey, int iU0, int iV0,
                          int iDuRow, int iDvRow, int iDuCol, int iDvCol, const unsigned char *pShade);

void FillDword8(unsigned char *pDst, int iColour);

/* run-length coded images */
void RleDecodeRow8_Remap(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                         int iThreshold, int iCount);
void RleBlit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                       int iThreshold, int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey);
void RleBlit8_ColorKey_FlipX(unsigned char *pDst, const unsigned char *pSrc, const unsigned char *pRemap,
                             int iThreshold, int iWidth, int iHeight, int iDstPitch, int iSrcPitch, int iKey);

/* rectangular images */
void Blit8_ColorKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                    int iDstPitch, int iSrcPitch, int iKey);
void Blit8_ColorKey_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                          int iDstPitch, int iSrcPitch, int iKey);
void Blit8_Blend_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                       int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pBlend);
void Blit8_Silhouette(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                      int iDstPitch, int iSrcPitch, int iKey, int iColour);
void Blit8_Silhouette_FlipX(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                            int iDstPitch, int iSrcPitch, int iKey, int iColour);
void FillRect8_B(unsigned char *pDst, int iWidth, int iHeight, int iPitch, int iColour);
void FlipVertical8_Remap(unsigned char *pImage, int iWidth, int iHeight, const unsigned char *pRemap);
void RemapBytes8(unsigned char *pBuffer, int iCount, const unsigned char *pRemap);

/* whole 640x480 8-bit frames */
void CopyRows640(const unsigned char *pSrc, unsigned char *pDst, int iUnused, int iRows);
void StretchDouble320x240To640x480(unsigned char *pDst, const unsigned char *pSrc, int iUnused);
void Clear640x480(unsigned char *pFrame, unsigned int uValue);

void Blit8_ShadeDst(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                    int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pShade);
void Blit8_Remap(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                 int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pRemap);
void Blit8_Blend(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                 int iDstPitch, int iSrcPitch, int iKey, const unsigned char *pBlend);
void Blit8_ColorKey_ShadeKey(unsigned char *pDst, const unsigned char *pSrc, int iWidth, int iHeight,
                             int iDstPitch, int iSrcPitch, int iShadeKey, int iKey, const unsigned char *pShade);

#endif

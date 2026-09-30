;
; blit.asm - the hand-written 8-bit (palette index) blitters, 0x401000-0x402470 in the original:
; the first object of the link, so each label Lxxxxxx is the original address of its
; instruction.  None of them is referenced by the game's C code, which draws its images into
; 16-bit surfaces with its own C routines (engine.c); they are kept only because the object was
; linked in.  The routine names are those of the Ghidra database (docs/GHIDRA_CHANGES.md),
; chosen from what the code does.
; Assembled with JWasm -omf (MASM 6 compatible); the OMF object is converted by LINK (cvtomf).
;
; Conventions of all routines:
;   * cdecl: arguments on the stack ([ebp+08h] is the first), popped by the caller.  Several
;     routines change their argument slots in place (e.g. a pitch becomes an end-of-row skip).
;   * esi, edi, ebx, ecx and edx are saved and restored; eax and the flags are clobbered
;     (GetPixel8 returns the pixel in al, the others return nothing).  Most routines rely on the
;     direction flag being clear; some execute cld.
;   * pixels are bytes (palette indices); a pitch is the distance in bytes from one row to the next;
;     the key is the transparent index: source pixels equal to it are not drawn.
;   * a remap table is 256 bytes, new index = table[index] (palette/colour changes); a blend table
;     is 64 KB, result = table[dst * 256 + src] (translucency); a shade table is 256 bytes applied
;     to the destination pixel, dst = table[dst] (shadows).
;   * the 4-pixel inner loops exist in two (or three) copies: one runs while the pixels are
;     transparent, one while they are drawn; each jumps into the other at the pixel where the kind
;     changes, so runs of the same kind fall through without taken jumps.
;   * nothing is clipped: the caller passes an already clipped rectangle.
;
.386
.model flat

; ALIGN 4 with the filler instructions MASM 6.13 used (JWasm's own align fill differs).
ALIGN4 macro
    pad = (4 - (($ - text_start) and 3)) and 3
    if pad eq 1
        nop
    elseif pad eq 2
        mov eax, eax
    elseif pad eq 3
        db 2Eh
        mov eax, eax
    endif
endm

; the routines, in their order in the object
public _Blit8to16_ColorKey
public _Blit8to16_ColorKey_Unrolled
public _FillRect8
public _copy_ppvBits_to_lpSurface
public _GetPixel8
public _PutPixel8
public _AffineBlit8_ColorKey
public _AffineBlit8_ColorKey_Signed
public _AffineBlit8_Blend
public _AffineBlit8_Remap
public _AffineBlit8_ShadeDst
public _FillDword8
public _RleDecodeRow8_Remap
public _RleBlit8_ColorKey
public _RleBlit8_ColorKey_FlipX
public _Blit8_ColorKey
public _Blit8_ColorKey_FlipX
public _Blit8_Blend_FlipX
public _Blit8_Silhouette
public _Blit8_Silhouette_FlipX
public _FillRect8_B
public _FlipVertical8_Remap
public _RemapBytes8
public _CopyRows640
public _StretchDouble320x240To640x480
public _Clear640x480
public _Blit8_ShadeDst
public _Blit8_Remap
public _Blit8_Blend
public _Blit8_ColorKey_ShadeKey

.code
text_start label byte                    ; 0x401000: start of the object's code (ALIGN4 counts from here)
;
; ---------------------------------------------------------------------------------------------
; Blit8to16_ColorKey (0x401000): converts an 8-bit image to 16 bits through a palette, two
; pixels per step, skipping key pixels.
;   [ebp+08h] dst       16-bit destination (WORD pixels)
;   [ebp+0Ch] src       8-bit source
;   [ebp+10h] count     pixel pairs per row (each loop step converts 2 pixels)
;   [ebp+14h] rows
;   [ebp+18h] dstPitch  in pixels; becomes the end-of-row skip 2 * (dstPitch - count) bytes
;   [ebp+1Ch] srcPitch  becomes the skip srcPitch - count
;   [ebp+20h] palette   WORD[256]: 16-bit colour of each index
;   [ebp+24h] key       transparent index
; NB the skips subtract `count` although a row advances 2 * count pixels, so the pitches must be
; passed to suit.  Loop registers: esi src, edi dst, ecx pairs left, edx palette (the row and
; pair counts are saved on the stack), al = ah = key, ebx pixel index * 2 (upper bits 0).
; ---------------------------------------------------------------------------------------------
_Blit8to16_ColorKey proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]    ; ecx = pairs per row
    mov      edx, dword ptr [ebp+14h]    ; edx = rows
    mov      esi, dword ptr [ebp+0Ch]    ; esi = src
    mov      edi, dword ptr [ebp+8]      ; edi = dst
    sub      dword ptr [ebp+18h], ecx    ; dstPitch -> (dstPitch - count) * 2 bytes
    shl      dword ptr [ebp+18h], 1
    sub      dword ptr [ebp+1Ch], ecx    ; srcPitch -> srcPitch - count
    xor      ebx, ebx                    ; index register (only bx is ever loaded)
    mov      al, byte ptr [ebp+24h]      ; al = ah = key
    mov      ah, al
L401024:                                 ; row loop
    push     edx                         ; save rows, pairs
    push     ecx
    mov      edx, dword ptr [ebp+20h]    ; edx = palette
    ALIGN4
; skipping copy: test the two pixels; a visible one jumps into the drawing copy
L40102C:
    mov      bl, byte ptr [esi]
    cmp      al, bl
    jne      L40104A
L401032:
    mov      bl, byte ptr [esi+1]
    cmp      al, bl
    jne      L40105D
L401039:
    add      edi, 4                      ; next pair: 2 WORDs / 2 bytes
    add      esi, 2
    dec      ecx
    jne      L40102C
    jmp      L401074                     ; row done
; drawing copy (entered at L40104A / L40105D): palette[pixel] stored as a WORD
L401044:
    mov      bl, byte ptr [esi]
    cmp      al, bl
    je       L401032
L40104A:
    shl      bx, 1                       ; index * 2
    mov      bx, word ptr [ebx+edx]      ; 16-bit colour
    mov      word ptr [edi], bx
    xor      ebx, ebx                    ; clear the index register again
    mov      bl, byte ptr [esi+1]
    cmp      al, bl
    je       L401039
L40105D:
    shl      bx, 1
    mov      bx, word ptr [ebx+edx]
    mov      word ptr [edi+2], bx
    xor      ebx, ebx
    add      edi, 4
    add      esi, 2
    dec      ecx
    jne      L401044
    ALIGN4
; end of row: skip to the next row, count the rows down
L401074:
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L401024
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8to16_ColorKey endp

; ---------------------------------------------------------------------------------------------
; Blit8to16_ColorKey_Unrolled (0x401086): the same conversion, four pixels per step.
;   [ebp+08h] dst       16-bit destination
;   [ebp+0Ch] src       8-bit source
;   [ebp+10h] width     pixels per row
;   [ebp+14h] rows
;   [ebp+18h] dstPitch  in pixels; becomes dstPitch - width, added twice per row (= bytes)
;   [ebp+1Ch] srcPitch  becomes the skip srcPitch - width
;   [ebp+20h] palette   WORD[256]
;   [ebp+24h] key       transparent index (reloaded into cl for every group)
; NB the width & 3 leftover pixels are neither drawn nor stepped over (the loop at L401143 only
; counts them down), so only widths that are multiples of 4 work.
; Loop registers: esi src, edi dst, ecx groups left (saved while cl holds the key), edx palette,
; ebx pixel index (upper bits 0), ax colour.
; ---------------------------------------------------------------------------------------------
_Blit8to16_ColorKey_Unrolled proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]    ; ecx = width
    mov      edx, dword ptr [ebp+14h]    ; edx = rows
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+18h], ecx    ; dstPitch -> dstPitch - width (pixels)
    sub      dword ptr [ebp+1Ch], ecx    ; srcPitch -> srcPitch - width
    xor      ebx, ebx
    ALIGN4
L4010A4:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+20h]    ; edx = palette
    shr      ecx, 2                      ; groups of 4 pixels
    je       L401128
    ALIGN4
; skipping copy: a visible pixel jumps into the drawing copy
L4010B0:
    push     ecx                         ; save the group count, cl = key
    mov      cl, byte ptr [ebp+24h]
    mov      bl, byte ptr [esi]
    cmp      cl, bl
    jne      L4010E6
L4010BA:
    mov      bl, byte ptr [esi+1]
    cmp      cl, bl
    jne      L4010F4
L4010C1:
    mov      bl, byte ptr [esi+2]
    cmp      cl, bl
    jne      L401103
L4010C8:
    mov      bl, byte ptr [esi+3]
    cmp      cl, bl
    jne      L401112
L4010CF:
    add      edi, 8
    add      esi, 4
    pop      ecx
    dec      ecx
    jne      L4010B0
    jmp      L401128
    ALIGN4
; drawing copy: WORD palette[pixel] for each visible pixel
L4010DC:
    push     ecx
    mov      cl, byte ptr [ebp+24h]
    mov      bl, byte ptr [esi]
    cmp      cl, bl
    je       L4010BA
L4010E6:
    mov      ax, word ptr [edx+ebx*2]    ; 16-bit colour
    mov      word ptr [edi], ax
    mov      bl, byte ptr [esi+1]
    cmp      cl, bl
    je       L4010C1
L4010F4:
    mov      ax, word ptr [edx+ebx*2]
    mov      word ptr [edi+2], ax
    mov      bl, byte ptr [esi+2]
    cmp      cl, bl
    je       L4010C8
L401103:
    mov      ax, word ptr [edx+ebx*2]
    mov      word ptr [edi+4], ax
    mov      bl, byte ptr [esi+3]
    cmp      cl, bl
    je       L4010CF
L401112:
    mov      ax, word ptr [edx+ebx*2]
    mov      word ptr [edi+6], ax
    add      edi, 8
    add      esi, 4
    pop      ecx
    dec      ecx
    jne      L4010DC
    jmp      L401128
    ALIGN4
; end of row
L401128:
    pop      ecx
    push     ecx
    and      ecx, 3                      ; leftover pixels
    jne      L401143
L40112F:                                 ; skip to the next row (dst skip twice: pixels -> bytes)
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L4010A4
    je       L401149                     ; (always taken)
L401143:                                 ; sic: only counts the leftover pixels down
    inc      ecx
L401144:
    dec      ecx
    je       L40112F
    jmp      L401144
L401149:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8to16_ColorKey_Unrolled endp

; ---------------------------------------------------------------------------------------------
; FillRect8 (0x401150): fills a rectangle with one index (see also FillRect8_B).
;   [ebp+08h] dst
;   [ebp+0Ch] width    bytes per row
;   [ebp+10h] height
;   [ebp+14h] pitch    becomes the end-of-row skip pitch - width
;   [ebp+18h] colour
; Each row: rep stosd for width / 4 dwords, rep stosb for the rest.
; ---------------------------------------------------------------------------------------------
_FillRect8 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+0Ch]
    mov      edx, dword ptr [ebp+10h]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+14h], ecx
    xor      eax, eax                    ; eax = colour in all four bytes
    mov      al, byte ptr [ebp+18h]
    mov      ah, al
    mov      ebx, eax
    shl      eax, 10h
    add      eax, ebx
    mov      ebx, ecx                    ; ebx = dwords per row
    shr      ebx, 2
    mov      esi, ecx                    ; esi = leftover bytes per row
    and      esi, 3
L40117C:                                 ; row loop
    mov      ecx, ebx
    rep stosd
    mov      ecx, esi
    rep stosb
    add      edi, dword ptr [ebp+14h]
    dec      edx
    jne      L40117C
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_FillRect8 endp

; ---------------------------------------------------------------------------------------------
; copy_ppvBits_to_lpSurface (0x401191; the name suggests a copy from a DIB section's bits to a
; locked DirectDraw surface): copies a rectangle of bytes.
;   [ebp+08h] dst
;   [ebp+0Ch] src
;   [ebp+10h] width     bytes per row
;   [ebp+14h] rows
;   [ebp+18h] dstPitch  becomes dstPitch - width
;   [ebp+1Ch] srcPitch  becomes srcPitch - width
; Each row: rep movsd for width / 4 dwords, rep movsb for the rest.
; ---------------------------------------------------------------------------------------------
_copy_ppvBits_to_lpSurface proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]
    mov      edx, dword ptr [ebp+14h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    mov      ebx, ecx                    ; ebx = dwords per row
    shr      ebx, 2
    mov      eax, ecx                    ; eax = leftover bytes per row
    and      eax, 3
    ALIGN4
L4011B8:                                 ; row loop
    mov      ecx, ebx
    rep movsd
    mov      ecx, eax
    rep movsb
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    dec      edx
    jne      L4011B8
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_copy_ppvBits_to_lpSurface endp

; ---------------------------------------------------------------------------------------------
; GetPixel8 (0x4011D0): returns the byte at [ebp+08h] in al (the rest of eax is left as it was).
; ---------------------------------------------------------------------------------------------
_GetPixel8 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edi, dword ptr [ebp+8]
    mov      al, byte ptr [edi]
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_GetPixel8 endp

; ---------------------------------------------------------------------------------------------
; PutPixel8 (0x4011E4): stores the byte [ebp+0Ch] at the address [ebp+08h].
; ---------------------------------------------------------------------------------------------
_PutPixel8 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edi, dword ptr [ebp+8]
    mov      al, byte ptr [ebp+0Ch]
    mov      byte ptr [edi], al
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_PutPixel8 endp

; ---------------------------------------------------------------------------------------------
; AffineBlit8_ColorKey (0x4011FB): draws a rotated / scaled image (an affine mapping of the
; source) with a transparent key.
;   [ebp+08h] dst
;   [ebp+0Ch] src        source image
;   [ebp+10h] width      destination width (at most 512: col_offsets)
;   [ebp+14h] height     destination height (at most 512: row_ptrs)
;   [ebp+18h] dstPitch   becomes dstPitch - width
;   [ebp+1Ch] srcPitch
;   [ebp+20h] key
;   [ebp+24h] u0         source x of the first destination pixel (24.8 fixed point)
;   [ebp+28h] v0         source y of the first destination pixel (24.8 fixed point)
;   [ebp+2Ch] duRow      source x step per destination row (24.8)
;   [ebp+30h] dvRow      source y step per destination row (24.8)
;   [ebp+34h] duCol      source x step per destination column (24.8)
;   [ebp+38h] dvCol      source y step per destination column (24.8)
; The source offset of every column relative to the first one is computed once (col_offsets
; holds the differences between neighbouring columns) and every row starts from its own source
; pointer (row_ptrs), so all rows walk the same column pattern.  Coordinates are truncated
; (unsigned >> 8) and the source is not bounds-checked.
; Drawing registers: edx -> row_ptrs entry, ebx -> col_offsets entry, esi source pointer, edi
; dst, ecx groups of 4 columns, ah key, al pixel.
; ---------------------------------------------------------------------------------------------
_AffineBlit8_ColorKey proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      ecx, dword ptr [ebp+10h]
    mov      dword ptr [width_px], ecx   ; (never read)
    ; base_offset = (v0 >> 8) * srcPitch + (u0 >> 8): source offset of column 0
    mov      eax, dword ptr [ebp+28h]
    shr      eax, 8
    mul      dword ptr [ebp+1Ch]
    mov      edx, dword ptr [ebp+24h]
    shr      edx, 8
    add      eax, edx
    mov      dword ptr [base_offset], eax
    ; col_offsets[i] = offset(column i) - offset(column i - 1), offsets relative to base_offset
    mov      edx, dword ptr [ebp+10h]
    mov      dword ptr [count_x], edx
    mov      ebx, dword ptr [ebp+24h]    ; ebx = u, ecx = v
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset col_offsets     ; edi = output, esi = previous offset
    xor      esi, esi
    ALIGN4
L40123C:
    mov      eax, ecx
    shr      eax, 8
    mul      dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    sub      eax, dword ptr [base_offset]
    push     eax                         ; offset of this column
    sub      eax, esi                    ; - offset of the previous one
    stosd
    pop      esi                         ; previous = this
    add      ebx, dword ptr [ebp+34h]    ; u += duCol
    add      ecx, dword ptr [ebp+38h]    ; v += dvCol
    dec      dword ptr [count_x]
    jne      L40123C
    ; row_ptrs[j] = src + (v >> 8) * srcPitch + (u >> 8), stepping (u, v) by the row steps
    mov      edx, dword ptr [ebp+14h]
    mov      esi, dword ptr [ebp+14h]    ; esi = rows to set up
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset row_ptrs
    ALIGN4
L401278:
    mov      eax, ecx
    shr      eax, 8
    mul      dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    add      eax, dword ptr [ebp+0Ch]
    stosd
    add      ebx, dword ptr [ebp+2Ch]    ; u += duRow
    add      ecx, dword ptr [ebp+30h]    ; v += dvRow
    dec      esi
    jne      L401278
    ; draw
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx    ; dstPitch -> dstPitch - width
    mov      edx, dword ptr [ebp+14h]
    mov      dword ptr [count_y], edx
    mov      edi, dword ptr [ebp+8]
    mov      edx, offset row_ptrs
    mov      ah, byte ptr [ebp+20h]      ; ah = key
    ALIGN4
L4012B0:                                 ; row loop: esi = the row's first source pixel
    mov      esi, dword ptr [edx]
    mov      ecx, dword ptr [ebp+10h]
    shr      ecx, 2                      ; groups of 4 columns
    je       L401350
    mov      ebx, offset col_offsets
    ALIGN4
; skipping copy: step to each column's source pixel; a visible one jumps into the drawing copy
L4012C4:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L401304
    jmp      L4012CE                     ; (jump to the next instruction, as in the original)
L4012CE:
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L40130F
    jmp      L4012D9
L4012D9:
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L40131B
    jmp      L4012E4
L4012E4:
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L401327
    jmp      L4012EF
L4012EF:
    add      edi, 4
    add      ebx, 10h
    dec      ecx
    jne      L4012C4
    jmp      L401334
    ALIGN4
; drawing copy
L4012FC:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4012CE
L401304:
    mov      byte ptr [edi], al
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4012D9
L40130F:
    mov      byte ptr [edi+1], al
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4012E4
L40131B:
    mov      byte ptr [edi+2], al
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4012EF
L401327:
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      ebx, 10h
    dec      ecx
    jne      L4012FC
    ALIGN4
; leftover columns (width & 3), one at a time
L401334:
    mov      ecx, dword ptr [ebp+10h]
    and      ecx, 3
    jne      L40135B
L40133C:                                 ; next row
    add      edi, dword ptr [ebp+18h]
    add      edx, 4
    dec      dword ptr [count_y]
    jne      L4012B0
    jmp      L401370
L401350:                                 ; width < 4: only leftover columns
    mov      ebx, offset col_offsets
    mov      ecx, dword ptr [ebp+10h]
    and      ecx, 3
L40135B:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L401365
    mov      byte ptr [edi], al
L401365:
    inc      edi
    add      ebx, 4
    dec      ecx
    jne      L40135B
    jmp      L40133C
    ALIGN4
L401370:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_AffineBlit8_ColorKey endp

; ---------------------------------------------------------------------------------------------
; AffineBlit8_ColorKey_Signed (0x401377): AffineBlit8_ColorKey for signed coordinates, with a
; source range.
;   [ebp+08h] dst
;   [ebp+0Ch] src        source image (also the lowest valid source address)
;   [ebp+10h] srcEnd     end of the source (first invalid address)
;   [ebp+14h] width      (at most 512)
;   [ebp+18h] height     (at most 512)
;   [ebp+1Ch] dstPitch   becomes dstPitch - width
;   [ebp+20h] srcPitch
;   [ebp+24h] key
;   [ebp+28h] u0, [ebp+2Ch] v0          start position (24.8, signed)
;   [ebp+30h] duRow, [ebp+34h] dvRow    step per destination row
;   [ebp+38h] duCol, [ebp+3Ch] dvCol    step per destination column
; Coordinates >= 0x80000000 are negative: they are shifted as neg, shr 8, neg (rounding towards
; zero).  Only the leftover columns (width & 3) check that the source pointer lies in
; [src, srcEnd); the 4-column groups do not.
; ---------------------------------------------------------------------------------------------
_AffineBlit8_ColorKey_Signed proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      ecx, dword ptr [ebp+14h]
    mov      dword ptr [width_px], ecx
    ; base_offset = (v0 >> 8) * srcPitch + (u0 >> 8), signed shifts
    mov      eax, dword ptr [ebp+2Ch]
    cmp      eax, 80000000h
    jae      L401398
    shr      eax, 8
    jmp      L40139F
L401398:
    neg      eax                         ; negative: shift the magnitude
    shr      eax, 8
    neg      eax
L40139F:
    mul      dword ptr [ebp+20h]
    mov      edx, dword ptr [ebp+28h]
    cmp      edx, 80000000h
    jae      L4013B2
    shr      edx, 8
    jmp      L4013B9
L4013B2:
    neg      edx
    shr      edx, 8
    neg      edx
L4013B9:
    add      eax, edx
    mov      dword ptr [base_offset], eax
    ; col_offsets: differences between neighbouring columns' source offsets
    mov      edx, dword ptr [ebp+14h]
    mov      dword ptr [count_x], edx
    mov      ebx, dword ptr [ebp+28h]
    mov      ecx, dword ptr [ebp+2Ch]
    mov      edi, offset col_offsets
    xor      esi, esi
    ALIGN4
L4013D8:
    mov      eax, ecx
    cmp      eax, 80000000h
    jae      L4013E6
    shr      eax, 8
    jmp      L4013ED
L4013E6:
    neg      eax
    shr      eax, 8
    neg      eax
L4013ED:
    mul      dword ptr [ebp+20h]
    mov      edx, ebx
    cmp      edx, 80000000h
    jae      L4013FF
    shr      edx, 8
    jmp      L401406
L4013FF:
    neg      edx
    shr      edx, 8
    neg      edx
L401406:
    add      eax, edx
    sub      eax, dword ptr [base_offset]
    push     eax
    sub      eax, esi
    stosd
    pop      esi
    add      ebx, dword ptr [ebp+38h]    ; u += duCol
    add      ecx, dword ptr [ebp+3Ch]    ; v += dvCol
    dec      dword ptr [count_x]
    jne      L4013D8
    ; row_ptrs[j] = src + (v >> 8) * srcPitch + (u >> 8)
    mov      edx, dword ptr [ebp+18h]
    mov      esi, dword ptr [ebp+18h]
    mov      ebx, dword ptr [ebp+28h]
    mov      ecx, dword ptr [ebp+2Ch]
    mov      edi, offset row_ptrs
    ALIGN4
L401434:
    mov      eax, ecx
    cmp      eax, 80000000h
    jae      L401442
    shr      eax, 8
    jmp      L401449
L401442:
    neg      eax
    shr      eax, 8
    neg      eax
L401449:
    mul      dword ptr [ebp+20h]
    mov      edx, ebx
    cmp      edx, 80000000h
    jae      L40145B
    shr      edx, 8
    jmp      L401462
L40145B:
    neg      edx
    shr      edx, 8
    neg      edx
L401462:
    add      eax, edx
    add      eax, dword ptr [ebp+0Ch]
    stosd
    add      ebx, dword ptr [ebp+30h]    ; u += duRow
    add      ecx, dword ptr [ebp+34h]    ; v += dvRow
    dec      esi
    jne      L401434
    ; draw (as AffineBlit8_ColorKey)
    mov      ecx, dword ptr [ebp+14h]
    sub      dword ptr [ebp+1Ch], ecx
    mov      edx, dword ptr [ebp+18h]
    mov      dword ptr [count_y], edx
    mov      edi, dword ptr [ebp+8]
    mov      edx, offset row_ptrs
    mov      ah, byte ptr [ebp+24h]      ; ah = key
    ALIGN4
L40148C:                                 ; row loop
    mov      esi, dword ptr [edx]
    mov      ecx, dword ptr [ebp+14h]
    shr      ecx, 2
    je       L40152C
    mov      ebx, offset col_offsets
    ALIGN4
; skipping copy
L4014A0:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L4014E0
    jmp      L4014AA
L4014AA:
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L4014EB
    jmp      L4014B5
L4014B5:
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L4014F7
    jmp      L4014C0
L4014C0:
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, ah
    jne      L401503
    jmp      L4014CB
L4014CB:
    add      edi, 4
    add      ebx, 10h
    dec      ecx
    jne      L4014A0
    jmp      L401510
    ALIGN4
; drawing copy
L4014D8:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4014AA
L4014E0:
    mov      byte ptr [edi], al
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4014B5
L4014EB:
    mov      byte ptr [edi+1], al
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4014C0
L4014F7:
    mov      byte ptr [edi+2], al
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L4014CB
L401503:
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      ebx, 10h
    dec      ecx
    jne      L4014D8
    ALIGN4
; leftover columns
L401510:
    mov      ecx, dword ptr [ebp+14h]
    and      ecx, 3
    jne      L401537
L401518:                                 ; next row
    add      edi, dword ptr [ebp+1Ch]
    add      edx, 4
    dec      dword ptr [count_y]
    jne      L40148C
    jmp      L401554
L40152C:
    mov      ebx, offset col_offsets
    mov      ecx, dword ptr [ebp+14h]
    and      ecx, 3
; leftover column: drawn only if src <= pointer < srcEnd
L401537:
    add      esi, dword ptr [ebx]
    cmp      esi, dword ptr [ebp+0Ch]
    jb       L40154B
    cmp      esi, dword ptr [ebp+10h]
    jae      L40154B
    mov      al, byte ptr [esi]
    cmp      al, ah
    je       L40154B
    mov      byte ptr [edi], al
L40154B:
    inc      edi
    add      ebx, 4
    dec      ecx
    jne      L401537
    jmp      L401518
L401554:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_AffineBlit8_ColorKey_Signed endp

; ---------------------------------------------------------------------------------------------
; AffineBlit8_Blend (0x40155B): AffineBlit8_ColorKey drawing translucently through a blend table.
;   [ebp+08h]-[ebp+38h] as AffineBlit8_ColorKey (dst, src, width, height, dstPitch, srcPitch,
;                       key, u0, v0, duRow, dvRow, duCol, dvCol)
;   [ebp+3Ch] blend     64 KB table: dst = blend[dst * 256 + src]
; The row offset uses imul (signed) instead of mul; the shifts are still unsigned.
; Drawing registers: ecx blend table (the row_ptrs pointer is kept on the stack), dl key,
; count_x groups left, al source pixel, ah destination pixel.
; ---------------------------------------------------------------------------------------------
_AffineBlit8_Blend proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx    ; dstPitch -> dstPitch - width
    mov      dword ptr [width_px], ecx
    ; base_offset = (v0 >> 8) * srcPitch + (u0 >> 8)
    mov      eax, dword ptr [ebp+28h]
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, dword ptr [ebp+24h]
    shr      edx, 8
    add      eax, edx
    mov      dword ptr [base_offset], eax
    ; col_offsets: differences between neighbouring columns' source offsets
    mov      edx, dword ptr [ebp+10h]
    mov      dword ptr [count_x], edx
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset col_offsets
    xor      esi, esi
L40159C:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    sub      eax, dword ptr [base_offset]
    push     eax
    sub      eax, esi
    stosd
    pop      esi
    add      ebx, dword ptr [ebp+34h]
    add      ecx, dword ptr [ebp+38h]
    dec      dword ptr [count_x]
    jne      L40159C
    ; row_ptrs: source pointer of each destination row
    mov      edx, dword ptr [ebp+14h]
    mov      esi, dword ptr [ebp+14h]
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset row_ptrs
    ALIGN4
L4015D8:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    add      eax, dword ptr [ebp+0Ch]
    stosd
    add      ebx, dword ptr [ebp+2Ch]
    add      ecx, dword ptr [ebp+30h]
    dec      esi
    jne      L4015D8
    ; draw
    mov      edx, dword ptr [ebp+14h]
    mov      dword ptr [count_y], edx
    mov      edi, dword ptr [ebp+8]
    mov      ecx, offset row_ptrs
    ALIGN4
L401608:                                 ; row loop: save the row_ptrs pointer
    push     ecx
    mov      esi, dword ptr [ecx]
    mov      eax, dword ptr [ebp+10h]
    shr      eax, 2
    je       L4016D1
    mov      dword ptr [count_x], eax
    mov      ebx, offset col_offsets
    mov      dl, byte ptr [ebp+20h]      ; dl = key
    mov      ecx, dword ptr [ebp+3Ch]    ; ecx = blend table
    xor      eax, eax
    ALIGN4
; skipping copy
L40162C:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L401668
L401634:
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L401678
L40163D:
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L40168A
L401646:
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L40169C
L40164F:
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L40162C
    jmp      L4016B4
    ALIGN4
; drawing copy: al = blend[dst * 256 + src]
L401660:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L401634
L401668:
    mov      ah, byte ptr [edi]          ; ah = destination pixel
    mov      al, byte ptr [ecx+eax]      ; blended
    mov      byte ptr [edi], al
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L40163D
L401678:
    mov      ah, byte ptr [edi+1]
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+1], al
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L401646
L40168A:
    mov      ah, byte ptr [edi+2]
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+2], al
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L40164F
L40169C:
    mov      ah, byte ptr [edi+3]
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L401660
    ALIGN4
; leftover columns
L4016B4:
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
    jne      L4016DC
L4016BC:                                 ; next row
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    add      ecx, 4
    dec      dword ptr [count_y]
    jne      L401608
    jmp      L401706
L4016D1:                                 ; width < 4
    mov      ebx, offset col_offsets
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
L4016DC:
    mov      dword ptr [count_x], eax
    mov      dl, byte ptr [ebp+20h]
    mov      ecx, dword ptr [ebp+3Ch]
    xor      eax, eax
L4016E9:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L4016F8
    mov      ah, byte ptr [edi]
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi], al
L4016F8:
    inc      edi
    add      ebx, 4
    dec      dword ptr [count_x]
    jne      L4016E9
    jmp      L4016BC
L401706:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_AffineBlit8_Blend endp

; ---------------------------------------------------------------------------------------------
; AffineBlit8_Remap (0x40170D): AffineBlit8_ColorKey drawing each pixel through a remap table.
;   [ebp+08h]-[ebp+38h] as AffineBlit8_Blend
;   [ebp+3Ch] remap     256-byte table: dst = remap[src]
; (ah stays 0 in the drawing loops, so eax is the source pixel.)
; ---------------------------------------------------------------------------------------------
_AffineBlit8_Remap proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx
    mov      dword ptr [width_px], ecx
    ; base_offset = (v0 >> 8) * srcPitch + (u0 >> 8)
    mov      eax, dword ptr [ebp+28h]
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, dword ptr [ebp+24h]
    shr      edx, 8
    add      eax, edx
    mov      dword ptr [base_offset], eax
    ; col_offsets: differences between neighbouring columns' source offsets
    mov      edx, dword ptr [ebp+10h]
    mov      dword ptr [count_x], edx
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset col_offsets
    xor      esi, esi
    ALIGN4
L401750:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    sub      eax, dword ptr [base_offset]
    push     eax
    sub      eax, esi
    stosd
    pop      esi
    add      ebx, dword ptr [ebp+34h]
    add      ecx, dword ptr [ebp+38h]
    dec      dword ptr [count_x]
    jne      L401750
    ; row_ptrs: source pointer of each destination row
    mov      edx, dword ptr [ebp+14h]
    mov      esi, dword ptr [ebp+14h]
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset row_ptrs
    ALIGN4
L40178C:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    add      eax, dword ptr [ebp+0Ch]
    stosd
    add      ebx, dword ptr [ebp+2Ch]
    add      ecx, dword ptr [ebp+30h]
    dec      esi
    jne      L40178C
    ; draw
    mov      edx, dword ptr [ebp+14h]
    mov      dword ptr [count_y], edx
    mov      edi, dword ptr [ebp+8]
    mov      ecx, offset row_ptrs
    ALIGN4
L4017BC:                                 ; row loop: save the row_ptrs pointer
    push     ecx
    mov      esi, dword ptr [ecx]
    mov      eax, dword ptr [ebp+10h]
    shr      eax, 2
    je       L401879
    mov      dword ptr [count_x], eax
    mov      ebx, offset col_offsets
    mov      dl, byte ptr [ebp+20h]      ; dl = key
    mov      ecx, dword ptr [ebp+3Ch]    ; ecx = remap table
    xor      eax, eax
    ALIGN4
; skipping copy
L4017E0:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L40181C
L4017E8:
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L40182A
L4017F1:
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L401839
L4017FA:
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, dl
    jne      L401848
L401803:
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L4017E0
    jmp      L40185C
    ALIGN4
; drawing copy: remap[src]
L401814:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L4017E8
L40181C:
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi], al
    add      esi, dword ptr [ebx+4]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L4017F1
L40182A:
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+1], al
    add      esi, dword ptr [ebx+8]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L4017FA
L401839:
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+2], al
    add      esi, dword ptr [ebx+0Ch]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L401803
L401848:
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L401814
; leftover columns
L40185C:
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
    jne      L401884
L401864:                                 ; next row
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    add      ecx, 4
    dec      dword ptr [count_y]
    jne      L4017BC
    jmp      L4018AC
L401879:                                 ; width < 4
    mov      ebx, offset col_offsets
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
L401884:
    mov      dword ptr [count_x], eax
    mov      dl, byte ptr [ebp+20h]
    mov      ecx, dword ptr [ebp+3Ch]
    xor      eax, eax
L401891:
    add      esi, dword ptr [ebx]
    mov      al, byte ptr [esi]
    cmp      al, dl
    je       L40189E
    mov      al, byte ptr [ecx+eax]
    mov      byte ptr [edi], al
L40189E:
    inc      edi
    add      ebx, 4
    dec      dword ptr [count_x]
    jne      L401891
    jmp      L401864
L4018AC:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_AffineBlit8_Remap endp

; ---------------------------------------------------------------------------------------------
; AffineBlit8_ShadeDst (0x4018B3): the source only gives a shape: where the (affinely mapped)
; source pixel is not the key, the destination pixel is passed through a shade table (shadows).
;   [ebp+08h]-[ebp+38h] as AffineBlit8_Blend
;   [ebp+3Ch] shade     256-byte table: dst = shade[dst]
; Drawing registers: al key, ecx shade table, dl destination pixel (edx upper bits 0).
; ---------------------------------------------------------------------------------------------
_AffineBlit8_ShadeDst proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx
    mov      dword ptr [width_px], ecx
    ; base_offset = (v0 >> 8) * srcPitch + (u0 >> 8)
    mov      eax, dword ptr [ebp+28h]
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, dword ptr [ebp+24h]
    shr      edx, 8
    add      eax, edx
    mov      dword ptr [base_offset], eax
    ; col_offsets: differences between neighbouring columns' source offsets
    mov      edx, dword ptr [ebp+10h]
    mov      dword ptr [count_x], edx
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset col_offsets
    xor      esi, esi
L4018F4:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    sub      eax, dword ptr [base_offset]
    push     eax
    sub      eax, esi
    stosd
    pop      esi
    add      ebx, dword ptr [ebp+34h]
    add      ecx, dword ptr [ebp+38h]
    dec      dword ptr [count_x]
    jne      L4018F4
    ; row_ptrs: source pointer of each destination row
    mov      edx, dword ptr [ebp+14h]
    mov      esi, dword ptr [ebp+14h]
    mov      ebx, dword ptr [ebp+24h]
    mov      ecx, dword ptr [ebp+28h]
    mov      edi, offset row_ptrs
    ALIGN4
L401930:
    mov      eax, ecx
    shr      eax, 8
    imul     dword ptr [ebp+1Ch]
    mov      edx, ebx
    shr      edx, 8
    add      eax, edx
    add      eax, dword ptr [ebp+0Ch]
    stosd
    add      ebx, dword ptr [ebp+2Ch]
    add      ecx, dword ptr [ebp+30h]
    dec      esi
    jne      L401930
    ; draw
    mov      edx, dword ptr [ebp+14h]
    mov      dword ptr [count_y], edx
    mov      edi, dword ptr [ebp+8]
    mov      ecx, offset row_ptrs
    ALIGN4
L401960:                                 ; row loop
    mov      esi, dword ptr [ecx]
    push     ecx
    mov      eax, dword ptr [ebp+10h]
    shr      eax, 2
    je       L401A19
    mov      dword ptr [count_x], eax
    mov      ebx, offset col_offsets
    mov      al, byte ptr [ebp+20h]      ; al = key
    mov      ecx, dword ptr [ebp+3Ch]    ; ecx = shade table
    xor      edx, edx
    ALIGN4
; skipping copy
L401984:
    add      esi, dword ptr [ebx]
    cmp      al, byte ptr [esi]
    jne      L4019B6
L40198A:
    add      esi, dword ptr [ebx+4]
    cmp      al, byte ptr [esi]
    jne      L4019C4
L401991:
    add      esi, dword ptr [ebx+8]
    cmp      al, byte ptr [esi]
    jne      L4019D4
L401998:
    add      esi, dword ptr [ebx+0Ch]
    cmp      al, byte ptr [esi]
    jne      L4019E4
L40199F:
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L401984
    jmp      L4019FC
    ALIGN4
; drawing copy: dst = shade[dst]
L4019B0:
    add      esi, dword ptr [ebx]
    cmp      al, byte ptr [esi]
    je       L40198A
L4019B6:
    mov      dl, byte ptr [edi]
    mov      dl, byte ptr [edx+ecx]
    mov      byte ptr [edi], dl
    add      esi, dword ptr [ebx+4]
    cmp      al, byte ptr [esi]
    je       L401991
L4019C4:
    mov      dl, byte ptr [edi+1]
    mov      dl, byte ptr [edx+ecx]
    mov      byte ptr [edi+1], dl
    add      esi, dword ptr [ebx+8]
    cmp      al, byte ptr [esi]
    je       L401998
L4019D4:
    mov      dl, byte ptr [edi+2]
    mov      dl, byte ptr [edx+ecx]
    mov      byte ptr [edi+2], dl
    add      esi, dword ptr [ebx+0Ch]
    cmp      al, byte ptr [esi]
    je       L40199F
L4019E4:
    mov      dl, byte ptr [edi+3]
    mov      dl, byte ptr [edx+ecx]
    mov      byte ptr [edi+3], dl
    add      edi, 4
    add      ebx, 10h
    dec      dword ptr [count_x]
    jne      L4019B0
    ALIGN4
; leftover columns
L4019FC:
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
    jne      L401A29
L401A04:                                 ; next row
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    add      ecx, 4
    dec      dword ptr [count_y]
    jne      L401960
    jmp      L401A4C
L401A19:                                 ; width < 4
    mov      ecx, dword ptr [ebp+3Ch]
    xor      edx, edx
    mov      ebx, offset col_offsets
    mov      eax, dword ptr [ebp+10h]
    and      eax, 3
L401A29:
    mov      dword ptr [count_x], eax
    mov      al, byte ptr [ebp+20h]
L401A31:
    add      esi, dword ptr [ebx]
    cmp      al, byte ptr [esi]
    je       L401A3E
    mov      dl, byte ptr [edi]
    mov      dl, byte ptr [edx+ecx]
    mov      byte ptr [edi], dl
L401A3E:
    inc      edi
    add      ebx, 4
    dec      dword ptr [count_x]
    jne      L401A31
    jmp      L401A04
L401A4C:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_AffineBlit8_ShadeDst endp

; ---------------------------------------------------------------------------------------------
; FillDword8 (0x401A53): builds the index [ebp+0Ch] in all four bytes and stores it at
; [ebp+08h] - one dword only: ecx is loaded with 0x10000 but the stosd has no rep prefix (a
; 256 KB fill was probably intended).
; ---------------------------------------------------------------------------------------------
_FillDword8 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    xor      eax, eax
    mov      al, byte ptr [ebp+0Ch]
    mov      ah, al
    mov      ebx, eax
    shl      eax, 10h
    add      eax, ebx
    mov      ecx, 10000h                 ; (unused: no rep)
    mov      edi, dword ptr [ebp+8]
    stosd
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_FillDword8 endp

; ---------------------------------------------------------------------------------------------
; RleDecodeRow8_Remap (0x401A7A): unpacks run-length coded pixels through a remap table.
;   [ebp+08h] dst
;   [ebp+0Ch] src        coded data
;   [ebp+10h] remap      256-byte table applied to every pixel
;   [ebp+14h] threshold  T: a byte below T is one literal pixel; a byte b >= T is a run: the next
;                        byte is repeated b - T + 3 times
;   [ebp+18h] count      pixels to write (a run is not cut at the end)
; Registers: esi src, edi dst, ecx pixels left, edx remap table, bl = T, bh = T - 2, eax the
; byte read (upper bits 0).
; ---------------------------------------------------------------------------------------------
_RleDecodeRow8_Remap proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+18h]    ; ecx = pixels to write
    xor      eax, eax
    mov      edx, dword ptr [ebp+10h]
    mov      bl, byte ptr [ebp+14h]      ; bl = T, bh = T - 2
    mov      bh, bl
    dec      bh
    dec      bh
    ALIGN4
L401A9C:                                 ; literal pixels, unrolled 4 times
    lodsb
    cmp      al, bl
    jae      L401AD0
    mov      al, byte ptr [edx+eax]
    stosb
    dec      ecx
    je       L401AEF
    lodsb
    cmp      al, bl
    jae      L401AD0
    mov      al, byte ptr [edx+eax]
    stosb
    dec      ecx
    je       L401AEF
    lodsb
    cmp      al, bl
    jae      L401AD0
    mov      al, byte ptr [edx+eax]
    stosb
    dec      ecx
    je       L401AEF
    lodsb
    cmp      al, bl
    jae      L401AD0
    mov      al, byte ptr [edx+eax]
    stosb
    loop     L401A9C                     ; (dec ecx, jnz)
    jmp      L401AEF
    ALIGN4
L401AD0:                                 ; run
    sub      al, bh                      ; al = b - T + 2 = run length - 1
    sub      ecx, eax                    ; count it (the last pixel below)
    push     ecx
    xor      ecx, ecx
    mov      cl, al
    inc      ecx                         ; ecx = run length
    lodsb
    mov      al, byte ptr [edx+eax]      ; the repeated pixel, remapped
    rep stosb
    pop      ecx
    dec      ecx                         ; the run's last pixel
    je       L401AEF
    lodsb
    cmp      al, bl
    jae      L401AD0
    mov      al, byte ptr [edx+eax]
    stosb
    loop     L401A9C
L401AEF:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_RleDecodeRow8_Remap endp

; ---------------------------------------------------------------------------------------------
; RleBlit8_ColorKey (0x401AF6): draws a run-length coded image through a remap table, with a key.
;   [ebp+08h] dst
;   [ebp+0Ch] src        coded data: one stream for the whole image (runs may cross rows)
;   [ebp+10h] remap      256-byte table applied to every pixel
;   [ebp+14h] threshold  T: a byte below T is one pixel; a byte b >= T is a run: the next byte
;                        is drawn b - T + 1 times
;   [ebp+18h] width
;   [ebp+1Ch] height
;   [ebp+20h] dstPitch   becomes dstPitch - width
;   [ebp+24h] srcPitch   becomes srcPitch - width, but is not used (the stream has no padding)
;   [ebp+28h] key        compared with the remapped pixel
; Registers: esi stream, edi dst, ecx columns left (the width is saved on the stack), edx rows,
; bx pixels left in the current run, al current (remapped) pixel, ah key.
; ---------------------------------------------------------------------------------------------
_RleBlit8_ColorKey proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+1Ch]    ; edx = rows
    xor      ecx, ecx                    ; eax = key in all four bytes (only ah is used)
    mov      eax, ecx
    mov      ah, byte ptr [ebp+28h]
    mov      al, ah
    mov      cx, ax
    shl      eax, 10h
    add      eax, ecx
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+18h]    ; ecx = width
    sub      dword ptr [ebp+20h], ecx
    sub      dword ptr [ebp+24h], ecx
    xor      ebx, ebx                    ; no run pending
    ALIGN4
L401B24:                                 ; row loop
    push     ecx
L401B25:                                 ; pixel loop
    or       bx, bx
    je       L401B2E
    dec      bx                          ; in a run: the same pixel again
    jmp      L401B4C
L401B2E:
    mov      al, byte ptr [esi]          ; next code byte
    inc      esi
    cmp      al, byte ptr [ebp+14h]
    jb       L401B3E
    sub      al, byte ptr [ebp+14h]      ; run: bl = further repeats
    mov      bl, al
    mov      al, byte ptr [esi]          ; the run's pixel
    inc      esi
; al = remap[al] (edi and ebx borrowed)
L401B3E:
    push     ebx
    push     edi
    xor      ebx, ebx
    mov      bl, al
    mov      edi, dword ptr [ebp+10h]
    mov      al, byte ptr [edi+ebx]
    pop      edi
    pop      ebx
L401B4C:
    cmp      al, ah                      ; key: leave the pixel
    je       L401B52
    mov      byte ptr [edi], al
L401B52:
    inc      edi
    dec      ecx
    jne      L401B25
    add      edi, dword ptr [ebp+20h]    ; next row
    pop      ecx
    dec      edx
    jne      L401B24
    je       L401B5F                     ; (always taken)
L401B5F:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_RleBlit8_ColorKey endp

; ---------------------------------------------------------------------------------------------
; RleBlit8_ColorKey_FlipX (0x401B66): RleBlit8_ColorKey mirrored horizontally: dst points at the
; rightmost pixel of the first row and each row is drawn right to left (the end-of-row step
; becomes dstPitch + width).  Same arguments.
; ---------------------------------------------------------------------------------------------
_RleBlit8_ColorKey_FlipX proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+1Ch]
    xor      ecx, ecx
    mov      eax, ecx
    mov      ah, byte ptr [ebp+28h]
    mov      al, ah
    mov      cx, ax
    shl      eax, 10h
    add      eax, ecx
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+18h]
    add      dword ptr [ebp+20h], ecx    ; dstPitch -> dstPitch + width
    sub      dword ptr [ebp+24h], ecx
    xor      ebx, ebx
    ALIGN4
L401B94:                                 ; row loop
    push     ecx                         ; save the width
L401B95:
    or       bx, bx
    je       L401B9E
    dec      bx
    jmp      L401BBC
L401B9E:
    mov      al, byte ptr [esi]
    inc      esi
    cmp      al, byte ptr [ebp+14h]
    jb       L401BAE
    sub      al, byte ptr [ebp+14h]
    mov      bl, al
    mov      al, byte ptr [esi]
    inc      esi
; al = remap[al]
L401BAE:
    push     ebx
    push     edi
    xor      ebx, ebx
    mov      bl, al
    mov      edi, dword ptr [ebp+10h]
    mov      al, byte ptr [edi+ebx]
    pop      edi
    pop      ebx
L401BBC:
    cmp      al, ah
    je       L401BC2
    mov      byte ptr [edi], al
L401BC2:
    dec      edi                         ; right to left
    dec      ecx
    jne      L401B95
    add      edi, dword ptr [ebp+20h]
    pop      ecx
    dec      edx
    jne      L401B94
    je       L401BCF                     ; (always taken)
L401BCF:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_RleBlit8_ColorKey_FlipX endp

; ---------------------------------------------------------------------------------------------
; Blit8_ColorKey (0x401BD6): copies an image, skipping key pixels.
;   [ebp+08h] dst
;   [ebp+0Ch] src
;   [ebp+10h] width
;   [ebp+14h] height
;   [ebp+18h] dstPitch  becomes dstPitch - width
;   [ebp+1Ch] srcPitch  becomes srcPitch - width
;   [ebp+20h] key
; Four source pixels are read as one dword (edx: dl, dh, then shr 16): if all four are the key
; (eax = key in all bytes) they are skipped at once, else they are tested one by one.
; Registers: esi src, edi dst, ecx groups left, ebx width, rows on the stack, ah key.
; NB the leftover loop loads the pixel into al, which spoils the four-key pattern in eax: from
; then on the dword test never matches (only slower, the byte tests use ah).
; ---------------------------------------------------------------------------------------------
_Blit8_ColorKey proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+14h]    ; edx = rows
    xor      ecx, ecx                    ; eax = key * 01010101h
    mov      eax, ecx
    mov      ah, byte ptr [ebp+20h]
    mov      al, ah
    mov      cx, ax
    shl      eax, 10h
    add      eax, ecx
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    mov      ebx, ecx
    ALIGN4
L401C04:                                 ; row loop
    push     edx
    mov      ecx, ebx
    shr      ecx, 2                      ; groups of 4
    je       L401C5C
; skipping copy
L401C0C:
    mov      edx, dword ptr [esi]        ; 4 source pixels
    cmp      edx, eax                    ; all four transparent?
    je       L401C25
    cmp      dl, ah
    jne      L401C36
L401C16:
    cmp      dh, ah
    jne      L401C3C
L401C1A:
    shr      edx, 10h
    cmp      dl, ah
    jne      L401C46
L401C21:
    cmp      dh, ah
    jne      L401C4D
L401C25:
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L401C0C
    je       L401C5C
; drawing copy
L401C30:
    mov      edx, dword ptr [esi]
    cmp      dl, ah
    je       L401C16
L401C36:
    mov      byte ptr [edi], dl
    cmp      dh, ah
    je       L401C1A
L401C3C:
    mov      byte ptr [edi+1], dh
    shr      edx, 10h
    cmp      dl, ah
    je       L401C21
L401C46:
    mov      byte ptr [edi+2], dl
    cmp      dh, ah
    je       L401C25
L401C4D:
    mov      byte ptr [edi+3], dh
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L401C30
    je       L401C5C
    ALIGN4
; leftover pixels (width & 3)
L401C5C:
    mov      ecx, ebx
    and      ecx, 3
    jne      L401C6F
L401C63:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      edx
    dec      edx
    jne      L401C04
    je       L401C80
L401C6F:                                 ; (pre-increment for the dec below)
    inc      ecx
L401C70:
    dec      ecx
    je       L401C63
    mov      al, byte ptr [esi]          ; (overwrites the key pattern's low byte, see above)
    inc      esi
    inc      edi
    cmp      al, ah
    je       L401C70
    mov      byte ptr [edi-1], al
    jmp      L401C70
L401C80:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_ColorKey endp

; ---------------------------------------------------------------------------------------------
; Blit8_ColorKey_FlipX (0x401C87): Blit8_ColorKey mirrored horizontally: dst points at the
; rightmost pixel of the first destination row, rows are drawn right to left and the end-of-row
; step becomes dstPitch + width.  Same arguments.
; ---------------------------------------------------------------------------------------------
_Blit8_ColorKey_FlipX proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+14h]
    xor      ecx, ecx
    mov      eax, ecx
    mov      ah, byte ptr [ebp+20h]
    mov      al, ah
    mov      cx, ax
    shl      eax, 10h
    add      eax, ecx
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+10h]
    add      dword ptr [ebp+18h], ecx    ; dstPitch -> dstPitch + width
    sub      dword ptr [ebp+1Ch], ecx
    mov      ebx, ecx
L401CB4:                                 ; row loop
    push     edx
    mov      ecx, ebx
    shr      ecx, 2
    je       L401D0C
; skipping copy
L401CBC:
    mov      edx, dword ptr [esi]
    cmp      edx, eax
    je       L401CD5
    cmp      dl, ah
    jne      L401CE6
L401CC6:
    cmp      dh, ah
    jne      L401CEC
L401CCA:
    shr      edx, 10h
    cmp      dl, ah
    jne      L401CF6
L401CD1:
    cmp      dh, ah
    jne      L401CFD
L401CD5:
    add      edi, -4                     ; 4 pixels to the left
    add      esi, 4
    dec      ecx
    jne      L401CBC
    je       L401D0C
; drawing copy: source pixels 0-3 go to edi, edi-1, edi-2, edi-3
L401CE0:
    mov      edx, dword ptr [esi]
    cmp      dl, ah
    je       L401CC6
L401CE6:
    mov      byte ptr [edi], dl
    cmp      dh, ah
    je       L401CCA
L401CEC:
    mov      byte ptr [edi-1], dh
    shr      edx, 10h
    cmp      dl, ah
    je       L401CD1
L401CF6:
    mov      byte ptr [edi-2], dl
    cmp      dh, ah
    je       L401CD5
L401CFD:
    mov      byte ptr [edi-3], dh
    add      edi, -4
    add      esi, 4
    dec      ecx
    jne      L401CE0
    je       L401D0C
    ALIGN4
; leftover pixels
L401D0C:
    mov      ecx, ebx
    and      ecx, 3
    jne      L401D1F
L401D13:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      edx
    dec      edx
    jne      L401CB4
    je       L401D30
L401D1F:
    inc      ecx
L401D20:
    dec      ecx
    je       L401D13
    mov      al, byte ptr [esi]
    inc      esi
    dec      edi
    cmp      al, ah
    je       L401D20
    mov      byte ptr [edi+1], al        ; (edi was already decremented)
    jmp      L401D20
L401D30:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_ColorKey_FlipX endp

; ---------------------------------------------------------------------------------------------
; Blit8_Blend_FlipX (0x401D37): mirrored translucent copy (see Blit8_Blend).
;   [ebp+08h] dst       rightmost pixel of the first destination row
;   [ebp+0Ch] src
;   [ebp+10h] width
;   [ebp+14h] height
;   [ebp+18h] dstPitch  becomes dstPitch + width
;   [ebp+1Ch] srcPitch  becomes srcPitch - width
;   [ebp+20h] key
;   [ebp+24h] blend     64 KB table: dst = blend[dst * 256 + src]
; Registers: bl source pixel, bh destination pixel (ebx = the table index), edx blend table,
; ecx groups (the width and rows are saved on the stack), ah key.
; NB the leftover pixels (width & 3) land two pixels too far left (edi is decremented before
; [edi-1] is used; compare Blit8_ColorKey_FlipX).
; ---------------------------------------------------------------------------------------------
_Blit8_Blend_FlipX proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]
    mov      edx, dword ptr [ebp+14h]
    mov      ah, byte ptr [ebp+20h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    add      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    xor      ebx, ebx
    ALIGN4
L401D58:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+24h]    ; edx = blend table
    shr      ecx, 2
    je       L401DD4
    ALIGN4
; skipping copy
L401D64:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    jne      L401D92
L401D6A:
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    jne      L401DA0
L401D71:
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    jne      L401DB0
L401D78:
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    jne      L401DC0
L401D7F:
    add      edi, -4
    add      esi, 4
    dec      ecx
    jne      L401D64
    jmp      L401DD4
    ALIGN4
; drawing copy
L401D8C:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    je       L401D6A
L401D92:
    mov      bh, byte ptr [edi]          ; destination pixel
    mov      al, byte ptr [edx+ebx]      ; blend[dst * 256 + src]
    mov      byte ptr [edi], al
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    je       L401D71
L401DA0:
    mov      bh, byte ptr [edi-1]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-1], al
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    je       L401D78
L401DB0:
    mov      bh, byte ptr [edi-2]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-2], al
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    je       L401D7F
L401DC0:
    mov      bh, byte ptr [edi-3]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-3], al
    add      edi, -4
    add      esi, 4
    dec      ecx
    jne      L401D8C
    jmp      L401DD4
; leftover pixels
L401DD4:
    pop      ecx
    push     ecx
    and      ecx, 3
    jne      L401DEC
L401DDB:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L401D58
    je       L401E04
L401DEC:
    inc      ecx
L401DED:
    dec      ecx
    je       L401DDB
    inc      esi
    dec      edi
    mov      bl, byte ptr [esi-1]
    cmp      ah, bl
    je       L401DED
    mov      bh, byte ptr [edi-1]        ; sic: two pixels left of the leftover's place
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-1], al
    jmp      L401DED
L401E04:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_Blend_FlipX endp

; ---------------------------------------------------------------------------------------------
; Blit8_Silhouette (0x401E0B): draws the shape of an image in one colour: every non-key source
; pixel becomes the colour.
;   [ebp+08h]-[ebp+20h] as Blit8_ColorKey (dst, src, width, height, dstPitch, srcPitch, key)
;   [ebp+24h] colour
; Registers: al key, ah colour, esi src, edi dst, ecx groups, ebx width, edx rows.
; ---------------------------------------------------------------------------------------------
_Blit8_Silhouette proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]
    mov      edx, dword ptr [ebp+14h]
    mov      al, byte ptr [ebp+20h]
    mov      ah, byte ptr [ebp+24h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    mov      ebx, ecx
    ALIGN4
L401E30:                                 ; row loop
    mov      ecx, ebx
    shr      ecx, 2
    je       L401E84
    ALIGN4
; skipping copy
L401E38:
    cmp      al, byte ptr [esi]
    jne      L401E5C
L401E3C:
    cmp      al, byte ptr [esi+1]
    jne      L401E63
L401E41:
    cmp      al, byte ptr [esi+2]
    jne      L401E6B
L401E46:
    cmp      al, byte ptr [esi+3]
    jne      L401E73
L401E4B:
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L401E38
    je       L401E84
    ALIGN4
; drawing copy
L401E58:
    cmp      al, byte ptr [esi]
    je       L401E3C
L401E5C:
    mov      byte ptr [edi], ah
    cmp      al, byte ptr [esi+1]
    je       L401E41
L401E63:
    mov      byte ptr [edi+1], ah
    cmp      al, byte ptr [esi+2]
    je       L401E46
L401E6B:
    mov      byte ptr [edi+2], ah
    cmp      al, byte ptr [esi+3]
    je       L401E4B
L401E73:
    mov      byte ptr [edi+3], ah
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L401E58
    je       L401E84
    ALIGN4
; leftover pixels
L401E84:
    mov      ecx, ebx
    and      ecx, 3
    jne      L401E96
L401E8B:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    dec      edx
    jne      L401E30
    je       L401EA6
L401E96:
    inc      ecx
L401E97:
    dec      ecx
    je       L401E8B
    inc      esi
    inc      edi
    cmp      al, byte ptr [esi-1]
    je       L401E97
    mov      byte ptr [edi-1], ah
    jmp      L401E97
L401EA6:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_Silhouette endp

; ---------------------------------------------------------------------------------------------
; Blit8_Silhouette_FlipX (0x401EAD): Blit8_Silhouette mirrored horizontally (dst = rightmost pixel
; of the first row, end-of-row step dstPitch + width).  Same arguments.
; ---------------------------------------------------------------------------------------------
_Blit8_Silhouette_FlipX proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+14h]
    mov      al, byte ptr [ebp+20h]
    mov      ah, byte ptr [ebp+24h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ebx, dword ptr [ebp+10h]
    add      dword ptr [ebp+18h], ebx    ; dstPitch -> dstPitch + width
    sub      dword ptr [ebp+1Ch], ebx
    ALIGN4
L401ED0:                                 ; row loop
    mov      ecx, ebx
    shr      ecx, 2
    je       L401F24
    ALIGN4
; skipping copy
L401ED8:
    cmp      al, byte ptr [esi]
    jne      L401EFC
L401EDC:
    cmp      al, byte ptr [esi+1]
    jne      L401F03
L401EE1:
    cmp      al, byte ptr [esi+2]
    jne      L401F0B
L401EE6:
    cmp      al, byte ptr [esi+3]
    jne      L401F13
L401EEB:
    add      edi, -4
    add      esi, 4
    dec      ecx
    jne      L401ED8
    je       L401F24
    ALIGN4
; drawing copy
L401EF8:
    cmp      al, byte ptr [esi]
    je       L401EDC
L401EFC:
    mov      byte ptr [edi], ah
    cmp      al, byte ptr [esi+1]
    je       L401EE1
L401F03:
    mov      byte ptr [edi-1], ah
    cmp      al, byte ptr [esi+2]
    je       L401EE6
L401F0B:
    mov      byte ptr [edi-2], ah
    cmp      al, byte ptr [esi+3]
    je       L401EEB
L401F13:
    mov      byte ptr [edi-3], ah
    add      edi, -4
    add      esi, 4
    dec      ecx
    jne      L401EF8
    je       L401F24
    ALIGN4
; leftover pixels
L401F24:
    mov      ecx, ebx
    and      ecx, 3
    jne      L401F38
L401F2B:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    dec      edx
    jne      L401ED0
    je       L401F45
    ALIGN4
L401F38:
    cmp      al, byte ptr [esi]
    je       L401F3E
    mov      byte ptr [edi], ah
L401F3E:
    inc      esi
    dec      edi
    dec      ecx
    je       L401F2B
    jmp      L401F38
L401F45:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_Silhouette_FlipX endp

; ---------------------------------------------------------------------------------------------
; FillRect8_B (0x401F4C): a second rectangle fill, same arguments as FillRect8 (dst, width,
; height, pitch, colour); it clears the direction flag itself.
; ---------------------------------------------------------------------------------------------
_FillRect8_B proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+0Ch]
    mov      edx, dword ptr [ebp+10h]
    mov      al, byte ptr [ebp+18h]      ; eax = colour in all four bytes
    mov      ah, al
    mov      bx, ax
    shl      eax, 10h
    mov      ax, bx
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+14h], ecx
    mov      ebx, ecx                    ; ebx = width
    cld
    ALIGN4
L401F74:                                 ; row loop: width / 4 dwords
    mov      ecx, ebx
    shr      ecx, 2
    je       L401F80
    nop                                  ; (in the original)
    rep stosd
    ALIGN4
L401F80:                                 ; then width & 3 bytes
    mov      ecx, ebx
    and      ecx, 3
    jne      L401F8F
L401F87:                                 ; next row
    add      edi, dword ptr [ebp+14h]
    dec      edx
    jne      L401F74
    je       L401F93
L401F8F:
    rep stosb
    jmp      L401F87
L401F93:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_FillRect8_B endp

; ---------------------------------------------------------------------------------------------
; FlipVertical8_Remap (0x401F9A): flips an image upside down in place, remapping every pixel it
; moves.
;   [ebp+08h] image
;   [ebp+0Ch] width     also the pitch
;   [ebp+10h] height    at least 2 (the loops would run 2^32 times otherwise)
;   [ebp+14h] remap     256-byte table
; Rows j and height-1-j are swapped for j < height / 2; with an odd height the middle row is
; not remapped.  Registers: esi top row, edi bottom row, ebx table, edx pairs of rows left.
; ---------------------------------------------------------------------------------------------
_FlipVertical8_Remap proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      esi, dword ptr [ebp+8]
    mov      ebx, dword ptr [ebp+14h]
    ; edi = image + (height - 1) * width: the last row
    mov      ecx, dword ptr [ebp+10h]
    dec      ecx
    mov      edi, esi
L401FAE:
    add      edi, dword ptr [ebp+0Ch]
    loop     L401FAE
    mov      edx, dword ptr [ebp+10h]    ; edx = height / 2 row pairs
    shr      edx, 1
    xor      eax, eax
L401FBA:                                 ; row pair loop
    push     edi
    push     esi
    push     edx
    xor      edx, edx
    mov      ecx, dword ptr [ebp+0Ch]
L401FC2:                                 ; swap the two rows' pixels, remapping both
    mov      al, byte ptr [edi]
    mov      dl, byte ptr [esi]
    mov      al, byte ptr [ebx+eax]
    mov      dl, byte ptr [ebx+edx]
    mov      byte ptr [edi], dl
    mov      byte ptr [esi], al
    inc      edi
    inc      esi
    loop     L401FC2
    pop      edx
    pop      esi
    pop      edi
    add      esi, dword ptr [ebp+0Ch]    ; top row down
    sub      edi, dword ptr [ebp+0Ch]    ; bottom row up
    dec      edx
    jne      L401FBA
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_FlipVertical8_Remap endp

; ---------------------------------------------------------------------------------------------
; RemapBytes8 (0x401FE7): remaps a buffer in place: [ebp+08h] buffer, [ebp+0Ch] count (at least
; 1), [ebp+10h] 256-byte remap table.
; ---------------------------------------------------------------------------------------------
_RemapBytes8 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      esi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+0Ch]
    mov      ebx, dword ptr [ebp+10h]
    xor      eax, eax
L401FFA:
    mov      al, byte ptr [esi]
    mov      al, byte ptr [ebx+eax]
    mov      byte ptr [esi], al
    inc      esi
    dec      ecx
    jne      L401FFA
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_RemapBytes8 endp

; ---------------------------------------------------------------------------------------------
; CopyRows640 (0x40200C): copies whole 640-byte rows between two 640-pitch buffers (an 8-bit
; 640x480 frame).  NB the source comes first.
;   [ebp+08h] src
;   [ebp+0Ch] dst
;   [ebp+10h] (unused)
;   [ebp+14h] rows
; ---------------------------------------------------------------------------------------------
_CopyRows640 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      esi, dword ptr [ebp+8]
    mov      edi, dword ptr [ebp+0Ch]
    mov      edx, dword ptr [ebp+14h]
L40201D:
    push     edi
    push     esi
    mov      ecx, 0A0h                   ; 160 dwords = 640 bytes
    rep movsd
    pop      esi
    pop      edi
    add      esi, 280h                   ; pitch 640
    add      edi, 280h
    dec      edx
    jne      L40201D
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_CopyRows640 endp

; ---------------------------------------------------------------------------------------------
; StretchDouble320x240To640x480 (0x40203E): doubles a 320x240 8-bit image (pitch 320) into a
; 640x480 one (pitch 640): every source pixel becomes a 2x2 block.
;   [ebp+08h] dst
;   [ebp+0Ch] src
;   [ebp+10h] (decremented by 640, otherwise unused)
; ---------------------------------------------------------------------------------------------
_StretchDouble320x240To640x480 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    cld
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+10h], 280h
    mov      edx, 0F0h                   ; 240 source rows
    ALIGN4
L40205C:
    mov      ecx, 0A0h                   ; 160 pairs of source pixels per row
    ALIGN4
L402064:
    lodsb                                ; pixel a -> ax = a a
    mov      ah, al
    mov      bx, ax
    lodsb                                ; pixel b -> eax = b b a a (bytes a a b b)
    mov      ah, al
    shl      eax, 10h
    mov      ax, bx
    mov      dword ptr [edi+280h], eax   ; the row below
    stosd                                ; this row
    loop     L402064
    add      edi, 280h                   ; skip the row already written
    dec      edx
    jne      L40205C
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_StretchDouble320x240To640x480 endp

; ---------------------------------------------------------------------------------------------
; Clear640x480 (0x40208C): fills a 640x480 8-bit frame (307200 bytes) with the dword [ebp+0Ch]
; (normally one index in all four bytes); [ebp+08h] is the frame.
; ---------------------------------------------------------------------------------------------
_Clear640x480 proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edi, dword ptr [ebp+8]
    mov      eax, dword ptr [ebp+0Ch]
    mov      ecx, 12C00h                 ; 76800 dwords
    rep stosd
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Clear640x480 endp

; ---------------------------------------------------------------------------------------------
; Blit8_ShadeDst (0x4020A8): the source only gives a shape: where the source pixel is not the
; key, the destination pixel is passed through a shade table (shadows).
;   [ebp+08h]-[ebp+20h] as Blit8_ColorKey (dst, src, width, height, dstPitch, srcPitch, key)
;   [ebp+24h] shade     256-byte table: dst = shade[dst]
; Registers: eax key in all bytes (dword test as in Blit8_ColorKey), ecx 4 source pixels (the
; group count is pushed while it is used), edx shade table, bl destination pixel (ebx upper bits
; 0).  NB the leftover loop stores the shaded pixel via al, spoiling the four-key pattern as in
; Blit8_ColorKey (only slower).
; ---------------------------------------------------------------------------------------------
_Blit8_ShadeDst proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+14h]
    xor      ecx, ecx                    ; eax = key * 01010101h
    mov      eax, ecx
    mov      ah, byte ptr [ebp+20h]
    mov      al, ah
    mov      cx, ax
    shl      eax, 10h
    add      eax, ecx
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    xor      ebx, ebx
    ALIGN4
L4020D8:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+24h]    ; edx = shade table
    shr      ecx, 2
    je       L402150
    ALIGN4
; skipping copy
L4020E4:
    push     ecx
    mov      ecx, dword ptr [esi]        ; 4 source pixels
    cmp      ecx, eax
    je       L4020FE
    cmp      cl, ah
    jne      L402113
L4020EF:
    cmp      ch, ah
    jne      L40211E
L4020F3:
    shr      ecx, 10h
    cmp      cl, ah
    jne      L40212E
L4020FA:
    cmp      ch, ah
    jne      L40213B
L4020FE:
    add      edi, 4
    add      esi, 4
    pop      ecx
    dec      ecx
    jne      L4020E4
    je       L402150
    ALIGN4
; drawing copy: dst = shade[dst]
L40210C:
    push     ecx
    mov      ecx, dword ptr [esi]
    cmp      cl, ah
    je       L4020EF
L402113:
    mov      bl, byte ptr [edi]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi], bl
    cmp      ch, ah
    je       L4020F3
L40211E:
    mov      bl, byte ptr [edi+1]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+1], bl
    shr      ecx, 10h
    cmp      cl, ah
    je       L4020FA
L40212E:
    mov      bl, byte ptr [edi+2]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+2], bl
    cmp      ch, ah
    je       L4020FE
L40213B:
    mov      bl, byte ptr [edi+3]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+3], bl
    add      edi, 4
    add      esi, 4
    pop      ecx
    dec      ecx
    jne      L40210C
    je       L402150
; leftover pixels
L402150:
    pop      ecx
    push     ecx
    and      ecx, 3
    jne      L402168
L402157:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L4020D8
    je       L40217E
L402168:
    inc      ecx
L402169:
    dec      ecx
    je       L402157
    inc      esi
    inc      edi
    cmp      ah, byte ptr [esi-1]
    je       L402169
    mov      bl, byte ptr [edi-1]
    mov      al, byte ptr [edx+ebx]      ; (overwrites the key pattern's low byte)
    mov      byte ptr [edi-1], al
    jmp      L402169
L40217E:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_ShadeDst endp

; ---------------------------------------------------------------------------------------------
; Blit8_Remap (0x402185): Blit8_ColorKey drawing each pixel through a remap table.
;   [ebp+08h]-[ebp+20h] as Blit8_ColorKey (dst, src, width, height, dstPitch, srcPitch, key)
;   [ebp+24h] remap     256-byte table: dst = remap[src]
; Registers: ah key, bl source pixel (ebx upper bits 0), edx remap table, ecx groups (width and
; rows saved on the stack).
; ---------------------------------------------------------------------------------------------
_Blit8_Remap proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      edx, dword ptr [ebp+14h]
    mov      ah, byte ptr [ebp+20h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    mov      ecx, dword ptr [ebp+10h]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    xor      ebx, ebx
L4021A4:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+24h]    ; edx = remap table
    shr      ecx, 2
    je       L402218
    ALIGN4
; skipping copy
L4021B0:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    jne      L4021DE
L4021B6:
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    jne      L4021EA
L4021BD:
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    jne      L4021F7
L4021C4:
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    jne      L402204
L4021CB:
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L4021B0
    je       L402218
    ALIGN4
; drawing copy: remap[src]
L4021D8:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    je       L4021B6
L4021DE:
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi], al
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    je       L4021BD
L4021EA:
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+1], al
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    je       L4021C4
L4021F7:
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+2], al
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    je       L4021CB
L402204:
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L4021D8
    je       L402218
    ALIGN4
; leftover pixels
L402218:
    pop      ecx
    push     ecx
    and      ecx, 3
    jne      L402230
L40221F:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L4021A4
    je       L402245
L402230:
    inc      ecx
L402231:
    dec      ecx
    je       L40221F
    inc      esi
    inc      edi
    mov      bl, byte ptr [esi-1]
    cmp      ah, bl
    je       L402231
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-1], al
    jmp      L402231
L402245:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_Remap endp

; ---------------------------------------------------------------------------------------------
; Blit8_Blend (0x40224C): translucent copy through a blend table.
;   [ebp+08h]-[ebp+20h] as Blit8_ColorKey (dst, src, width, height, dstPitch, srcPitch, key)
;   [ebp+24h] blend     64 KB table: dst = blend[dst * 256 + src]
; Registers: bl source pixel, bh destination pixel (ebx = the table index), edx blend table,
; ah key, ecx groups (width and rows saved on the stack).
; ---------------------------------------------------------------------------------------------
_Blit8_Blend proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]
    mov      edx, dword ptr [ebp+14h]
    mov      ah, byte ptr [ebp+20h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    xor      ebx, ebx
    ALIGN4
L40226C:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+24h]    ; edx = blend table
    shr      ecx, 2
    je       L4022E8
    ALIGN4
; skipping copy
L402278:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    jne      L4022A6
L40227E:
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    jne      L4022B4
L402285:
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    jne      L4022C4
L40228C:
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    jne      L4022D4
L402293:
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L402278
    jmp      L4022E8
    ALIGN4
; drawing copy
L4022A0:
    mov      bl, byte ptr [esi]
    cmp      ah, bl
    je       L40227E
L4022A6:
    mov      bh, byte ptr [edi]          ; destination pixel
    mov      al, byte ptr [edx+ebx]      ; blend[dst * 256 + src]
    mov      byte ptr [edi], al
    mov      bl, byte ptr [esi+1]
    cmp      ah, bl
    je       L402285
L4022B4:
    mov      bh, byte ptr [edi+1]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+1], al
    mov      bl, byte ptr [esi+2]
    cmp      ah, bl
    je       L40228C
L4022C4:
    mov      bh, byte ptr [edi+2]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+2], al
    mov      bl, byte ptr [esi+3]
    cmp      ah, bl
    je       L402293
L4022D4:
    mov      bh, byte ptr [edi+3]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi+3], al
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L4022A0
    jmp      L4022E8
; leftover pixels
L4022E8:
    pop      ecx
    push     ecx
    and      ecx, 3
    jne      L402300
L4022EF:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L40226C
    je       L402318
L402300:
    inc      ecx
L402301:
    dec      ecx
    je       L4022EF
    inc      esi
    inc      edi
    mov      bl, byte ptr [esi-1]
    cmp      ah, bl
    je       L402301
    mov      bh, byte ptr [edi-1]
    mov      al, byte ptr [edx+ebx]
    mov      byte ptr [edi-1], al
    jmp      L402301
L402318:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_Blend endp

; ---------------------------------------------------------------------------------------------
; Blit8_ColorKey_ShadeKey (0x40231F): Blit8_ColorKey with a second special index that shades the
; destination instead of being drawn (a sprite with a built-in shadow).
;   [ebp+08h] dst
;   [ebp+0Ch] src
;   [ebp+10h] width
;   [ebp+14h] height
;   [ebp+18h] dstPitch  becomes dstPitch - width
;   [ebp+1Ch] srcPitch  becomes srcPitch - width
;   [ebp+20h] shadeKey  source pixels of this index: dst = shade[dst]
;   [ebp+24h] key       source pixels of this index are not drawn
;   [ebp+28h] shade     256-byte table
; Other pixels are copied.  The 4-pixel loop has three copies: after a transparent pixel
; (L402354), after a copied one (L4023A0) and after a shaded one (L4023E4).
; Registers: al shadeKey, ah key, bl pixel (ebx upper bits 0), edx shade table, ecx groups
; (width and rows saved on the stack).
; ---------------------------------------------------------------------------------------------
_Blit8_ColorKey_ShadeKey proc near
    push     ebp
    mov      ebp, esp
    push     esi
    push     edi
    push     ebx
    push     ecx
    push     edx
    mov      ecx, dword ptr [ebp+10h]
    mov      edx, dword ptr [ebp+14h]
    mov      al, byte ptr [ebp+20h]
    mov      ah, byte ptr [ebp+24h]
    mov      esi, dword ptr [ebp+0Ch]
    mov      edi, dword ptr [ebp+8]
    sub      dword ptr [ebp+18h], ecx
    sub      dword ptr [ebp+1Ch], ecx
    xor      ebx, ebx
    ALIGN4
L402344:                                 ; row loop: save rows, width
    push     edx
    push     ecx
    mov      edx, dword ptr [ebp+28h]    ; edx = shade table
    shr      ecx, 2
    je       L40242C
    ALIGN4
; after a transparent pixel
L402354:
    mov      bl, byte ptr [esi]
    cmp      al, bl
    je       L4023EA
    cmp      ah, bl
    jne      L4023AA
L402362:
    mov      bl, byte ptr [esi+1]
    cmp      al, bl
    je       L4023F8
    cmp      ah, bl
    jne      L4023B7
L402371:
    mov      bl, byte ptr [esi+2]
    cmp      al, bl
    je       L402408
    cmp      ah, bl
    jne      L4023C5
L402380:
    mov      bl, byte ptr [esi+3]
    cmp      al, bl
    je       L402418
    cmp      ah, bl
    jne      L4023D3
L40238F:
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L402354
    je       L40242C
    ALIGN4
; after a copied pixel
L4023A0:
    mov      bl, byte ptr [esi]
    cmp      al, bl
    je       L4023EA
L4023A6:
    cmp      ah, bl
    je       L402362
L4023AA:
    mov      byte ptr [edi], bl
    mov      bl, byte ptr [esi+1]
    cmp      al, bl
    je       L4023F8
L4023B3:
    cmp      ah, bl
    je       L402371
L4023B7:
    mov      byte ptr [edi+1], bl
    mov      bl, byte ptr [esi+2]
    cmp      al, bl
    je       L402408
L4023C1:
    cmp      ah, bl
    je       L402380
L4023C5:
    mov      byte ptr [edi+2], bl
    mov      bl, byte ptr [esi+3]
    cmp      al, bl
    je       L402418
L4023CF:
    cmp      ah, bl
    je       L40238F
L4023D3:
    mov      byte ptr [edi+3], bl
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L4023A0
    je       L40242C
    ALIGN4
; after a shaded pixel
L4023E4:
    mov      bl, byte ptr [esi]
    cmp      al, bl
    jne      L4023A6
L4023EA:                                 ; shade: dst = shade[dst]
    mov      bl, byte ptr [edi]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi], bl
    mov      bl, byte ptr [esi+1]
    cmp      al, bl
    jne      L4023B3
L4023F8:
    mov      bl, byte ptr [edi+1]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+1], bl
    mov      bl, byte ptr [esi+2]
    cmp      al, bl
    jne      L4023C1
L402408:
    mov      bl, byte ptr [edi+2]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+2], bl
    mov      bl, byte ptr [esi+3]
    cmp      al, bl
    jne      L4023CF
L402418:
    mov      bl, byte ptr [edi+3]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi+3], bl
    add      edi, 4
    add      esi, 4
    dec      ecx
    jne      L4023E4
    je       L40242C
; leftover pixels
L40242C:
    pop      ecx
    push     ecx
    and      ecx, 3
    jne      L402444
L402433:                                 ; next row
    add      esi, dword ptr [ebp+1Ch]
    add      edi, dword ptr [ebp+18h]
    pop      ecx
    pop      edx
    dec      edx
    jne      L402344
    je       L402465
L402444:
    inc      ecx
L402445:
    dec      ecx
    je       L402433
    inc      esi
    inc      edi
    mov      bl, byte ptr [esi-1]
    cmp      al, bl
    je       L40245A
    cmp      ah, bl
    je       L402445
    mov      byte ptr [edi-1], bl
    jmp      L402445
L40245A:                                 ; leftover pixel: shade
    mov      bl, byte ptr [edi-1]
    mov      bl, byte ptr [edx+ebx]
    mov      byte ptr [edi-1], bl
    jmp      L402445
L402465:
    pop      edx
    pop      ecx
    pop      ebx
    pop      edi
    pop      esi
    leave
    ret
_Blit8_ColorKey_ShadeKey endp

; ---------------------------------------------------------------------------------------------
; The blitters' uninitialized data, 0x4203b0-0x4213c3 (the start of the game's .bss), followed
; by the game's own uninitialized variables (game_bss.inc, generated from asm/game_bss.txt).
; ---------------------------------------------------------------------------------------------
.data?
col_offsets  dd 512 dup(?)               ; 0x4203b0: affine blits: source offset differences between neighbouring columns
row_ptrs     dd 512 dup(?)               ; 0x420bb0: affine blits: source pointer of each destination row
count_x      dd ?                        ; 0x4213b0: column / group counter
tmp_4213b4   dd ?                        ; 0x4213b4: not referenced
count_y      dd ?                        ; 0x4213b8: row counter
width_px     dd ?                        ; 0x4213bc: destination width (written, never read)
base_offset  dd ?                        ; 0x4213c0: source offset of the first destination pixel

include game_bss.inc                     ; 0x4213c4...: the game's .bss and communal variables

end


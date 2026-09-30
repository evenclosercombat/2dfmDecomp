"""Make a copy of VC6 LINK.EXE that sorts with the qsort of its own era.

usage: python tools/mklink.py OUTDIR      (writes OUTDIR/LINK.EXE; OUTDIR/qsort.obj is a by-product)

LINK orders the import address table by sorting the .idata$4/.idata$5 contributions with msvcrt's
qsort; the comparator only compares DLL names, so the order of the thunks within one DLL is whatever
permutation qsort leaves equal elements in.  The msvcrt.dll of Windows Vista and later has the newer
(VS2005) qsort, the one of 2002 the classic algorithm, so on a current Windows LINK 6.00 lays out
the IAT differently from the original build even for identical input.

The copy made here gets one more section holding qsort.obj from VC6's own LIBC.LIB (the classic
algorithm) and its 15 `call [__imp__qsort]` instructions are redirected to it.  LINK's bound import
directory has to go to make room for the extra section header.  Nothing else changes.
"""
import os, struct, sys
import pefile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolchain
from coff import Coff


def extract_qsort(outdir):
    obj = os.path.join(outdir, "qsort.obj")
    rc, out = toolchain.run(["lib", "/nologo", "/list", "libc.lib"])
    member = [l.strip() for l in out.splitlines() if l.strip().lower().endswith("\\qsort.obj")]
    if rc or len(member) != 1:
        sys.exit("mklink: qsort.obj not found in LIBC.LIB")
    rc, out = toolchain.run(["lib", "/nologo", "/extract:" + member[0], "/out:" + obj, "libc.lib"])
    if rc:
        sys.exit("mklink: " + out)
    return obj


def qsort_code(obj):
    """Lay out qsort.obj's code sections (qsort, shortsort, swap) and resolve their REL32 fixups."""
    c = Coff(obj)
    texts = [s for s in c.sections if s.name == ".text"]
    base, off = {}, 0
    for s in texts:
        base[s.idx] = off
        off += (len(s.data) + 15) & ~15
    code = bytearray(off)
    for s in texts:
        code[base[s.idx]:base[s.idx] + len(s.data)] = s.data
    for s in texts:
        for va, symi, typ in s.relocs:
            assert typ == 20, "unexpected relocation type %d" % typ          # IMAGE_REL_I386_REL32
            sym = c.symbols[symi]
            at = base[s.idx] + va
            disp = base[sym.secnum] + sym.value - (at + 4) + struct.unpack_from("<i", code, at)[0]
            struct.pack_into("<i", code, at, disp)
    entry = [sym for sym in c.symbols.values() if sym.name == "_qsort"][0]
    return bytes(code), base[entry.secnum] + entry.value


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    code, entry = qsort_code(extract_qsort(outdir))
    src = os.path.join(toolchain.VC6, "VC98", "Bin", "LINK.EXE")
    img = bytearray(open(src, "rb").read())
    pe = pefile.PE(data=bytes(img))
    oh = pe.OPTIONAL_HEADER
    opt = oh.get_file_offset()
    salign, falign = oh.SectionAlignment, oh.FileAlignment

    bound = oh.DATA_DIRECTORY[11]
    if bound.Size:
        img[bound.VirtualAddress:bound.VirtualAddress + bound.Size] = bytes(bound.Size)
        struct.pack_into("<II", img, opt + 96 + 8 * 11, 0, 0)
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            struct.pack_into("<I", img, e.struct.get_file_offset() + 4, 0)   # descriptor no longer bound

    last = pe.sections[-1]
    sh = last.get_file_offset() + 40
    if sh + 40 > oh.SizeOfHeaders or any(img[sh:sh + 40]):
        sys.exit("mklink: no room for another section header")
    va = (last.VirtualAddress + last.Misc_VirtualSize + salign - 1) & ~(salign - 1)
    raw = (len(img) + falign - 1) & ~(falign - 1)
    rawsize = (len(code) + falign - 1) & ~(falign - 1)
    img[sh:sh + 40] = struct.pack("<8sIIIIIIHHI", b".qsort", len(code), va, rawsize, raw, 0, 0, 0, 0, 0x60000020)
    struct.pack_into("<H", img, pe.FILE_HEADER.get_file_offset() + 2, pe.FILE_HEADER.NumberOfSections + 1)
    struct.pack_into("<I", img, opt + 56, va + ((len(code) + salign - 1) & ~(salign - 1)))   # SizeOfImage
    struct.pack_into("<I", img, opt + 64, 0)                                                 # CheckSum
    img += bytes(raw - len(img)) + code + bytes(rawsize - len(code))

    slot = [i.address for e in pe.DIRECTORY_ENTRY_IMPORT for i in e.imports if i.name == b"qsort"][0]
    text = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"][0]
    t0, tn = text.PointerToRawData, text.PointerToRawData + text.SizeOfRawData
    call = b"\xff\x15" + struct.pack("<I", slot)
    n, p = 0, img.find(call, t0, tn)
    while p >= 0:
        at = oh.ImageBase + text.VirtualAddress + (p - t0)
        img[p:p + 6] = b"\xe8" + struct.pack("<i", oh.ImageBase + va + entry - (at + 5)) + b"\x90"
        n += 1
        p = img.find(call, p + 6, tn)
    if n != 15:
        sys.exit("mklink: expected 15 qsort calls in LINK.EXE 6.00.8168, found %d" % n)
    open(os.path.join(outdir, "LINK.EXE"), "wb").write(img)


if __name__ == "__main__":
    main(sys.argv[1])

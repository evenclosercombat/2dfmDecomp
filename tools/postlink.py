"""Reproduce the post-link steps the shipped KGT2nd_GAME.exe went through.

1. LINK stamps the current time; the original was linked at 0x3CAC2E94 (2002-04-04 10:37:40 UTC).
   The file header and the export directory carry that stamp.

2. After linking, the executable's resources were rewritten with the Win32 UpdateResource API
   (the resource directories are stamped 0x3DAD9A2E, 2002-10-16).  UpdateResource lays out .rsrc
   differently from CVTRES:
       directory tables (breadth first) | data entries | name strings | data (4-byte aligned)
   and leaves an extra, empty section header behind the resource section (".uro\\x07", 1 byte,
   no raw data).  It updates SizeOfImage but not the checksum, so the checksum that LINK computed
   for the pre-update image (/RELEASE) is left in place.

This script rebuilds .rsrc in that layout from the resources LINK put into the image, so the
repository only needs the .rc source; nothing is copied from the original executable.

3. The original's .rdata section header says read/write (0xC0000040).  LINK cannot have produced
   that: .edata sits inside .rdata, and LINK only merges .edata into .rdata when their attributes
   agree (with /SECTION:.rdata,RW it keeps a separate .edata section); no object contributes a
   writable .rdata either.  So the flag was changed after linking, presumably by the same
   resource-update step that left the stray section header.
"""
import struct, sys
import pefile

LINK_TIMESTAMP = 0x3CAC2E94
RSRC_TIMESTAMP = 0x3DAD9A2E
EXTRA_SECTION = (b".uro\x07\x00\x00\x00", 1, 0xE0000020)   # name, virtual size, characteristics
RDATA_CHARACTERISTICS = 0xC0000040                          # initialized data, read, write
# The checksum LINK computed for the image before the resource update.  It covers the resources of
# April 2002, which the October update replaced (recomputing it over the current resources with the
# original link timestamp gives 0x136DEB, not 0x13655E), so it cannot be derived and is written as is.
LINK_CHECKSUM = 0x0013655E
# The stray section header also carries junk in PointerToRelocations/PointerToLinenumbers (like its
# name, apparently uninitialized memory of the resource-update code).
EXTRA_SECTION_JUNK = (0xF161E048, 0x0E8C8F9D)


def read_tree(pe):
    """Return {type: {name: {lang: (codepage, bytes)}}} from the image's resource directory."""
    tree = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        tkey = str(t.name) if t.name is not None else t.id
        for n in t.directory.entries:
            nkey = str(n.name) if n.name is not None else n.id
            for l in n.directory.entries:
                d = l.data.struct
                tree.setdefault(tkey, {}).setdefault(nkey, {})[l.id] = (d.CodePage, pe.get_data(d.OffsetToData, d.Size))
    return tree


def ordered(keys):
    """Named entries first (sorted case-insensitively, as the loader requires), then ids ascending."""
    names = sorted([k for k in keys if isinstance(k, str)], key=lambda s: s.upper())
    ids = sorted(k for k in keys if isinstance(k, int))
    return names + ids


def build_rsrc(tree, rva):
    # breadth-first list of directories: (key-path, keys)
    level0 = ordered(tree)
    dirs = [((), level0)]
    for t in level0:
        dirs.append(((t,), ordered(tree[t])))
    for t in level0:
        for n in ordered(tree[t]):
            dirs.append(((t, n), sorted(tree[t][n])))
    dir_off, off = {}, 0
    for path, keys in dirs:
        dir_off[path] = off
        off += 16 + 8 * len(keys)
    leaves = [(t, n, l) for t in level0 for n in ordered(tree[t]) for l in sorted(tree[t][n])]
    entry_off = {}
    for leaf in leaves:
        entry_off[leaf] = off
        off += 16
    str_off = {}
    for path, keys in dirs:
        for k in keys:
            if isinstance(k, str) and k not in str_off:
                str_off[k] = off
                off += 2 + 2 * len(k)
    data_off = {}
    for leaf in leaves:
        off = (off + 3) & ~3
        data_off[leaf] = off
        off += len(tree[leaf[0]][leaf[1]][leaf[2]][1])
    out = bytearray(off)
    for path, keys in dirs:
        named = sum(isinstance(k, str) for k in keys)
        o = dir_off[path]
        struct.pack_into("<IIHHHH", out, o, 0, RSRC_TIMESTAMP, 0, 0, named, len(keys) - named)
        for i, k in enumerate(keys):
            name = (0x80000000 | str_off[k]) if isinstance(k, str) else k
            child = path + (k,)
            target = (0x80000000 | dir_off[child]) if child in dir_off else entry_off[child]
            struct.pack_into("<II", out, o + 16 + 8 * i, name, target)
    for k, o in str_off.items():
        struct.pack_into("<H", out, o, len(k))
        out[o + 2:o + 2 + 2 * len(k)] = k.encode("utf-16-le")
    for leaf in leaves:
        cp, data = tree[leaf[0]][leaf[1]][leaf[2]]
        struct.pack_into("<IIII", out, entry_off[leaf], rva + data_off[leaf], len(data), cp, 0)
        out[data_off[leaf]:data_off[leaf] + len(data)] = data
    return bytes(out)


def main(path):
    pe = pefile.PE(data=open(path, "rb").read())    # not PE(path): that keeps the file mapped
    img = bytearray(pe.__data__)
    fh_off = pe.DOS_HEADER.e_lfanew + 4
    opt_off = fh_off + 20
    sect_off = opt_off + pe.FILE_HEADER.SizeOfOptionalHeader
    salign = pe.OPTIONAL_HEADER.SectionAlignment

    # 1. link timestamp (file header + export directory)
    struct.pack_into("<I", img, fh_off + 4, LINK_TIMESTAMP)
    exp = pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
    if exp.Size:
        struct.pack_into("<I", img, pe.get_offset_from_rva(exp.VirtualAddress) + 4, LINK_TIMESTAMP)

    # 2. resource section in UpdateResource layout
    rs_idx = [i for i, s in enumerate(pe.sections) if s.Name.rstrip(b"\0") == b".rsrc"][0]
    rs = pe.sections[rs_idx]
    blob = build_rsrc(read_tree(pe), rs.VirtualAddress)
    assert len(blob) <= rs.SizeOfRawData
    img[rs.PointerToRawData:rs.PointerToRawData + rs.SizeOfRawData] = blob + bytes(rs.SizeOfRawData - len(blob))
    struct.pack_into("<I", img, sect_off + 40 * rs_idx + 8, len(blob))          # VirtualSize
    struct.pack_into("<I", img, opt_off + 96 + 8 * 2 + 4, len(blob))            # resource directory size

    struct.pack_into("<I", img, opt_off + 64, LINK_CHECKSUM)

    # 3. .rdata marked writable
    for i, s in enumerate(pe.sections):
        if s.Name.rstrip(b"\0") == b".rdata":
            struct.pack_into("<I", img, sect_off + 40 * i + 36, RDATA_CHARACTERISTICS)

    # extra empty section header after .rsrc
    n = pe.FILE_HEADER.NumberOfSections
    assert rs_idx == n - 1
    va = (rs.VirtualAddress + len(blob) + salign - 1) & ~(salign - 1)
    name, vsize, chars = EXTRA_SECTION
    struct.pack_into("<8sIIIIIIHHI", img, sect_off + 40 * n, name, vsize, va, 0, len(img), *EXTRA_SECTION_JUNK, 0, 0, chars)
    struct.pack_into("<H", img, fh_off + 2, n + 1)
    struct.pack_into("<I", img, opt_off + 56, va + salign)                      # SizeOfImage
    open(path, "wb").write(img)


if __name__ == "__main__":
    main(sys.argv[1])

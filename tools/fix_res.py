"""Post-process the .res produced by RC 5.00.1641 (the RC shipped with VC6) so that it matches the
output of the (newer) resource compiler that built the original executable.

Two behaviours differ:
  1. VERSIONINFO: the newer RC counts the padding after a block's last child in the block's
     length.  RC 5.00 does not.  Content is otherwise identical.
  2. RC 5.00 cannot place a U+0000 inside a dialog control's text (it truncates at "\\0").  The
     original .rc had text whose characters were flattened to their low byte, and one of them
     (a full-width space, U+3000) became U+0000.  EMBEDDED_NULS lists those controls.
"""
import struct, sys

# (dialog name, control index) -> number of U+0000 characters that end the control text
EMBEDDED_NULS = {("DIALOG_GAMESPEEDSETUP", 24): 1}


def parse(b):
    """Split a .res file into [header_bytes, type, name, data]."""
    out, i = [], 0
    while i < len(b):
        dsize, hsize = struct.unpack_from("<II", b, i)
        hdr = b[i:i + hsize]
        j = i + 8
        ids = []
        for _ in range(2):
            if struct.unpack_from("<H", b, j)[0] == 0xffff:
                ids.append(struct.unpack_from("<H", b, j + 2)[0]); j += 4
            else:
                e = j
                while b[e:e + 2] != b"\0\0":
                    e += 2
                ids.append(b[j:e].decode("utf-16-le")); j = e + 2
        out.append([hdr, ids[0], ids[1], b[i + hsize:i + hsize + dsize]])
        i = (i + hsize + dsize + 3) & ~3
    return out


def build(entries):
    b = bytearray()
    for hdr, typ, name, data in entries:
        hdr = bytearray(hdr)
        struct.pack_into("<I", hdr, 0, len(data))
        b += hdr + data
        b += b"\0" * (-len(b) % 4)
    return bytes(b)


def fix_version(data):
    data = bytearray(data)

    def node(i):
        """Rewrite node lengths bottom-up; return the aligned end offset of this node."""
        ln, vlen, typ = struct.unpack_from("<HHH", data, i)
        e = i + 6
        while data[e:e + 2] != b"\0\0":
            e += 2
        j = (e + 2 + 3) & ~3
        vbytes = vlen * 2 if typ == 1 else vlen
        j = (j + vbytes + 3) & ~3 if vlen else j
        end = i + ln
        child = j
        last_end = None
        while child < end:
            clen = struct.unpack_from("<H", data, child)[0]
            if clen == 0:
                break
            last_end = node(child)
            child = last_end
        if last_end is not None:
            struct.pack_into("<H", data, i, last_end - i)
        return (i + struct.unpack_from("<H", data, i)[0] + 3) & ~3
    node(0)
    return bytes(data)


def fix_dialog(name, data):
    for (dlg, idx), n in EMBEDDED_NULS.items():
        if dlg == name:
            count = struct.unpack_from("<H", data, 8)[0]
            assert idx == count - 1, "only the last control is supported"
            # the last control's text is followed only by its 2-byte creation-data count
            data = data[:-2] + b"\0\0" * n + data[-2:]
    return data


def main(src, dst):
    entries = parse(open(src, "rb").read())
    for e in entries:
        if e[1] == 16:
            e[3] = fix_version(e[3])
        elif e[1] == 5:
            e[3] = fix_dialog(e[2], e[3])
    open(dst, "wb").write(build(entries))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1])

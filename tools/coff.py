"""Minimal COFF object reader (i386) for function matching."""
import struct


class Section:
    def __init__(self, idx, name, data, relocs, chars):
        self.idx, self.name, self.data, self.relocs, self.chars = idx, name, data, relocs, chars


class Symbol:
    def __init__(self, idx, name, value, secnum, typ, sclass):
        self.idx, self.name, self.value, self.secnum, self.type, self.sclass = idx, name, value, secnum, typ, sclass


class Coff:
    def __init__(self, path_or_bytes):
        b = open(path_or_bytes, "rb").read() if isinstance(path_or_bytes, str) else path_or_bytes
        self.raw = b
        machine, nsec, ts, symptr, nsym, optsz, ch = struct.unpack_from("<HHIIIHH", b, 0)
        self.symptr, self.nsym = symptr, nsym
        strtab_off = symptr + nsym * 18
        self.strtab = b[strtab_off:]
        self.sections = []
        for i in range(nsec):
            o = 20 + optsz + i * 40
            name = b[o:o + 8].rstrip(b"\0")
            vs, va, rawsz, rawptr, relptr, lnptr, nrel, nln, chars = struct.unpack_from("<IIIIIIHHI", b, o + 8)
            if name.startswith(b"/"):
                name = self._str(int(name[1:]))
            data = b[rawptr:rawptr + rawsz] if rawptr else bytes(rawsz)
            relocs = []
            for r in range(nrel):
                va_, symi, typ = struct.unpack_from("<IIH", b, relptr + r * 10)
                relocs.append((va_, symi, typ))
            self.sections.append(Section(i + 1, name.decode("latin1"), data, relocs, chars))
        self.symbols = {}
        i = 0
        while i < nsym:
            o = symptr + i * 18
            raw = b[o:o + 8]
            if raw[:4] == b"\0\0\0\0":
                name = self._str(struct.unpack_from("<I", raw, 4)[0])
            else:
                name = raw.rstrip(b"\0")
            value, secnum, typ, sclass, naux = struct.unpack_from("<IhHBB", b, o + 8)
            self.symbols[i] = Symbol(i, name.decode("latin1"), value, secnum, typ, sclass)
            i += 1 + naux

    def _str(self, off):
        e = self.strtab.index(b"\0", off)
        return self.strtab[off:e]

    def functions(self):
        """name -> (section, offset, size). Size: to next function symbol in same section or section end."""
        by_sec = {}
        for s in self.symbols.values():
            if s.secnum > 0 and (s.type & 0x20) and s.sclass in (2, 3):   # function, external/static
                by_sec.setdefault(s.secnum, []).append(s)
        out = {}
        for secnum, syms in by_sec.items():
            sec = self.sections[secnum - 1]
            syms.sort(key=lambda s: s.value)
            for k, s in enumerate(syms):
                end = syms[k + 1].value if k + 1 < len(syms) else len(sec.data)
                out[s.name] = (sec, s.value, end - s.value)
        return out

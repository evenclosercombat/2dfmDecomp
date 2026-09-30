"""Generate src/resources.c from rsrc/kgt2nd.rc.

This branch is built by a C compiler alone, without a resource compiler, so the resources the game
uses are compiled into the executable as C data instead of a .rsrc section:

  * BITMAP and ICON: the files in rsrc/assets/, included with #embed;
  * MENU and DIALOG: the binary templates (MENUITEMTEMPLATE / DLGTEMPLATE), written out here as
    WORD arrays, one line per menu item or dialog control with the .rc statement as a comment.
    They are what RC stores in the resource, so LoadMenuIndirect / DialogBoxIndirectParam take them.

VERSIONINFO is not converted (nothing reads it; without a .rsrc section Explorer shows no version).

Only needed when kgt2nd.rc changes; the build compiles the committed src/resources.c.

usage: python tools/gen_resources.py [--check ORIGINAL.exe]
  --check  also compare every generated menu and dialog template with the resources of the
           original executable (needs the pefile package)
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RC = os.path.join(ROOT, "rsrc", "kgt2nd.rc")
OUT = os.path.join(ROOT, "src", "resources.c")

# dialog control classes that RC stores as atoms
CLASS_ATOMS = {"button": 0x80, "edit": 0x81, "static": 0x82, "listbox": 0x83, "scrollbar": 0x84, "combobox": 0x85}
DS_SETFONT = 0x40
MF_POPUP, MF_END = 0x10, 0x80


def tokenize(line):
    toks, i = [], 0
    while i < len(line):
        c = line[i]
        if c.isspace():
            i += 1
        elif line.startswith("//", i):
            break
        elif c == '"':
            s, i = [], i + 1
            while True:
                if line[i] == '"':
                    if i + 1 < len(line) and line[i + 1] == '"':
                        s.append('"'); i += 2; continue
                    i += 1
                    break
                if line[i] == "\\" and i + 1 < len(line):
                    e = line[i + 1]
                    s.append({"n": "\n", "t": "\t", "r": "\r", "a": "\b", "0": "\0", "\\": "\\"}.get(e, "\\" + e))
                    i += 2
                    continue
                s.append(line[i]); i += 1
            toks.append(("str", "".join(s)))
        elif c in ",|":
            toks.append((c, c)); i += 1
        else:
            m = re.match(r"[^\s,|\"]+", line[i:])
            toks.append(("word", m.group(0))); i += len(m.group(0))
    return toks


def num(w):
    w = w.rstrip("lL")
    return int(w, 16) if w.lower().startswith("0x") else int(w)


def expr(toks):
    """a number or numbers joined with |"""
    toks = list(toks)
    v = num(toks.pop(0)[1])
    while toks and toks[0][0] == "|":
        toks.pop(0)
        v |= num(toks.pop(0)[1])
    return v


def args(toks):
    """split a token list at commas"""
    out, cur = [], []
    for t in toks:
        if t[0] == ",":
            out.append(cur); cur = []
        else:
            cur.append(t)
    out.append(cur)
    return out


def text(s):
    """control text, kept whole: the original has one text ending in an extra U+0000 ("Rounds\\0", the
    last control of DIALOG_GAMESPEEDSETUP); Windows reads a text up to its first NUL, and the extra
    WORD it then takes for the creation-data size is 0 - the template is the original's"""
    return s


def parse(path):
    lines = open(path, encoding="utf-16").read().split("\n")
    res = []            # (type, name, data)
    i = 0

    def block():
        nonlocal i
        assert tokenize(lines[i])[0][1] == "BEGIN", lines[i]
        i += 1
        body = []
        depth = 0
        while True:
            t = tokenize(lines[i])
            i += 1
            if not t:
                continue
            if t[0][1] == "BEGIN":
                depth += 1
            elif t[0][1] == "END":
                if depth == 0:
                    return body
                depth -= 1
            else:
                body.append((depth, t))

    while i < len(lines):
        t = tokenize(lines[i])
        if len(t) >= 2 and t[1][1] in ("BITMAP", "ICON"):
            res.append((t[1][1], t[0][1], t[-1][1]))
            i += 1
        elif len(t) >= 2 and t[1][1] == "MENU":
            name = t[0][1]
            i += 1
            res.append(("MENU", name, block()))
        elif len(t) >= 2 and t[1][1] == "DIALOG":
            name = t[0][1]
            rect = [expr(a) for a in args(t[3:] if t[2][1] == "DISCARDABLE" else t[2:])]
            i += 1
            dlg = {"rect": rect, "style": 0, "caption": "", "font": None}
            while tokenize(lines[i])[0][1] != "BEGIN":
                s = tokenize(lines[i])
                if s[0][1] == "STYLE":
                    dlg["style"] = expr(s[1:])
                elif s[0][1] == "CAPTION":
                    dlg["caption"] = s[1][1]
                elif s[0][1] == "FONT":
                    a = args(s[1:])
                    dlg["font"] = (expr(a[0]), a[1][0][1])
                i += 1
            items = block()
            dlg["controls"] = []
            for depth, s in items:
                a = args(s[1:])
                assert s[0][1] == "CONTROL"
                dlg["controls"].append({"text": a[0][0][1], "id": expr(a[1]), "class": a[2][0][1],
                                        "style": expr(a[3]), "rect": [expr(x) for x in a[4:8]]})
            res.append(("DIALOG", name, dlg))
        elif len(t) >= 2 and t[1][1] == "VERSIONINFO":
            i += 1
            while tokenize(lines[i])[0][1] != "BEGIN":
                i += 1
            block()
        else:
            i += 1
    return res


class Words:
    """a WORD array with a comment per line"""
    def __init__(self):
        self.w, self.lines = [], []

    def add(self, words, comment):
        self.lines.append((len(self.w), len(words), comment))
        self.w += words

    def align4(self):
        if len(self.w) % 2:
            self.add([0], "(padding to a DWORD boundary)")


def s_words(s):
    return [ord(c) for c in s] + [0]


def fmt_word(v, as_char):
    if as_char and 0x20 <= v < 0x7f and chr(v) not in "'\\":
        return "'%s'" % chr(v)
    return "0x%04x" % v


def menu_template(body):
    w = Words()
    w.add([0, 0], "MENUITEMTEMPLATEHEADER: version 0, offset 0")
    # body: (depth, tokens) with POPUP entries followed by their items one level deeper
    def level(items, start, depth, indent):
        k = start
        entries = []
        while k < len(items) and items[k][0] >= depth:
            if items[k][0] == depth:
                entries.append(k)
            k += 1
        for n, e in enumerate(entries):
            d, t = items[e]
            end = MF_END if n == len(entries) - 1 else 0
            if t[0][1] == "POPUP":
                w.add([MF_POPUP | end] + s_words(t[1][1]), '%sPOPUP "%s"' % (indent, t[1][1]))
                level(items, e + 1, depth + 1, indent + "    ")
            else:
                a = args(t[1:])
                w.add([end, expr(a[1])] + s_words(a[0][0][1]), '%sMENUITEM "%s", %d' % (indent, a[0][0][1].replace("\t", "\\t"), expr(a[1])))
    level(body, 0, 0, "")
    return w


def dialog_template(d):
    w = Words()
    style = d["style"] | (DS_SETFONT if d["font"] else 0)
    x, y, cx, cy = d["rect"]
    w.add([style & 0xffff, style >> 16, 0, 0], "DLGTEMPLATE: style 0x%08X, no extended style" % style)
    w.add([len(d["controls"]), x, y, cx, cy], "%d controls; x, y, cx, cy" % len(d["controls"]))
    w.add([0], "no menu")
    w.add([0], "default dialog class")
    w.add(s_words(d["caption"]), 'CAPTION "%s"' % d["caption"] if d["caption"] else "no caption")
    if d["font"]:
        w.add([d["font"][0]] + s_words(d["font"][1]), "FONT %d, (face name)" % d["font"][0])
    for c in d["controls"]:
        w.align4()
        st = c["style"]
        cls = CLASS_ATOMS.get(c["class"].lower())
        clsw = [0xffff, cls] if cls else s_words(c["class"])
        t = text(c["text"])
        shown = "".join(ch if 0x20 <= ord(ch) < 0x7f else "?" for ch in t)
        w.add([st & 0xffff, st >> 16, 0, 0] + c["rect"] + [c["id"] & 0xffff] + clsw + s_words(t) + [0],
              'CONTROL "%s", %d, "%s", 0x%08X, %s' % (shown, c["id"], c["class"], st, ", ".join(map(str, c["rect"]))))
    return w


def c_ident(name):
    return re.sub(r"\W", "_", name)


def emit(res):
    out = []
    out.append("""/*
 * resources.c - the resources of rsrc/kgt2nd.rc, compiled in as C data.
 *
 * GENERATED by tools/gen_resources.py from rsrc/kgt2nd.rc - edit the .rc and rerun it.
 *
 * This branch is built by the C compiler alone (no resource compiler), so the executable has no
 * .rsrc section: the bitmaps and the icon are the files of rsrc/assets/ (#embed), the menu and the
 * dialogs the binary templates a resource compiler would have produced.  The functions at the end
 * stand in for FindResource/LoadResource/LockResource, LoadIcon, LoadMenu and DialogBoxParam
 * (main.c, dialogs.c and online.c call them).  VERSIONINFO is not included.
 */
#include "kgt.h"
""")
    table = []
    for typ, name, data in res:
        ident = "gRc_" + c_ident(name)
        if typ in ("BITMAP", "ICON"):
            path = "../rsrc/assets/" + data
            if typ == "BITMAP":
                out.append("/* BITMAP %s: the .bmp file; the resource is what follows its 14-byte BITMAPFILEHEADER.\n"
                           "   Two leading bytes keep that BITMAPINFOHEADER 4-byte aligned. */" % name)
                out.append("static const BYTE %s[] __attribute__((aligned(4))) = {\n    0, 0,\n#embed \"%s\"\n};\n" % (ident, path))
                table.append(("RT_BITMAP", name, ident + " + 2 + sizeof(BITMAPFILEHEADER)", "sizeof(%s) - 2 - sizeof(BITMAPFILEHEADER)" % ident))
            else:
                out.append("/* ICON %s: the .ico file (ICONDIR, ICONDIRENTRY[], images) */" % name)
                out.append("static const BYTE %s[] __attribute__((aligned(4))) = {\n#embed \"%s\"\n};\n" % (ident, path))
                table.append(("RT_ICON", name, ident, "sizeof(%s)" % ident))
            continue
        if typ == "MENU":
            w = menu_template(data)
            rt = "RT_MENU"
            out.append("/* MENU %s: MENUITEMTEMPLATEHEADER and MENUITEMTEMPLATEs (MF_POPUP 0x10, MF_END 0x80 on the\n"
                       "   last item of each level), texts in UTF-16 */" % name)
        else:
            w = dialog_template(data)
            rt = "RT_DIALOG"
            out.append("/* DIALOG %s: DLGTEMPLATE and DWORD-aligned DLGITEMTEMPLATEs (style, extended style, x, y, cx, cy,\n"
                       "   id, class (0xffff + atom: 0x80 Button, 0x81 Edit, 0x82 Static, 0x83 ListBox, 0x85 ComboBox),\n"
                       "   text, 0 bytes of creation data), texts in UTF-16 as the original's (see the .rc) */" % name)
        out.append("static const WORD %s[] __attribute__((aligned(4))) = {" % ident)
        for start, n, comment in w.lines:
            chunk = w.w[start:start + n]
            as_char = "CONTROL" in comment or "MENUITEM" in comment or "POPUP" in comment or "CAPTION" in comment or "FONT" in comment
            comment = "".join(ch if 0x20 <= ord(ch) < 0x7f else "?" for ch in comment).replace("*/", "* /")
            out.append("    %s,%s" % (", ".join(fmt_word(v, as_char) for v in chunk), ("  /* %s */" % comment) if comment else ""))
        out.append("};\n")
        table.append((rt, name, ident, "sizeof(%s)" % ident))
    out.append("""typedef struct kgtEmbeddedResource {
    LPCSTR szType;      /* RT_BITMAP, RT_ICON (here: a whole .ico file), RT_MENU or RT_DIALOG */
    LPCSTR szName;      /* name as in the .rc (compared without case), or MAKEINTRESOURCE(id) */
    const void *pData;
    DWORD dwSize;
} kgtEmbeddedResource;

static const kgtEmbeddedResource gEmbeddedResources[] = {""")
    for rt, name, data, size in table:
        nm = "MAKEINTRESOURCEA(%s)" % name if name.isdigit() else '"%s"' % name
        out.append("    { %s, %s, %s, %s }," % (rt, nm, data, size))
    out.append("""};

/*
 * FindResourceA + LoadResource + LockResource for the embedded resources.
 * szName: resource name (any case) or MAKEINTRESOURCE(id); szType: RT_BITMAP, RT_ICON, RT_MENU or
 * RT_DIALOG; pdwSize: receives the size (may be NULL).
 * Returns the resource data, or NULL if there is no such resource.
 */
const void *pLockEmbeddedResource(LPCSTR szName, LPCSTR szType, DWORD *pdwSize)
{
    int i;

    for (i = 0; i < (int)(sizeof(gEmbeddedResources) / sizeof(gEmbeddedResources[0])); i++) {
        const kgtEmbeddedResource *pRes = &gEmbeddedResources[i];

        if (pRes->szType != szType)
            continue;
        if (IS_INTRESOURCE(szName) || IS_INTRESOURCE(pRes->szName)) {
            if (szName != pRes->szName)
                continue;
        } else if (lstrcmpiA(szName, pRes->szName) != 0) {
            continue;
        }
        if (pdwSize)
            *pdwSize = pRes->dwSize;
        return pRes->pData;
    }
    return NULL;
}

/*
 * LoadIconA for the embedded icon: picks the image of the .ico whose size is the system's icon size
 * (else the first one) with the most colours, as LoadIcon would from the icon group.
 * szName: icon name.
 * Returns the icon, or NULL.
 */
HICON hLoadEmbeddedIcon(LPCSTR szName)
{
    const BYTE *pIco;
    const BYTE *pEntry;
    const BYTE *pBest;
    int iCount, i, cx, cy;

    pIco = pLockEmbeddedResource(szName, RT_ICON, NULL);
    if (pIco == NULL)
        return NULL;
    cx = GetSystemMetrics(SM_CXICON);
    cy = GetSystemMetrics(SM_CYICON);
    /* ICONDIR: WORD reserved, WORD type (1), WORD count; ICONDIRENTRY (16 bytes): BYTE width,
       BYTE height, BYTE colours, BYTE reserved, WORD planes, WORD bits, DWORD size, DWORD offset */
    iCount = *(const WORD *)(pIco + 4);
    pBest = NULL;
    for (i = 0; i < iCount; i++) {
        pEntry = pIco + 6 + i * 16;
        if (pBest == NULL
            || (pEntry[0] == cx && pEntry[1] == cy
                && (pBest[0] != cx || pBest[1] != cy || *(const WORD *)(pEntry + 6) > *(const WORD *)(pBest + 6))))
            pBest = pEntry;
    }
    if (pBest == NULL)
        return NULL;
    return CreateIconFromResourceEx((PBYTE)pIco + *(const DWORD *)(pBest + 12), *(const DWORD *)(pBest + 8),
                                    TRUE, 0x00030000, cx, cy, LR_DEFAULTCOLOR);
}

/*
 * LoadMenuA for the embedded menu.
 * szName: menu name.
 * Returns the menu, or NULL.
 */
HMENU hLoadEmbeddedMenu(LPCSTR szName)
{
    const void *pTemplate = pLockEmbeddedResource(szName, RT_MENU, NULL);

    return pTemplate ? LoadMenuIndirectA(pTemplate) : NULL;
}

/*
 * DialogBoxParamA with an embedded dialog template.
 * hInstance, hWndParent, pfnDialog, lParam: as for DialogBoxParamA; szTemplate: dialog name or
 * MAKEINTRESOURCE(id).
 * Returns the EndDialog value, or -1 on failure.
 */
INT_PTR iEmbeddedDialogBoxParamA(HINSTANCE hInstance, LPCSTR szTemplate, HWND hWndParent, DLGPROC pfnDialog, LPARAM lParam)
{
    const void *pTemplate = pLockEmbeddedResource(szTemplate, RT_DIALOG, NULL);

    if (pTemplate == NULL)
        return -1;
    return DialogBoxIndirectParamA(hInstance, (LPCDLGTEMPLATEA)pTemplate, hWndParent, pfnDialog, lParam);
}""")
    return "\n".join(out) + "\n"


def check(res, exe):
    import pefile
    pe = pefile.PE(exe)
    types = {"MENU": 4, "DIALOG": 5}
    found = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for n in t.directory.entries:
            key = str(n.name) if n.name is not None else str(n.id)
            e = n.directory.entries[0]
            d = e.data.struct
            found[(t.id, key.upper())] = pe.get_data(d.OffsetToData, d.Size)
    bad = 0
    for typ, name, data in res:
        if typ not in types:
            continue
        w = menu_template(data) if typ == "MENU" else dialog_template(data)
        mine = struct.pack("<%dH" % len(w.w), *w.w)
        orig = found.get((types[typ], name.upper()))
        if orig is None:
            print("%s %s: not in the executable" % (typ, name)); bad += 1
        elif mine == orig:
            print("%s %s: identical" % (typ, name))
        else:
            print("%s %s: DIFFERENT (%d vs %d bytes)" % (typ, name, len(mine), len(orig))); bad += 1
            for k in range(0, max(len(mine), len(orig)), 2):
                if mine[k:k + 2] != orig[k:k + 2]:
                    print("   first difference at byte %d: %r vs %r" % (k, mine[k:k + 16], orig[k:k + 16])); break
    return bad


def main(argv):
    res = parse(RC)
    open(OUT, "w", encoding="ascii", newline="\r\n").write(emit(res))
    print("wrote", OUT)
    if "--check" in argv:
        return 1 if check(res, argv[argv.index("--check") + 1]) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

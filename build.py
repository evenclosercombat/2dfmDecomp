"""Build KGT2nd_GAME.exe from source and check that it is identical to the original (by SHA-256).

usage: python build.py [--no-verify] [--nonmatching]

  --no-verify    skip the SHA-256 check
  --nonmatching  compile the C versions of the functions that do not match yet (see README.md)

Steps (all output goes to build/):
  1. RC 5.00 -> kgt2nd.res (images from rsrc/assets/), then tools/fix_res.py
  2. asm/game_bss.txt -> asm/game_bss.inc; JWasm assembles asm/blit.asm into an OMF object
  3. CL each translation unit (C: /Ox /Oa /W3; C++: plus /GX)
  4. LINK in the original order with the empty-export .def, as exe.exe (the export directory
     records the output name), using a copy of LINK.EXE with the classic qsort (tools/mklink.py)
  5. tools/postlink.py: timestamps, UpdateResource-style .rsrc, section header, SizeOfImage
  6. copy to build/KGT2nd_GAME.exe and compare its SHA-256 with the original's known hash

The build never reads the original executable.  The development repository (kgt2nd_decomp) has the
tools that show where a build that does not match differs (tools/verify.py needs the original).
"""
import hashlib, os, shutil, sys

# SHA-256 of the shipped KGT2nd_GAME.exe (2D Fighter Maker 2nd)
EXPECTED_SHA256 = "287c6f39aea5265b126dff301af9e8fdf49e6edc78957eeb50aba38e6705326a"

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import toolchain

B = os.path.join(ROOT, "build")
OBJ = os.path.join(B, "obj")
PY = sys.executable

# Translation units in link order (= the order of their code and data in the executable).
SOURCES = [
    "src/online.c",
    "src/globals.c",
    "src/cppunit.cpp",      # the one C++ object of the original (contributes nothing; see the file)
    "src/main.c",
    "src/engine.c",
    "src/battle.c",
    "src/compress.c",
    "src/input.c",
    "src/config.c",
    "src/debug.c",
    "src/cdaudio.c",
    "src/midi.c",
    "src/dsutil.c",
    "src/dialogs.c",
]
# LIBC before the DirectX/winsock import libraries: the game calls DirectSoundCreate, DirectDrawCreate,
# DirectPlay*, WSAStartup, ntohl, gethostbyname and gethostname through import thunks, and in the
# original those thunks follow the whole CRT (with RtlUnwind's, pulled in by the CRT, last).
LIBS = ["kernel32.lib", "user32.lib", "gdi32.lib", "winmm.lib", "libc.lib", "dsound.lib", "ddraw.lib",
        "dplayx.lib", "wsock32.lib"]


def step(name, cmd, **kw):
    print("==", name)
    rc, out = toolchain.run(cmd, **kw)
    out = out.strip()
    if out:
        print(out)
    if rc:
        sys.exit("build failed at: %s" % name)


def main(argv):
    os.makedirs(OBJ, exist_ok=True)

    res = os.path.join(B, "kgt2nd.res")
    step("rc", ["rc", "/i", os.path.join(ROOT, "rsrc", "assets"), "/fo", res, os.path.join(ROOT, "rsrc", "kgt2nd.rc")])
    step("fix_res", [PY, os.path.join(ROOT, "tools", "fix_res.py"), res])

    step("game_bss", [PY, os.path.join(ROOT, "tools", "gen_game_bss.py"),
                      os.path.join(ROOT, "asm", "game_bss.txt"), os.path.join(ROOT, "asm", "game_bss.inc")])
    blit = os.path.join(OBJ, "blit.obj")
    step("jwasm", [toolchain.JWASM, "-nologo", "-omf", "-Fo" + blit, os.path.join(ROOT, "asm", "blit.asm")])

    objs = [blit]
    for src in SOURCES:
        path = os.path.join(ROOT, src)
        if not os.path.exists(path):
            sys.exit("missing source: %s" % src)
        obj = os.path.join(OBJ, os.path.splitext(os.path.basename(src))[0] + ".obj")
        print("== cl", src)
        rc, out = toolchain.cl(path, obj, ["/DNONMATCHING"] if "--nonmatching" in argv else [])
        lines = [l for l in out.splitlines() if l.strip() and l.strip() != os.path.basename(src)]
        if lines:
            print("\n".join(lines))
        if rc:
            sys.exit("build failed at: cl %s" % src)
        objs.append(obj)

    # LINK (/OPT:REF) drops communal variables nobody references; some game_bss.txt commons are
    # only reached through their 'field' names (absolute symbols), so keep them all explicitly.
    import gen_game_bss
    keep = ["/include:_" + n for g, a, s, n in gen_game_bss.load(os.path.join(ROOT, "asm", "game_bss.txt"))
            if g == "common"]
    # LINK with the qsort of its time, or the import address table comes out in a different order
    # (see tools/mklink.py); KGT_LINK overrides.
    linker = toolchain.LINK
    if "KGT_LINK" not in os.environ:
        step("mklink", [PY, os.path.join(ROOT, "tools", "mklink.py"), os.path.join(B, "link")])
        linker = os.path.join(B, "link", "LINK.EXE")
    step("link", [linker, "/nologo", "/subsystem:windows", "/machine:I386", "/incremental:no", "/release",
                  "/def:" + os.path.join(ROOT, "src", "exe.def"), "/map:exe.map", "/out:exe.exe"]
         + keep + [res] + objs + LIBS, cwd=B)     # .res first: gives the original Rich header order
    exe = os.path.join(B, "exe.exe")
    step("postlink", [PY, os.path.join(ROOT, "tools", "postlink.py"), exe])
    final = os.path.join(B, "KGT2nd_GAME.exe")
    shutil.copyfile(exe, final)
    print("built", final)
    if "--no-verify" in argv:
        return 0
    return check_sha256(final, "--nonmatching" in argv)


def check_sha256(path, nonmatching=False):
    """The only check the build makes: the SHA-256 of the result."""
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()
    print("sha256", h)
    if h == EXPECTED_SHA256:
        print("MATCH: identical to the original KGT2nd_GAME.exe")
        return 0
    if nonmatching:
        print("differs from the original, as expected with --nonmatching")
        return 0
    print("DIFFERENT from the original KGT2nd_GAME.exe (expected sha256 %s)" % EXPECTED_SHA256)
    print("to see where: tools/verify.py of the development repository (kgt2nd_decomp) compares a build")
    print("with the original executable section by section")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

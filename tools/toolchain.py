"""Locations of the build tools and the exact options used to reproduce the original build."""
import os, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Visual C++ 6.0 (RTM: cl 12.00.8168, link 6.00.8168, cvtres 5.00.1720)
VC6 = os.environ.get("VC6DIR", r"C:\Program Files (x86)\Microsoft Visual Studio")
# MASM-compatible assembler with OMF output.  Not "JWASM": JWasm reads that variable as extra options.
# Looked up as KGT_JWASM, then tools/bin/JWasm.exe (a place for local tool binaries, not in git),
# then PATH.
JWASM = os.environ.get("KGT_JWASM", "JWasm.exe")
_local_jwasm = os.path.join(ROOT, "tools", "bin", "JWasm.exe")
if "KGT_JWASM" not in os.environ and os.path.exists(_local_jwasm):
    JWASM = _local_jwasm

LINK = os.environ.get("KGT_LINK", "link")

OPTFLAGS = os.environ.get("KGT_OPT", "/Ox /Oa").split()
CFLAGS = ["/nologo", "/c"] + OPTFLAGS + ["/W3", "/I" + os.path.join(ROOT, "include")]
CPPFLAGS = CFLAGS + ["/GX"]


def env():
    e = dict(os.environ)
    e["PATH"] = os.pathsep.join([os.path.join(VC6, "VC98", "Bin"), os.path.join(VC6, "Common", "MSDev98", "Bin"), e["PATH"]])
    e["INCLUDE"] = os.path.join(VC6, "VC98", "Include")
    e["LIB"] = os.path.join(VC6, "VC98", "Lib")
    # Some VC6 installs mark CL.EXE "run as administrator"; don't let that trigger UAC.
    e["__COMPAT_LAYER"] = "RunAsInvoker"
    return e


def run(cmd, **kw):
    r = subprocess.run(cmd, env=env(), capture_output=True, text=True, errors="replace", **kw)
    return r.returncode, (r.stdout + r.stderr)


def cl(src, obj, extra=()):
    flags = CPPFLAGS if src.lower().endswith(".cpp") else CFLAGS
    return run(["cl"] + flags + list(extra) + ["/Fo" + obj, src])

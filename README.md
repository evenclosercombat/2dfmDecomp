# KGT2nd_GAME

C source of `KGT2nd_GAME.exe` that rebuilds the shipped executable byte for byte with the original toolchain:

```
SHA-256 287c6f39aea5265b126dff301af9e8fdf49e6edc78957eeb50aba38e6705326a  KGT2nd_GAME.exe
``

## Requirements

* Windows (the toolchain is 32-bit Windows software; tested on Windows 11).
* **Microsoft Visual C++ 6.0**, RTM (`cl` 12.00.8168, `link` 6.00.8168, `rc`, `cvtres` 5.00.1720).
  The build expects it in `C:\Program Files (x86)\Microsoft Visual Studio`; set `VC6DIR` otherwise.
  The Rich header records these RTM builds; the match has only been checked with them.
* **[JWasm](https://github.com/JWasm/JWasm / https://github.com/Baron-von-Riedesel/JWasm/releases) 2.x** (assembles the blitter into an OMF object). Put
  `JWasm.exe` in `tools/bin/` (ignored by git), on `PATH`, or point `KGT_JWASM` at it.
* **Python 3** with the packages in `requirements.txt` (`pip install -r requirements.txt`).

## Building

```
pip install -r requirements.txt
python build.py
```

The result is `build/KGT2nd_GAME.exe`. The build ends by comparing its SHA-256 with the original's known
hash and prints `MATCH: identical to the original KGT2nd_GAME.exe` (or `DIFFERENT`, exit code 1).
`python build.py --no-verify` skips the check.

What `build.py` does, in order:

1. compiles `rsrc/kgt2nd.rc` (with the images in `rsrc/assets/`) and normalizes the result
   (`tools/fix_res.py`),
2. generates the uninitialized-data layout (`tools/gen_game_bss.py`) and assembles `asm/blit.asm`,
3. compiles the 14 translation units in `src/` with `cl /Ox /Oa`,
4. makes a copy of your `LINK.EXE` that uses the `qsort` of VC6's own C runtime (`tools/mklink.py`)
   and links everything in the original order,
5. reproduces the post-link changes the original went through (`tools/postlink.py`),
6. checks the SHA-256 of the result.

## Functions built from the original's machine code

180 of the 186 C functions compile to exactly the original machine code (the assembler blitter is
reproduced exactly as well). For the other 6, VC6 still picks slightly different registers or stack slots, so on
this branch they are built from the original's machine code (`__declspec(naked)` functions with the bytes
as `_emit` data, plus a dead copy of the C body that keeps each file's string literals and imports in the
original order):

`vjmpHandleBattleInterface` (6 instructions differ), `bReadKgtCore` (15), `vDrawCurrentEngineObject` (46),
`vDrawKgtImage16` (59), `vBlitImageRect16` (60) and `vHandleHitboxEffects` (93).

## Branches

* **`main`** (this branch): the exact byte match of the original executable.
* **`nonmatching`**: the same program with those 6 functions compiled from their C source instead. Its
  executable differs from the original in those functions only and plays the same; its build checks
  against the known hash of that build instead of the original's.

## Layout

| path | contents |
|---|---|
| `build.py` | the build tool |
| `src/*.c` | the game, one file per original translation unit, in link order (see `SOURCES` in `build.py`) |
| `src/cppunit.cpp` | stand-in for the original's one C++ translation unit, which left no code behind |
| `src/exe.def` | the original's empty `EXPORTS` .def file (hence the empty export directory) |
| `include/` | types (`kgt_types.h`), globals, prototypes |
| `asm/blit.asm` | the hand-written 8-bit blitter (the original was an assembler object) |
| `asm/game_bss.txt` | address, size and name of every uninitialized global |
| `rsrc/kgt2nd.rc`, `rsrc/assets/` | resources (dialogs, menus, strings, version info) and the embedded images |
| `tools/` | the build steps: `toolchain.py` (tool locations and flags), `fix_res.py`, `gen_game_bss.py`, `mklink.py` (with `coff.py`), `postlink.py` |

## How the original was built (as far as can be told)

* Visual C++ 6.0, release build with `/Ox /Oa` (not `/O2`: no string pooling), statically linked
  against `LIBC.LIB`, with a `.def` file with an empty `EXPORTS` section (the export directory
  records the project's output name, `exe.exe`).
* 13 C files, 1 C++ file and one assembler object (the blitter), as recorded in the Rich header.
* Linked on a Windows of its time. LINK 6.00 orders the import address table with msvcrt's `qsort`,
  whose algorithm changed in later Windows versions, so `build.py` links with a copy of LINK.EXE
  that uses VC6's own `qsort` (built from your VC6 install by `tools/mklink.py`).
* In October 2002 the resources were replaced with the Win32 `UpdateResource` API, which rewrote
  `.rsrc` in its own layout, left a stray section header behind, marked `.rdata` writable and kept
  the checksum LINK had computed for the earlier resources. `tools/postlink.py` reproduces that.
* Uninitialized globals: the compiler orders them by an internal hash, so rather than guessing
  declaration orders they are laid out explicitly in `asm/game_bss.txt` and defined by the
  assembler object (plain `.bss` and communal variables).

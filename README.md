# KGT2nd_GAME

C source of `KGT2nd_GAME.exe`, the game runtime of *2D Fighter Maker 2nd* (2D格闘ツクール2nd.,
Enterbrain, 2002).

**This is the `nonmatching` branch: the whole program is plain C, built by a modern C compiler
alone** (clang from llvm-mingw, one command, no assembler, resource compiler or separate linker
step). The executable is *not* byte-identical to the original.

## Requirements

* Windows (64-bit Windows 10/11 is fine: the game is a 32-bit program and runs under WOW64; tested on
  Windows 11).
* **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)**, any recent release (it needs C23
  `#embed`, i.e. clang 19 or later). Tested with llvm-mingw 20260922 (clang 23.1.2), the
  `ucrt-x86_64` package, whose `i686-w64-mingw32-clang` builds 32-bit programs. It brings the
  mingw-w64 headers and import libraries for DirectDraw, DirectSound and DirectPlay.

  The target must be 32-bit x86 (`i686`): the game keeps pointers in 32-bit fields of its data
  structures and files. With the `ucrt` packages the executable uses the Universal C Runtime that
  is part of Windows 10 and 11 (the `msvcrt` packages would give one for older Windows; untested).
  It needs no other DLLs than Windows' own.

  GCC from mingw-w64 (i686, version 15 or later for `#embed`) should work too, but is untested.

Nothing else: no Python, no Visual C++, no assembler.

## Building

```
build.cmd
```

`build.cmd` uses the compiler in `%CC%`, or `i686-w64-mingw32-clang` from `PATH`, e.g.

```
set "CC=C:\llvm-mingw\bin\i686-w64-mingw32-clang.exe"
build.cmd
```

The result is `build\KGT2nd_GAME.exe`. Extra arguments go to the compiler (`build.cmd -O0 -g` for a
debug build; an `-O` option replaces the default `-O2`).

The same build as one command, to run by hand from any shell in the repository directory (list the
sources explicitly where the shell does not expand `src/*.c`; llvm-mingw's clang also expands the
wildcard itself):

```
i686-w64-mingw32-clang -std=gnu23 -O2 -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude src/*.c -o build/KGT2nd_GAME.exe -lddraw -ldsound -ldplayx -lwsock32 -lwinmm
```

* `-fwrapv` and `-fno-strict-aliasing` are required: signed overflow must wrap as in the original's
  machine code, and the uninitialized globals are one block of memory accessed through different
  types (see below), as are parts of the game's data structures.
* `-Wno-switch` only silences warnings about `switch` statements over enums that do not list every
  value.

## How this branch differs from the original build

* **Uninitialized globals** (`include/game_bss.h`, `src/game_bss.c`): the code relies on the
  original memory layout in places (a variable cleared or indexed together with its neighbours,
  names for fields inside other variables), so all of them are parts of one object, `gBss`, at their
  original offsets. Each file declares the variables it uses as `#define name BSS(type, name)`
  instead of `extern type name;`. On the main branch the same table (`asm/game_bss.txt`) is
  assembled into the executable's `.bss`.
* **Resources** (`src/resources.c`, generated from `rsrc/kgt2nd.rc` by `tools/gen_resources.py`):
  the bitmaps and the icon are included with `#embed`, the menu and dialog templates are written out
  as data (identical to the original's), and a few functions stand in for the resource APIs
  (`FindResource`/`LockResource`, `LoadIcon`, the class menu, `DialogBoxParam`). Rerun the script
  (Python 3) only after changing the `.rc`; the build does not need it.
* **The blitter** (`src/blit.c`): the hand-written assembler routines of the original, in C.
* **Portability fixes**, each commented in the source: undefined behaviour that VC6 happened to
  compile as intended but a modern optimizer may not (a parameter read through its neighbour's
  address, `sprintf` onto its own argument, uninitialized locals, out-of-range shift counts), enums
  kept `int` as with MSVC, a function that returned no value, and a `GlobalFree` of a resource pointer
  (harmless in the original, fatal in this build).
* Everything VC6-specific that only served the byte match (the C++ stub, the empty `.def` file, the
  post-link steps) is gone.

## Layout

| path | contents |
|---|---|
| `build.cmd` | the build |
| `src/*.c` | the game, one file per original translation unit (plus `game_bss.c`, `resources.c`, `blit.c`) |
| `include/` | types (`kgt_types.h`), globals (`globals.h`, `game_bss.h`), prototypes, `blit.h` |
| `rsrc/kgt2nd.rc`, `rsrc/assets/` | the resources (dialogs, menu, version info) and the embedded images |
| `tools/gen_resources.py` | regenerates `src/resources.c` from the `.rc` (not needed to build) |
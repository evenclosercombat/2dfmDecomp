# KGT2nd_GAME

C source of `KGT2nd_GAME.exe`, the game runtime of *2D Fighter Maker 2nd* (2D格闘ツクール2nd.,
Enterbrain, 2002).

**This is the `win64` branch: the plain-C port of the `nonmatching` branch to 64-bit Windows.** It
builds a native x86-64 executable with a modern C compiler (clang from llvm-mingw, one command) that
plays like the 32-bit program and reads the game data files unchanged; the same source still builds
the 32-bit (i686) program of the `nonmatching` branch, with the original's memory layout.  The
**`nonmatching`** branch is the 32-bit-only C build; the **`main`** branch has the exact rebuild of
the original executable with Visual C++ 6.0 (SHA-256
`287c6f39aea5265b126dff301af9e8fdf49e6edc78957eeb50aba38e6705326a`).

This repository contains only what the build needs. The decompilation work itself - matching tools,
the Ghidra notes, rename history and style guide - is in the development repository `kgt2nd_decomp`,
and what was learned about VC6 in `vc6-matching-notes`. Source comments that mention `docs/...` files
or VC6 details refer to that work and to the main branch.

The executable's embedded artwork (three 640x480 bitmaps, a text bitmap and the icon) is in
`rsrc/assets/`; these are Enterbrain's copyrighted images, so keep that in mind before publishing the
repository. No other game data is included.

## Requirements

* 64-bit Windows 10/11 for the x86-64 build (tested on Windows 11); the i686 build runs on any 32-bit
  or 64-bit Windows 10/11 (under WOW64 on 64-bit Windows).
* **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)**, any recent release (it needs C23
  `#embed`, i.e. clang 19 or later). Tested with llvm-mingw 20260922 (clang 23.1.2), the
  `ucrt-x86_64` package: its `x86_64-w64-mingw32-clang` builds the 64-bit program, its
  `i686-w64-mingw32-clang` the 32-bit one. It brings the mingw-w64 headers and import libraries for
  DirectDraw and DirectSound (and, for i686, DirectPlay).

  With the `ucrt` packages the executable uses the Universal C Runtime that is part of Windows 10 and
  11 (both builds, so their `rand()` sequences are the same). It needs no other DLLs than Windows' own.

  GCC from mingw-w64 (version 15 or later for `#embed`) should work too, but is untested.

Nothing else: no Python, no Visual C++, no assembler.

## Building

```
build.cmd            64-bit build (x86-64): build\KGT2nd_GAME.exe
build.cmd x86        32-bit build (i686):   build\KGT2nd_GAME_x86.exe
```

`build.cmd` uses the compiler in `%CC%`, or `x86_64-w64-mingw32-clang` (with `x86`:
`i686-w64-mingw32-clang`) from `PATH`, e.g.

```
set "CC=C:\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe"
build.cmd
```

The output name follows the target of the compiler (`-dumpmachine`). Extra arguments go to the
compiler (`build.cmd -O0 -g` for a debug build, `build.cmd x86 -O0 -g` for a 32-bit one; an `-O`
option replaces the default `-O2`).

The same builds as one command each, to run by hand from any shell in the repository directory (list
the sources explicitly where the shell does not expand `src/*.c`; llvm-mingw's clang also expands the
wildcard itself):

```
x86_64-w64-mingw32-clang -std=gnu23 -O2 -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude src/*.c -o build/KGT2nd_GAME.exe -lddraw -ldsound -lwsock32 -lwinmm
i686-w64-mingw32-clang -std=gnu23 -O2 -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude src/*.c -o build/KGT2nd_GAME_x86.exe -lddraw -ldsound -ldplayx -lwsock32 -lwinmm
```

* `-fwrapv` and `-fno-strict-aliasing` are required: signed overflow must wrap as in the original's
  machine code, and the uninitialized globals are one block of memory accessed through different
  types (see below), as are parts of the game's data structures.
* `-Wno-switch` only silences warnings about `switch` statements over enums that do not list every
  value.
* The sources also compile without pointer-truncation warnings with `-Wall -Wextra
  -Wpointer-to-int-cast -Wint-to-pointer-cast -Wshorten-64-to-32` (the remaining `-Wextra` warnings -
  unused variables and parameters, signed/unsigned comparisons - are the original code's).

## Running

Copy the executable into the game's folder, next to its data files. The game loads the system file
named after the executable (`NAME.exe` loads `NAME.kgt`), so give it the name of the game's
executable. Settings are read from `game.ini` in the same folder.

* **DirectPlay**: the netplay dialog uses DirectPlay, which the shipped menu does not offer. The
  32-bit build imports `dplayx.dll` like the original and lists the DirectPlay service providers at
  start-up; on Windows 10/11 DirectPlay is an optional feature ("Legacy Components"), and if Windows
  asks to install it, the game works either way. The 64-bit build loads `dplayx.dll` at start-up
  instead of importing it (llvm-mingw has no x86-64 import library for it, and 64-bit Windows has a
  64-bit `dplayx.dll` only with the DirectPlay feature installed); without it the list of service
  providers is empty and the netplay dialog reports that DirectPlay could not be created.
* **No icon or version information in Explorer**: the executable has no resource section (see below),
  so Explorer shows a generic icon and no version details. The window, the taskbar and the about box
  show the game's icon. If you want them in the file as well, compile `rsrc/kgt2nd.rc` with
  llvm-mingw's resource compiler and link the result in (optional, not part of the build). The `.rc`
  is UTF-16, which llvm-rc does not read, so convert it to UTF-8 first:

  ```
  powershell -Command "[IO.File]::WriteAllText('build\kgt2nd_utf8.rc', [IO.File]::ReadAllText('rsrc\kgt2nd.rc', [Text.Encoding]::Unicode))"
  x86_64-w64-mingw32-windres -c 65001 -I rsrc/assets build/kgt2nd_utf8.rc -O coff -o build/kgt2nd.res.o
  ```

  and add `build/kgt2nd.res.o` to the compile command above (e.g. `build.cmd build\kgt2nd.res.o`;
  `i686-w64-mingw32-windres` for the 32-bit build). The game itself still uses the compiled-in copies.

## How this branch differs from `nonmatching`

The original keeps pointers in 32-bit places: in structures that are also file records, in `int`
fields, and in one block of uninitialized globals laid out at fixed offsets. The port keeps the
behaviour and the file formats and gives the pointers room:

* **File records and loaded structures** (`include/kgt_types.h`): the two records that are read
  whole from the files and have a pointer slot, the image and sound headers, have file layouts of
  their own (`kgtImageHeaderFile`, 0x14 bytes, `kgtSoundFile`, 0x2a bytes); `bReadKgtCore` (main.c)
  reads those and converts them to the loaded `kgtImageHeader` / `kgtSound` with real pointers (the
  sound's size slot and wave pointer stay a union, as in the original). Everything else is read from
  the files member by member into pointer-free parts of the structures (the character, system, demo
  and stage tails after the `kgt_core`), so those reads are the same in both builds; the loaded
  structures just have wider pointer members. The size checks at the end of `kgt_types.h` state every
  structure's size as its 32-bit size plus 4 bytes per pointer (`KGT_PTR_GROWTH`), so they hold for
  both builds, and check that the file parts start at the same offset after the `kgt_core`.
* **Pointers in integer fields**: the multi-use work slots of `kgtEngineObject` (`iPlayerIdx`,
  `iObjectType`, `iWork0166`...`iWork0176`, and `pWork015E` / `pWork0162` as unions with pointer-sized
  integers) are `intptr_t`, so that the game-state handlers can keep an object pointer or a number in
  them as before; where they hold a number they are read as `int` (explicit casts). The character's
  last attacker (`iLastAttacker`) is now the pointer it always held (`pLastAttacker`). None of these
  structures is written to a file or sent over the network (the netplay messages are pointer-free
  and keep their byte layout).
* **After-image trails** (`unk_0x650_struct`): header and 100 frames of 16 bytes in the original,
  with image-step pointers in them; now typed members (`kgtFrames[]`) used by battle.c and engine.c
  alike instead of int indexing.
* **Uninitialized globals** (`include/game_bss.h`, `src/game_bss.c`): still one object, `gBss`, with
  the variables in their original order, but the members that hold pointers take their size from the
  real types and are pointer-aligned, and every `BSS_OFS_` is an `offsetof`. In the i686 build the
  layout is exactly the original's (`game_bss.c` checks every address); in the x86-64 build the
  variables grow in place. Names inside other variables (flash and shake variables behind the stage,
  system variables behind the system file, test-play settings, story progress) are offsets from their
  variable, after a `kgt_core` moved by its growth (`KGT_CORE_GROWTH`). The one place that clears a
  variable together with its neighbours (`gBmiFrame` and the frame buffer handles) clears up to the
  last of them.
* **The original's out-of-bounds views**: script commands of system, demo and stage objects (GL, EB, C
  and V with owner scope) use character fields of their file's structure, which the original reaches
  at an address beyond the `kgt_core` - inside the system file's lists (system scripts), the system
  file's palettes (demo scripts) or the after-image trails (stage scripts). `pBssAddr32`
  (`game_bss.c`) maps such an original address to the same byte in this build, and battle.c uses it
  for those accesses, so they touch the same data in both builds (in the i686 build it is the same
  address as before).
* **Win32 details**: dialog procedures return `INT_PTR`, MCI parameters are `DWORD_PTR`, the
  trackbar positions are `LRESULT`s read as `int`, the DirectSound wave object is allocated with its
  real header size, and the 64-bit build loads DirectPlay at run time (see above).
* **The blitter** (`src/blit.c`) already added its 32-bit offsets as signed values and needed no
  change.
* `build.cmd` builds either target (x86-64 by default).

Everything else is as on the `nonmatching` branch:

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

## Limitations of the 64-bit build

* Where the original reads or writes memory through a wrong index rather than a wrong structure
  (e.g. a script of a gauge object, whose `iPlayerIdx` is 10-21, using the character or input-buffer
  arrays with it), the bytes reached are not the same as in the 32-bit program; the game's own data
  does not do that, and in the original such accesses read unrelated pointers or crash.
* Values that were pointers in the original are pointers of another size here, so where the original
  exposes one as a number (a garbage "variable" of a stage script aliasing an after-image trail's
  image pointer), the number differs; so it does between two runs of the original.
* DirectPlay netplay could not be tested (no 64-bit DirectPlay installed on the test machine; the
  menu of the shipped game does not offer it either).

## Debug trace (off by default)

Compiled with `-DKGT_TRACE` and a `trace.c` that defines `vTraceInput` and `vTraceTick` (kept outside
this repository), the main loop runs a fixed 8 ticks per frame, the keyboard state is replaced by a
script and a checksum of the pointer-free game state is logged after every tick; the i686 and x86-64
builds were compared with it frame by frame. The two hooks (main.c, input.c) are inactive in normal
builds.

## Layout

| path | contents |
|---|---|
| `build.cmd` | the build |
| `src/*.c` | the game, one file per original translation unit (plus `game_bss.c`, `resources.c`, `blit.c`) |
| `include/` | types (`kgt_types.h`), globals (`globals.h`, `game_bss.h`), prototypes, `blit.h` |
| `rsrc/kgt2nd.rc`, `rsrc/assets/` | the resources (dialogs, menu, version info) and the embedded images |
| `tools/gen_resources.py` | regenerates `src/resources.c` from the `.rc` (not needed to build) |

## How the original was built (as far as can be told)

* Visual C++ 6.0, release build with `/Ox /Oa`, statically linked against `LIBC.LIB`, 13 C files, 1
  C++ file and one assembler object (the blitter); resources replaced after linking with the Win32
  `UpdateResource` API. The main branch reproduces all of this byte for byte.

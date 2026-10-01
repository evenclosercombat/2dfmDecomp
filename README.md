# KGT2nd_GAME

This is a plain-C port of the original KGT2nd_Game.exe, built off of the 'non-matching' branch. It is untested with helper programs that use memory injection to add additional features to 2DFM, such as rollback launchers.

## Requirements

* Windows 10/11: 64-bit Windows for the x86-64 build (tested on Windows 11); the i686 build also runs
  on 32-bit Windows, and under WOW64 on 64-bit Windows.
* **llvm-mingw** (<https://github.com/mstorsjo/llvm-mingw/releases>), any release with clang 19 or
  later (the resources are included with C23 `#embed`). On a 64-bit Windows host take the
  `llvm-mingw-<version>-ucrt-x86_64.zip` package and unpack it anywhere, e.g. `C:\llvm-mingw`.
  Tested with llvm-mingw 20260922 (`llvm-mingw-20260922-ucrt-x86_64`). Its `bin` directory has
  `x86_64-w64-mingw32-clang` for the 64-bit program and `i686-w64-mingw32-clang` for the 32-bit one,
  with the mingw-w64 headers and import libraries for DirectDraw, DirectSound and (i686 only)
  DirectPlay.

  With the `ucrt` package both executables use the Universal C Runtime that is part of Windows 10
  and 11 (so their `rand()` sequences are the same) and need no DLLs other than Windows' own.
  GCC from mingw-w64 (15 or later, for `#embed`) should work too, but is untested.

Nothing else: no Python, no Visual C++, no assembler.

## Building

```
build.cmd              64-bit build (x86-64) -> build\KGT2nd_GAME.exe
build.cmd x86          32-bit build (i686)   -> build\KGT2nd_GAME_x86.exe
```

`build.cmd` uses the compiler in the environment variable `CC` if it is set, otherwise
`x86_64-w64-mingw32-clang` (with `x86`: `i686-w64-mingw32-clang`) from `PATH`. The output name
follows the target of that compiler (`-dumpmachine`), so a `CC` left pointing at the i686 compiler
builds `build\KGT2nd_GAME_x86.exe` even without `x86`, and `x86` has no effect when `CC` is set.

From `cmd.exe`, in the repository directory:

```
set "PATH=C:\llvm-mingw\bin;%PATH%"
build.cmd
build.cmd x86
```

or `set "CC=C:\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe"` and then `build.cmd`.

From PowerShell (which does not run scripts from the current directory without `.\`):

```
$env:Path = "C:\llvm-mingw\bin;" + $env:Path
.\build.cmd
.\build.cmd x86
```

or `$env:CC = "C:\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe"` and then `.\build.cmd`
(`Remove-Item Env:CC` to go back to the `PATH` lookup).

Further arguments go to the compiler after everything else (the `x64` / `x86` argument, if any, must
come first); one starting with `-O` replaces the default `-O2`: `build.cmd -O0 -g` is a debug build,
`build.cmd x86 -O0 -g` a 32-bit one.

The same builds as one command each, from any shell in the repository directory (llvm-mingw's clang
expands `src/*.c` itself; list the files where another compiler does not):

```
x86_64-w64-mingw32-clang -std=gnu23 -O2 -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude src/*.c -o build/KGT2nd_GAME.exe -lddraw -ldsound -lwsock32 -lwinmm
i686-w64-mingw32-clang -std=gnu23 -O2 -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude src/*.c -o build/KGT2nd_GAME_x86.exe -lddraw -ldsound -ldplayx -lwsock32 -lwinmm
```

(`build\` must exist; `build.cmd` creates it.)

* `-fwrapv` and `-fno-strict-aliasing` are required: signed overflow must wrap as in the original's
  machine code, and the uninitialized globals are one block of memory accessed through different
  types (see below), as are parts of the game's data structures.
* `-Wno-switch` only silences warnings about `switch` statements over enums that do not list every
  value. The build prints no warnings.
* With `-Wall -Wextra -Wpointer-to-int-cast -Wint-to-pointer-cast -Wshorten-64-to-32` added, the
  x86-64 build shows no pointer-truncation warnings; what remains (unused variables, parameters and a
  label, three signed/unsigned comparisons, and the enum `switch` warnings that `-Wall` turns on
  again) is the original code's.

### Optional: icon and version information in the file

The executable has no resource section: the menu, the dialogs, the bitmaps and the icon are compiled
in as C data (`src/resources.c`), so the window, the taskbar and the about box show the game's icon,
but Explorer shows a generic icon and no version details. To add them to the file, compile
`rsrc/kgt2nd.rc` with llvm-mingw's `windres` and pass the object to `build.cmd`. The `.rc` is UTF-16,
which llvm's resource compiler does not read, so convert it to UTF-8 first (the commands work from
`cmd.exe` and from PowerShell):

```
powershell -Command "[IO.File]::WriteAllText('build\kgt2nd_utf8.rc', [IO.File]::ReadAllText('rsrc\kgt2nd.rc', [Text.Encoding]::Unicode))"
x86_64-w64-mingw32-windres -c 65001 -I rsrc/assets build/kgt2nd_utf8.rc -O coff -o build/kgt2nd.res.o
build.cmd build\kgt2nd.res.o
```

For the 32-bit build use `i686-w64-mingw32-windres ... -o build/kgt2nd_x86.res.o` and
`build.cmd x86 build\kgt2nd_x86.res.o`. The game itself still uses the compiled-in copies.

## Changes from `nonmatching`

Non-matching keeps pointers in 32-bit places: in structures that are also file records, in `int`
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
* **After-image trails** (`unk_0x650_struct`): a header and 100 frames of 16 bytes in the original,
  with image-step pointers in them; now typed members (`kgtFrames[]`, `kgtTrailFrame`) used by
  battle.c and engine.c alike instead of int indexing.
* **Uninitialized globals** (`include/game_bss.h`, `src/game_bss.c`): still one object, `gBss`, with
  the variables in their original order, but the members that hold pointers take their size from the
  real types and are pointer-aligned, and every `BSS_OFS_` is computed with `offsetof`. In the i686
  build the layout is exactly the original's (`game_bss.c` checks every address); in the x86-64 build
  the variables grow in place. Names inside other variables (flash and shake variables behind the
  stage, system variables behind the system file, test-play settings, story progress) are offsets from
  their variable, after a `kgt_core` moved by its growth (`KGT_CORE_GROWTH`). The one place that clears
  a variable together with its neighbours (`gBmiFrame` and the frame buffer handles) clears up to the
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

## Limitations of the 64-bit build

* Where the original reads or writes memory through a wrong index rather than a wrong structure
  (e.g. a script of a gauge object, whose `iPlayerIdx` is 10-21, using the character or input-buffer
  arrays with it), the bytes reached are not the same as in the 32-bit program; the game's own data
  does not do that, and in the original such accesses read unrelated pointers or crash.
* Values that were pointers in the original are pointers of another size here, so where the original
  exposes one as a number (a garbage "variable" of a stage script aliasing an after-image trail's
  image pointer), the number differs; so it does between two runs of the original.
* DirectPlay netplay is untested (no 64-bit DirectPlay installed on the test machine; the menu of the
  shipped game does not offer it either).

## Debug trace (off by default)

`src/main.c` and `src/input.c` have two hooks for comparing builds frame by frame, compiled only with
`-DKGT_TRACE`: `vTraceInput` gets the keyboard state after `GetKeyboardState` (to replace it by a
script), `vTraceTick` is called after every tick (to log a checksum of the game state), and the main
loop runs a fixed 8 ticks per frame. The two functions are not in this repository; with a `trace.c`
that defines them, `build.cmd -DKGT_TRACE path\to\trace.c` builds the traced program. The i686 and
x86-64 builds were compared this way.

## Layout

| path | contents |
|---|---|
| `build.cmd` | the build (both targets) |
| `src/*.c` | the game, one file per original translation unit, plus `blit.c` (the original's assembler blitter), `game_bss.c` (`gBss`, the uninitialized variables) and `resources.c` (the resources) |
| `include/` | `kgt.h` (included by every file), types (`kgt_types.h`), globals (`globals.h`, `game_bss.h`), prototypes (`protos.h`), `blit.h` |
| `rsrc/kgt2nd.rc`, `rsrc/assets/` | the resources (dialogs, menu, version info) and the embedded images |
| `tools/gen_resources.py` | regenerates `src/resources.c` from the `.rc` (not needed to build) |
| `build/` | the output (not in the repository) |
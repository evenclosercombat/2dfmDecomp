@echo off
rem build.cmd - builds build\KGT2nd_GAME.exe from src\*.c with one clang (llvm-mingw) command.
rem
rem   build.cmd            optimized build (-O2)
rem   build.cmd -O0 -g     extra arguments are passed on to the compiler; one starting with -O
rem                        replaces the default -O2
rem
rem The compiler is %CC% if set, else i686-w64-mingw32-clang from PATH.  It must target 32-bit
rem Windows (i686): the game keeps pointers in 32-bit fields of its data structures.
rem -fwrapv and -fno-strict-aliasing are required (see README.md); -Wno-switch only silences the
rem many switches over enums that do not list every value.
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not defined CC set "CC=i686-w64-mingw32-clang"
if exist "%CC%" goto have_cc
where "%CC%" >nul 2>nul
if not errorlevel 1 goto have_cc
echo build.cmd: compiler "%CC%" not found.
echo Put llvm-mingw's bin directory on PATH or set CC to the compiler, for example
echo     set "CC=C:\llvm-mingw\bin\i686-w64-mingw32-clang.exe"
exit /b 1
:have_cc

set "OPT=-O2"
for %%a in (%*) do (
    set "ARG=%%~a"
    if "!ARG:~0,2!"=="-O" set "OPT="
)

set "SRCS="
for %%f in (src\*.c) do set "SRCS=!SRCS! src\%%~nxf"

if not exist build mkdir build
echo "%CC%" -std=gnu23 %OPT% -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude%SRCS% -o build\KGT2nd_GAME.exe -lddraw -ldsound -ldplayx -lwsock32 -lwinmm %*
"%CC%" -std=gnu23 %OPT% -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude%SRCS% -o build\KGT2nd_GAME.exe -lddraw -ldsound -ldplayx -lwsock32 -lwinmm %*
if errorlevel 1 goto failed
echo built build\KGT2nd_GAME.exe
exit /b 0
:failed
echo build.cmd: build failed
exit /b 1

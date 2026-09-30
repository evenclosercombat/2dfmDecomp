@echo off
rem build.cmd - builds the game from src\*.c with one clang (llvm-mingw) command.
rem
rem   build.cmd              64-bit build (x86-64) -> build\KGT2nd_GAME.exe
rem   build.cmd x86          32-bit build (i686)   -> build\KGT2nd_GAME_x86.exe
rem   build.cmd -O0 -g       extra arguments are passed on to the compiler; one starting with -O
rem                          replaces the default -O2 (an x64 / x86 argument must come first)
rem
rem The compiler is %CC% if set, else x86_64-w64-mingw32-clang (or, with x86,
rem i686-w64-mingw32-clang) from PATH; the output name follows the compiler's target.
rem -fwrapv and -fno-strict-aliasing are required (see README.md); -Wno-switch only silences the
rem many switches over enums that do not list every value.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "TRIPLE=x86_64"
if /i "%~1"=="x64" (set "TRIPLE=x86_64" & shift /1)
if /i "%~1"=="x86" (set "TRIPLE=i686" & shift /1)
if not defined CC set "CC=%TRIPLE%-w64-mingw32-clang"
if exist "%CC%" goto have_cc
where "%CC%" >nul 2>nul
if not errorlevel 1 goto have_cc
echo build.cmd: compiler "%CC%" not found.
echo Put llvm-mingw's bin directory on PATH or set CC to the compiler, for example
echo     set "CC=C:\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe"
exit /b 1
:have_cc

rem the target of the compiler decides the output name; the 32-bit build imports dplayx.dll like
rem the original, the 64-bit build loads it at run time (online.c)
set "OUT=build\KGT2nd_GAME.exe"
set "LIBS=-lddraw -ldsound -lwsock32 -lwinmm"
for /f "delims=" %%m in ('"%CC%" -dumpmachine') do set "MACHINE=%%m"
if /i "!MACHINE:~0,4!"=="i686" set "OUT=build\KGT2nd_GAME_x86.exe" & set "LIBS=-lddraw -ldsound -ldplayx -lwsock32 -lwinmm"
if /i "!MACHINE:~0,4!"=="i386" set "OUT=build\KGT2nd_GAME_x86.exe" & set "LIBS=-lddraw -ldsound -ldplayx -lwsock32 -lwinmm"

rem the remaining arguments (after an x64 / x86 one)
set "ARGS="
set "OPT=-O2"
:args
if "%~1"=="" goto args_done
set "ARG=%~1"
if "!ARG:~0,2!"=="-O" set "OPT="
set "ARGS=!ARGS! %1"
shift /1
goto args
:args_done

set "SRCS="
for %%f in (src\*.c) do set "SRCS=!SRCS! src\%%~nxf"

if not exist build mkdir build
echo "%CC%" -std=gnu23 %OPT% -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude%SRCS% -o %OUT% %LIBS%%ARGS%
"%CC%" -std=gnu23 %OPT% -fwrapv -fno-strict-aliasing -Wno-switch -mwindows -Iinclude%SRCS% -o %OUT% %LIBS%%ARGS%
if errorlevel 1 goto failed
echo built %OUT%
exit /b 0
:failed
echo build.cmd: build failed
exit /b 1

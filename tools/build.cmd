@echo off
rem build.cmd — buduje WinMailMAPI.dll (x86 + x64) i harness testowy (zig cc)
rem Uzycie: tools\build.cmd   (katalog bin/ dostaje pliki wynikowe)
setlocal enabledelayedexpansion
set ROOT=%~dp0..
rem ZIG mozna wskazac srodowiskiem (uzywane przez CI); w przeciwnym razie
rem automatycznie znajdz tools\zig-*\zig.exe
set "ZIG=%ZIG%"
if not defined ZIG for /d %%D in ("%ROOT%\tools\zig-*") do set "ZIG=%%D\zig.exe"
if not exist "!ZIG!" (
    echo [blad] nie znaleziono zig.exe ^(tools\zig-* lub zmienna ZIG^)
    exit /b 1
)
echo zig: !ZIG!
if not exist "%ROOT%\bin" mkdir "%ROOT%\bin"
set CFLAGS=-O2 -I"%ROOT%\src"
set DLLOPT=-shared

echo [1/4] WinMailMAPI.dll  (x86)
"%ZIG%" cc %CFLAGS% %DLLOPT% -target x86-windows-gnu "%ROOT%\src\wm_mapi.c" "%ROOT%\src\wm_mapi.def" -o "%ROOT%\bin\WinMailMAPI.dll" -ladvapi32 -luser32 || exit /b 1

echo [2/4] WinMailMAPI64.dll (x64)
"%ZIG%" cc %CFLAGS% %DLLOPT% -target x86_64-windows-gnu "%ROOT%\src\wm_mapi.c" "%ROOT%\src\wm_mapi.def" -o "%ROOT%\bin\WinMailMAPI64.dll" -ladvapi32 -luser32 || exit /b 1

echo [3/4] wmtest86.exe
"%ZIG%" cc %CFLAGS% -target x86-windows-gnu "%ROOT%\tests\wmtest.c" -o "%ROOT%\bin\wmtest86.exe" -ladvapi32 -luser32 -lshell32 || exit /b 1

echo [4/4] wmtest64.exe
"%ZIG%" cc %CFLAGS% -target x86_64-windows-gnu "%ROOT%\tests\wmtest.c" -o "%ROOT%\bin\wmtest64.exe" -ladvapi32 -luser32 -lshell32 || exit /b 1

echo OK — pliki w bin\
dir /b "%ROOT%\bin"
endlocal

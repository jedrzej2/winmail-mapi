@echo off
rem ============================================================
rem  WinMailMAPI — samotest na maszynie docelowej
rem  Sprawdza: eksporty DLL, wywolania MAPI, budowe mailto,
rem  symulowana wysylke (atrapa), stan magazynu i rejestru.
rem  Nie wysyla zadnej prawdziwej wiadomosci.
rem ============================================================
setlocal EnableExtensions
set "DEST="
if exist "%ProgramFiles(x86)%\WinMailMAPI\wmtest86.exe" set "DEST=%ProgramFiles(x86)%\WinMailMAPI"
if not defined DEST if exist "%LOCALAPPDATA%\WinMailMAPI\wmtest86.exe" set "DEST=%LOCALAPPDATA%\WinMailMAPI"
if not defined DEST set "DEST=%~dp0..\bin"

echo ============================================
echo  WinMailMAPI — samotest
echo ============================================
echo.
echo [1] Diagnostyka biblioteki:
"%DEST%\wmtest86.exe" --diag "%DEST%\WinMailMAPI.dll"
echo.
echo [2] Eksporty:
"%DEST%\wmtest86.exe" --exports "%DEST%\WinMailMAPI.dll"
echo.
echo [3] Wywolania MAPI:
"%DEST%\wmtest86.exe" --calltest "%DEST%\WinMailMAPI.dll"
echo.
echo [4] Budowa adresu mailto:
"%DEST%\wmtest86.exe" --preview "%DEST%\WinMailMAPI.dll"
echo.
echo [5] Symulowana wysylka (MAPISendMail -> atrapa):
"%DEST%\wmtest86.exe" --sendtest "%DEST%\WinMailMAPI.dll"
echo.
echo [6] Rejestr MAPI w systemie:
reg query "HKCU\Software\Clients\Mail" /ve 2>nul
if not errorlevel 1 echo     ^(_default HKCU^)  ^<--- musi byc: Windows Mail
reg query "HKLM\SOFTWARE\Clients\Mail" /ve 2>nul
reg query "HKLM\SOFTWARE\Clients\Mail\Windows Mail" /v DLLPath 2>nul
reg query "HKLM\SOFTWARE\WOW6432Node\Clients\Mail\Windows Mail" /v DLLPath 2>nul
echo.
echo [i] Test wysylki na zywo ^(otworzy okno Windows Mail^):
echo     "%DEST%\wmtest86.exe" --sendlive "%DEST%\WinMailMAPI.dll"
echo.
endlocal

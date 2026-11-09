@echo off
rem ============================================================
rem  WinMailMAPI — odinstalowanie patcha MAPI
rem
rem  Usuwa WYLACZNIE slady dodane przez instalator:
rem    - wartosci DLLPath wskazujace na WinMailMAPI
rem    - klucz HKCU\Software\WinMailMAPI
rem    - katalog instalacji (Program Files (x86)\WinMailMAPI
rem      lub %LOCALAPPDATA%\WinMailMAPI)
rem    - przywraca poprzedni domyslny klient poczty z backupu
rem
rem  Magazyn wiadomosci NIE jest ruszany. Backup zostaje na dysku.
rem  Uzycie: uninstall.cmd [/RESTORE-STORE]
rem      /RESTORE-STORE — dodatkowo przywraca magazyn z ostatniego
rem                       backupu (gdyby cos uszkodzono recznie)
rem ============================================================
setlocal EnableExtensions
set ISADMIN=0
net session >nul 2>&1
if not errorlevel 1 set ISADMIN=1
if /i "%~1"=="/RESTORE-STORE" set RESTORESTORE=1

echo ============================================
echo  WinMailMAPI — odinstalowanie
echo ============================================
echo.

rem ---- ostatni backup ---------------------------------------
set "BKROOT=%ProgramData%\WinMailMAPI\backup"
if not exist "%BKROOT%" set "BKROOT=%LOCALAPPDATA%\WinMailMAPI\backup"
set "BK="
for /f "delims=" %%d in ('dir /b /ad /o-n "%BKROOT%" 2^>nul') do (
    if not defined BK set "BK=%BKROOT%\%%d"
)
if defined BK (echo [i] Ostatni backup: %BK%) else echo [i] Brak backupow — usunie tylko rejestre patcha.

rem ---- 1. przywroc stan rejestru z backupu -------------------
rem import najpierw: backup mógł złapać ślady starszej instalacji patcha,
rem więc nasze wartości usuwamy DOPIERO po importach
set IMPORTED=0
if exist "%BK%\reg_clients_user.reg" (
    reg import "%BK%\reg_clients_user.reg" >nul 2>&1
    set IMPORTED=1
    echo [i] Przyrowcono domyslny klient HKCU z backupu
)
if "%ISADMIN%"=="1" if exist "%BK%\reg_clients_machine.reg" (
    reg import "%BK%\reg_clients_machine.reg" >nul 2>&1
    echo [i] Przyrowcono klienty HKLM z backupu
)
if "%ISADMIN%"=="1" if exist "%BK%\reg_clients_machine32.reg" (
    reg import "%BK%\reg_clients_machine32.reg" >nul 2>&1
)

rem ---- 2. usun wartosci dodane przez patch -------------------
rem (import nie usuwa nadmiarowych wartosci — zrzucamy je recznie)
call :delourvalue "HKLM\SOFTWARE\Clients\Mail\Windows Mail" "DLLPath"
call :delourvalue "HKLM\SOFTWARE\WOW6432Node\Clients\Mail\Windows Mail" "DLLPath"
call :delourvalue "HKCU\Software\Clients\Mail\Windows Mail" "DLLPath"
reg delete "HKCU\Software\Clients\Mail\Windows Mail\shell\open\command" /f >nul 2>&1
reg delete "HKCU\Software\Clients\Mail\Windows Mail" /f >nul 2>&1

rem ---- 3. domyslny klient HKCU -------------------------------
rem jesli wskazuje na "Windows Mail", ale klucz klienta juz nie istnieje,
rem to zostawil go nasz instalator — czyscimy (bez blokow — zmienna %DEFCLIENT%)
reg query "HKCU\Software\Clients\Mail\Windows Mail" >nul 2>&1
if not errorlevel 1 goto :defclientok
set DEFCLIENT=
for /f "tokens=2*" %%a in ('reg query "HKCU\Software\Clients\Mail" /ve 2^>nul') do set "DEFCLIENT=%%b"
if /i not "%DEFCLIENT%"=="Windows Mail" goto :defclientok
reg delete "HKCU\Software\Clients\Mail" /ve /f >nul 2>&1
echo [i] Wyczyszczono domyslny klient HKCU ^(ustawiony przez patch^)
:defclientok

rem ---- 3. usun konfiguracje patcha ---------------------------
reg delete "HKCU\Software\WinMailMAPI" /f >nul 2>&1
echo [i] Usunieto HKCU\Software\WinMailMAPI

rem ---- 4. usun katalog instalacji ----------------------------
if exist "%ProgramFiles(x86)%\WinMailMAPI" rmdir /s /q "%ProgramFiles(x86)%\WinMailMAPI"
if exist "%ProgramFiles%\WinMailMAPI" rmdir /s /q "%ProgramFiles%\WinMailMAPI"
if exist "%LOCALAPPDATA%\WinMailMAPI" rmdir /s /q "%LOCALAPPDATA%\WinMailMAPI"
echo [i] Usunieto katalog instalacji

rem ---- 5. opcjonalne przywrocenie magazynu -------------------
if defined RESTORESTORE (
    if defined BK if exist "%BK%\store" if exist "%BK%\manifest.txt" (
        for /f "tokens=1* delims==" %%a in ('type "%BK%\manifest.txt" ^| findstr /b "magazyn="') do set "STORE=%%b"
        if defined STORE if exist "%STORE%" (
            robocopy "%BK%\store" "%STORE%" /E /COPY:DAT /R:1 /W:1 >nul
            echo [i] Magazyn przywrocony z backupu do: %STORE%
        )
    )
)

echo.
echo ============================================
echo  ODINSTALOWANO
echo  Backup pozostaje na dysku: %BKROOT%
echo ============================================
exit /b 0

:delourvalue
rem usuwa wartosc tylko gdy dane wskazuja na WinMailMAPI
set "KEY=%~1"
set "VAL=%~2"
set "DATA="
for /f "tokens=2*" %%a in ('reg query "%KEY%" /v "%VAL%" 2^>nul') do set "DATA=%%b"
if not defined DATA goto :eof
echo %DATA% | findstr /i "WinMailMAPI" >nul
if not errorlevel 1 (
    reg delete "%KEY%" /v "%VAL%" /f >nul 2>&1
    echo [i] Usunieto %KEY%\%VAL%
)
goto :eof

@echo off
rem ============================================================
rem  WinMailMAPI — instalacja patcha MAPI dla Windows Mail
rem  Docelowy system: Windows 7/8/8.1/10 (x64 lub x86)
rem
rem  Co robi:
rem   1. WYKRYWA Windows Mail (WinMail.exe) i magazyn wiadomosci
rem   2. ROBI PELNY BACKUP magazynu + konfiguracji (rejestr)
rem   3. Kopiuje WinMailMAPI.dll (x86) i WinMailMAPI64.dll (x64)
rem   4. Rejestruje dostawce Simple MAPI:
rem        - z uprawnieniami admina: oba widoki HKLM (x86 + x64)
rem        - bez admina: HKCU (dziala dla aplikacji 32-bit)
rem   5. WERYFIKUJE integralnosc skrzynki po instalacji;
rem      w razie rozbieznosci przywraca backup automatycznie
rem
rem  Instalator NIGDY nie modyfikuje plikow wiadomosci — kopiuje
rem  tylko DLL i wartosci rejestru DLLPath.
rem ============================================================
setlocal EnableExtensions
set "SRC=%~dp0.."
set "SRCBIN=%SRC%\bin"
set TS=
set ISADMIN=0
set MODE=
set DEST=
set BK=
set PFX86=%ProgramFiles(x86)%
set PF=%ProgramFiles%
set WMDIR=
set STORE=

echo ============================================
echo  WinMailMAPI — patch MAPI dla Windows Mail
echo ============================================
echo.

rem ---- 0. czy podniesione uprawnienia? -----------------------
net session >nul 2>&1
if not errorlevel 1 set ISADMIN=1
if "%ISADMIN%"=="1" (echo [i] Tryb: administrator — rejestracja HKLM ^(x86+x64^)) else (echo [i] Tryb: uzytkownik — rejestracja HKCU ^(aplikacje 32-bit^))

rem ---- 1. znacznik czasu ------------------------------------
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"`) do set "TS=%%i"
if not defined TS (
    echo [blad] nie udalo sie ustalic czasu ^(powershell^)
    exit /b 1
)

rem ---- 2. znajdz WinMail.exe --------------------------------
if exist "%PFX86%\Windows Mail\WinMail.exe" set "WMDIR=%PFX86%\Windows Mail"
if not defined WMDIR if exist "%PF%\Windows Mail\WinMail.exe" set "WMDIR=%PF%\Windows Mail"
if not defined WMDIR if exist "%ProgramW6432%\Windows Mail\WinMail.exe" set "WMDIR=%ProgramW6432%\Windows Mail"
if not defined WMDIR (
    echo [blad] nie znaleziono WinMail.exe.
    echo        Zainstaluj Windows Mail ^(katalog "Windows Mail" z Visty^)
    echo        i uruchom instalator ponownie.
    exit /b 1
)
echo [i] Windows Mail: %WMDIR%

rem ---- 3. katalog magazynu ----------------------------------
set "STORE=%LOCALAPPDATA%\Microsoft\Windows Mail"
reg query "HKCU\Software\Microsoft\Windows Mail" /v "Store Root" >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=2*" %%a in ('reg query "HKCU\Software\Microsoft\Windows Mail" /v "Store Root" 2^>nul') do set "STORE=%%b"
)
echo [i] Magazyn wiadomosci: %STORE%

rem ---- 4. liczniki PRZED zmianami ---------------------------
set CNT_BEFORE=0
if exist "%STORE%" for /f %%c in ('dir /s /b "%STORE%\*.eml" 2^>nul ^| find /c /v ""') do set "CNT_BEFORE=%%c"
echo [i] Plikow wiadomosci (.eml) przed instalacja: %CNT_BEFORE%

rem ---- 5. BACKUP --------------------------------------------
if "%ISADMIN%"=="1" (set "BKROOT=%ProgramData%\WinMailMAPI\backup") else (set "BKROOT=%LOCALAPPDATA%\WinMailMAPI\backup")
set "BK=%BKROOT%\%TS%"
echo [i] Backup do: %BK%
mkdir "%BK%" >nul 2>&1

if exist "%STORE%" (
    robocopy "%STORE%" "%BK%\store" /E /COPY:DAT /DCOPY:T /R:1 /W:1 /NFL /NDL /NJH /NJS >nul
    echo     - magazyn skopiowany ^(robocopy^)
) else (
    echo     - magazyn nie istnieje ^(czysta instalacja^) — pomijam kopie plikow
)
reg query "HKCU\Software\Microsoft\Windows Mail" >nul 2>&1 && reg export "HKCU\Software\Microsoft\Windows Mail" "%BK%\reg_winmail_user.reg" /y >nul && echo     - konfiguracja uzytkownika ^(HKCU Windows Mail^) wyeksportowana
reg query "HKCU\Software\Clients\Mail" >nul 2>&1 && reg export "HKCU\Software\Clients\Mail" "%BK%\reg_clients_user.reg" /y >nul && echo     - domyslny klient poczty HKCU wyeksportowany
reg query "HKCU\Software\WinMailMAPI" >nul 2>&1 && reg export "HKCU\Software\WinMailMAPI" "%BK%\reg_wmmapi_user.reg" /y >nul
set HASPFX86=0
if exist "%PFX86%" set HASPFX86=1
if "%ISADMIN%"=="1" (
    reg query "HKLM\SOFTWARE\Clients\Mail" >nul 2>&1 && reg export "HKLM\SOFTWARE\Clients\Mail" "%BK%\reg_clients_machine.reg" /y >nul && echo     - klienci poczty HKLM ^(64-bit^) wyeksportowani
    if "%HASPFX86%"=="1" reg query "HKLM\SOFTWARE\WOW6432Node\Clients\Mail" >nul 2>&1 && reg export "HKLM\SOFTWARE\WOW6432Node\Clients\Mail" "%BK%\reg_clients_machine32.reg" /y >nul
)

rem manifest instalacji — kolejne dopisy, bez blokow (wartosci moga zawierac nawiasy)
> "%BK%\manifest.txt" echo instalacja=%TS%
>> "%BK%\manifest.txt" echo tryb=%ISADMIN%
>> "%BK%\manifest.txt" echo winmail=%WMDIR%\WinMail.exe
>> "%BK%\manifest.txt" echo magazyn=%STORE%
>> "%BK%\manifest.txt" echo licznik_przed=%CNT_BEFORE%
>> "%BK%\manifest.txt" echo domyslne_hklm_bylo=
>> "%BK%\manifest.txt" echo domyslne_hkcu_bylo=

rem zapisz poprzednie wartosci domyslne klienta do manifestu
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Clients\Mail" /ve 2^>nul') do >> "%BK%\manifest.txt" echo domyslne_hklm_bylo=%%b
for /f "tokens=2*" %%a in ('reg query "HKCU\Software\Clients\Mail" /ve 2^>nul') do >> "%BK%\manifest.txt" echo domyslne_hkcu_bylo=%%b

rem ---- 6. kopiowanie DLL ------------------------------------
rem bez blokow: %PFX86% zawiera nawiasy ^(Program Files (x86)^)
set DEST=
if "%ISADMIN%"=="1" if exist "%PFX86%" set "DEST=%PFX86%\WinMailMAPI"
if not defined DEST if "%ISADMIN%"=="1" set "DEST=%PF%\WinMailMAPI"
if not defined DEST set "DEST=%LOCALAPPDATA%\WinMailMAPI"
echo [i] Instalacja DLL do: %DEST%
mkdir "%DEST%" >nul 2>&1
copy /y "%SRCBIN%\WinMailMAPI.dll" "%DEST%\WinMailMAPI.dll" >nul || goto :restore_and_fail
if exist "%SRCBIN%\WinMailMAPI64.dll" copy /y "%SRCBIN%\WinMailMAPI64.dll" "%DEST%\WinMailMAPI64.dll" >nul || goto :restore_and_fail
copy /y "%SRCBIN%\wmtest86.exe" "%DEST%\wmtest86.exe" >nul
if exist "%SRCBIN%\wmtest64.exe" copy /y "%SRCBIN%\wmtest64.exe" "%DEST%\wmtest64.exe" >nul

rem ---- 7. rejestracja MAPI ----------------------------------
echo [i] Rejestracja dostawcy MAPI...
rem 7a. konfiguracja patcha (sciezka WinMail + tryb zalacznikow)
reg add "HKCU\Software\WinMailMAPI" /v "WinMailPath" /t REG_SZ /d "%WMDIR%\WinMail.exe" /f >nul
reg add "HKCU\Software\WinMailMAPI" /v "AttachMode" /t REG_SZ /d "drop" /f >nul

if "%ISADMIN%"=="1" goto :regadmin

rem 7b. rejestracja per-user (HKCU) — widoczna dla aplikacji 32-bit
reg add "HKCU\Software\Clients\Mail\Windows Mail" /ve /d "Windows Mail" /f >nul
reg add "HKCU\Software\Clients\Mail\Windows Mail" /v "DLLPath" /t REG_SZ /d "%DEST%\WinMailMAPI.dll" /f >nul
reg add "HKCU\Software\Clients\Mail\Windows Mail\shell\open\command" /ve /d "\"%WMDIR%\WinMail.exe\" /mailurl:\"%%1\"" /f >nul
reg add "HKCU\Software\Clients\Mail" /ve /d "Windows Mail" /f >nul
echo     - HKCU: DLLPath = %DEST%\WinMailMAPI.dll
echo [!] 64-bitowe aplikacje MAPI wymagaja instalacji jako administrator.
goto :verify

:regadmin
rem 7c. rejestracja maszynowa.
rem UWAGA: HKLM\SOFTWARE\Clients jest kluczem WSPOLDZIELONYM miedzy widokami
rem 32/64-bit (jeden fizyczny klucz) — rejestrujemy JEDNA biblioteke.
rem Domyslnie x86 (aplikacje 32-bit: Office 32-bit, programy fiskalne,
rem stare ERP — najczestszy przypadek). /X64CLIENT przelacza na x64.
set "REGDLL=%DEST%\WinMailMAPI.dll"
if /i "%~1"=="/X64CLIENT" set "REGDLL=%DEST%\WinMailMAPI64.dll"
reg add "HKLM\SOFTWARE\Clients\Mail\Windows Mail" /v "DLLPath" /t REG_SZ /d "%REGDLL%" /f >nul
rem domyslny klient: nie nadpisuj istniejacego wyboru w HKLM; ustaw per-user
reg add "HKCU\Software\Clients\Mail" /ve /d "Windows Mail" /f >nul
echo     - HKLM: DLLPath = %REGDLL%
if /i "%~1"=="/X64CLIENT" (echo     - zarejestrowana wersja: x64 ^(klienci 64-bit^)) else (echo     - zarejestrowana wersja: x86 ^(klienci 32-bit; /X64CLIENT przelacza^))
echo     - HKCU: domyslny klient = Windows Mail ^(nie nadpisano HKLM^)

rem ---- 8. weryfikacja integralnosci -------------------------
:verify
set CNT_AFTER=0
if exist "%STORE%" for /f %%c in ('dir /s /b "%STORE%\*.eml" 2^>nul ^| find /c /v ""') do set "CNT_AFTER=%%c"
echo [i] Plikow wiadomosci po instalacji: %CNT_AFTER%
if not "%CNT_BEFORE%"=="%CNT_AFTER%" goto :restore_and_fail

set DLLCHECK=0
if "%ISADMIN%"=="1" (
    reg query "HKLM\SOFTWARE\Clients\Mail\Windows Mail" /v DLLPath >nul 2>&1
    if not errorlevel 1 set DLLCHECK=1
    reg query "HKLM\SOFTWARE\WOW6432Node\Clients\Mail\Windows Mail" /v DLLPath >nul 2>&1
    if errorlevel 1 set DLLCHECK=0
) else (
    reg query "HKCU\Software\Clients\Mail\Windows Mail" /v DLLPath >nul 2>&1
    if not errorlevel 1 set DLLCHECK=1
)
if not "%DLLCHECK%"=="1" goto :restore_and_fail

rem ---- 9. samotest biblioteki -------------------------------
echo.
echo [i] Samotest biblioteki:
set "TESTDLL=%DEST%\WinMailMAPI.dll"
if exist "%DEST%\wmtest86.exe" "%DEST%\wmtest86.exe" --exports "%TESTDLL%"

echo.
echo ============================================
echo  INSTALACJA ZAKONCZONA POMYSLNIE
echo  Backup: %BK%
echo  Test reczny: kliknij plik prawym ^> Wyslij do ^> Adresat poczty
echo  lub w Wordzie: Plik ^> Udostepnij ^> Wyślij jako zalacznik
echo ============================================
exit /b 0

:restore_and_fail
echo.
echo [!] WYKRYTO ROZBIEZNOSC — przywracam backup...
if exist "%STORE%" if exist "%BK%\store" robocopy "%BK%\store" "%STORE%" /E /COPY:DAT /R:1 /W:1 >nul
if exist "%BK%\reg_winmail_user.reg" reg import "%BK%\reg_winmail_user.reg" >nul 2>&1
if exist "%BK%\reg_clients_user.reg" reg import "%BK%\reg_clients_user.reg" >nul 2>&1
if "%ISADMIN%"=="1" if exist "%BK%\reg_clients_machine.reg" reg import "%BK%\reg_clients_machine.reg" >nul 2>&1
if "%ISADMIN%"=="1" if exist "%BK%\reg_clients_machine32.reg" reg import "%BK%\reg_clients_machine32.reg" >nul 2>&1
rem usun czesci dodane w tej instalacji (import nie usuwa nadmiarowych wartosci)
reg delete "HKCU\Software\WinMailMAPI" /f >nul 2>&1
if not "%ISADMIN%"=="1" reg delete "HKCU\Software\Clients\Mail\Windows Mail" /f >nul 2>&1
echo [!] Instalacja przerwana, stan przywrocony z backupu: %BK%
exit /b 1

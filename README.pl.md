# winmail-mapi — dostawca Simple MAPI dla Windows Mail na Windows 7–10

[🇬🇧 English: README.md](README.md)

Windows Mail (następca Outlook Express z Visty) nie posiada **żadnego**
Simple MAPI. Na Windows 7/8/10 uruchamia się go, kopiując folder
`Windows Mail` (wersję 32-bit) z Visty — a potem „Wyślij do → Adresat poczty",
„Wyślij jako załącznik" w Wordzie, programy fiskalne, ERP i faxy **nie działają**,
bo te integracje mówią Simple MAPI, a Windows Mail nie ma dostawcy.

**winmail-mapi** to właśnie ten brakujący dostawca: mała, samowystarczalna
biblioteka DLL eksportująca pełny zestaw Simple MAPI i mostkująca go do
Windows Mail.

```
prawy klik na pliku → Wyślij do → Adresat poczty  →  okno wiadomości Windows Mail
Word/Excel „Wyślij jako załącznik" (MAPI)         →  okno wiadomości Windows Mail
```

## Co robi

| Funkcja MAPI | Implementacja |
|---|---|
| `MAPISendMail`, `MAPISendDocuments` | buduje adres `mailto:` (odbiorcy, temat, treść — konwersja ANSI→UTF-8 dla polskich znaków) i otwiera okno tworzenia przez `WinMail.exe /mailurl:`; załączniki są wstrzykiwane do okna komunikatem `WM_DROPFILES` |
| `MAPIFindNext`, `MAPIReadMail` | odczyt (bez zapisu) wiadomości `.eml` z lokalnego magazynu Windows Mail |
| `MAPILogon`, `MAPILogoff` | Windows Mail nie ma profili — logowanie zawsze się udaje |
| `MAPIResolveName`, `MAPIAddress` | przekazanie odbiorców bez zmian (bez okna książki adresowej) |
| `MAPISaveMail`, `MAPIDeleteMail`, `MAPIDetails` | nieobsługiwane (patrz *Ograniczenia*) |

Instalator **nie dotyka magazynu wiadomości**: przed zmianami robi pełny backup
(kopia magazynu + eksporty rejestru), liczy pliki `.eml` przed/po i
**automatycznie przywraca stan** przy rozbieżności. Deinstalator wraca do stanu
sprzed instalacji (zweryfikowane end-to-end).

## Wymagania

1. **Windows Mail zainstalowany i uruchamialny** (32-bit `WinMail.exe`
   w `C:\Program Files (x86)\Windows Mail`). Instalator sam go wykryje i
   jasno komunikuje brak. Projekt **nie zawiera i nie rozprowadza** plików
   Windows Mail (WinMail.exe, msoe.dll) — to prawa Microsoftu; użyj własnej
   kopii z instalacji Windows Vista.
2. **Co najmniej jedno konto pocztowe skonfigurowane w Windows Mail**
   (Narzędzia → Konta) — bez konta okno tworzenia działa, ale wysyłka nie.
3. `installer\install.cmd` uruchomiony **jako administrator** (prawy klik →
   Uruchom jako administrator) — rejestracja maszynowa. Bez uprawnień
   instalacja zadziała per-user i pokryje tylko aplikacje 32-bit.

To cała konfiguracja: bez zmian w „Aplikacje domyślne", bez restartu, bez
dodatkowych bibliotek.

## Bezpieczeństwo

- Wydane DLL **nie są podpisane cyfrowo**, więc SmartScreen może ostrzegać
  przy uruchomieniu instalatora. Zanim zaakceptujesz, zweryfikuj pobierany
  plik: porównaj `Get-FileHash` zipa z `SHA256SUMS.txt` z wydania i/lub
  zbuduj DLL sam ze źródeł (budowa CI jest odtwarzalna, pinowana haszem).
- Pliki przeniesione przez internet mogą mieć znacznik Mark-of-the-Web —
  jeśli Windows zablokuje `.cmd`: prawy klik → Właściwości → **Odblokuj**
  (albo odblokuj ZIP przed rozpakowaniem).
- Patch zmienia system minimalnie i w pełni odwracalnie: kopiuje dwie
  biblioteki do `Program Files (x86)\WinMailMAPI`, ustawia `DLLPath` pod
  `HKLM\SOFTWARE\Clients\Mail\Windows Mail` (klucz współdzielony 32/64-bit)
  i domyślnego klienta poczty per-user (`HKCU\Software\Clients\Mail`).
  Nic więcej — brak usług, autostartu, zadań harmonogramu, procesów w tle.

## Ograniczenia (świadome, uczciwie opisane)

- **Wysyłka zawsze przez okno tworzenia** — Windows Mail nie ma trybu cichej
  wysyłki; aplikacja dostaje wypełnione okno, użytkownik klika „Wyślij".
- **Załączniki** przechodzą przez marshalling `WM_DROPFILES` (zweryfikowany
  dla kombinacji procesów x64→x86, x86→x86 i x64→x64). Jedyna kombinacja,
  w której załączniki nie przechodzą, to klient 32-bit + Windows Mail 64-bit
  — nietypowa, bo na Win7–10 używa się 32-bitowej wersji z Visty.
- `MAPISaveMail` nieobsługiwane: magazyn Windows Mail to baza danych bez
  publicznego API zapisu. `MAPIDeleteMail` jest domyślnie zablokowane
  (ochrona poczty; po jawnym włączeniu przenosi pliki do kwarantanny
  `_wmapi_trash` zamiast usuwać). `MAPIDetails` nieobsługiwane.
- ID wiadomości dla `MAPIFindNext` wynikają z nazw plików i mogą się zmienić
  po reorganizacji magazynu.

## Konfiguracja

Wartości rejestru w `HKCU\Software\WinMailMAPI` (pierwszą ustawia instalator):

| Wartość | Znaczenie |
|---|---|
| `WinMailPath` | ścieżka do `WinMail.exe` |
| `AttachMode` | `drop` (domyślny) = wstrzykiwanie `WM_DROPFILES`; `url` = parametr `attach` mailto; `off` |
| `AllowDeleteMail` | `1` = `MAPIDeleteMail` przenosi plik do `_wmapi_trash` (domyślnie odmawia) |

## Budowanie

Bez Visual Studio i bez Windows SDK — kompilator to
[`zig cc`](https://ziglang.org/download/), DLL mają statycznie linkowany CRT.

```cmd
tools\build.cmd
```

Wersja narzędzia jest przypięta: **zig 0.17.1**,
`zig-x86_64-windows-0.17.1.zip`,
SHA-256 `b03f477a1943673e02aca0f570e8030dffd536ce1f45cbc4d3326ec31159562e`
(skrypt sam pobiera go do `tools\`; własną kopię wskażesz zmienną `ZIG`).

CI (`.github/workflows/build.yml`) buduje x86+x64 na `windows-latest` z tym
samym przypiętym haszem, uruchamia testy i dołącza zip + `SHA256SUMS.txt`
do wydań przy tagach.

## Testy

`bin\wmtest86.exe` / `bin\wmtest64.exe` badają dostawcę niczego nie wysyłając:
eksporty przez `GetProcAddress`, przepływ wywołań (`MAPILogon`,
`MAPIResolveName`, `MAPIAddress`, `MAPIFreeBuffer`), dokładną budowę adresu
`mailto:` i pełną rundę `MAPISendMail` z atrapą launcher przechwytującą wiersz
polecenia. `installer\selftest.cmd` uruchamia całość na maszynie docelowej;
`--sendlive` otwiera prawdziwe okno Windows Mail z testowym załącznikiem
(nic nie jest wysyłane automatycznie).

## Zobacz też

- [jonaski/winmailtest](https://github.com/jonaski/winmailtest) — graficzny
  tester Simple MAPI na Windows.
- [go-mapi](https://github.com/marcfargas/go-mapi) — inne użycie tego samego
  mechanizmu rejestracji: przechwytuje „Wyślij do" i przekierowuje do
  Gmaila zamiast lokalnego klienta.
- Gdyby routing MAPI szwankował po odinstalowaniu innego oprogramowania
  pocztowego, wbudowany `fixmapi.exe` resetuje stub MAPI32.

## Licencja

MIT — [LICENSE](LICENSE). Windows Mail jest produktem Microsoftu i **nie**
jest częścią ani dystrybucją tego projektu.

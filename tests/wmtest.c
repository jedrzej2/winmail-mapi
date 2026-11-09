/* wmtest.c — harness testowy WinMailMAPI.dll
 *
 * Tryby:
 *   --exports <dll>             sprawdza, czy wszystkie eksporty są widoczne
 *                               przez GetProcAddress (nazwy bez dekoracji)
 *   --calltest <dll>            wywołuje MAPILogon/ResolveName/Address/
 *                               FreeBuffer/FindNext jak prawdziwy klient MAPI
 *   --preview <dll>             buduje przykładową wiadomość i drukuje mailto:
 *   --sendtest <dll>            pełny MAPISendMail z atrapą WinMail.exe
 *                               (launch = ten harness w trybie fakelauncher)
 *   --diag <dll>                drukuje WmMapiDiag()
 *   --dropserver <plik>         ukryte okno przyjmujące WM_DROPFILES
 *   --droptest [serwer.exe]     test marshallingu HDROP między procesami
 *
 * Tryb automatyczny: jeżeli wiersz polecenia zawiera "/mailurl:",
 * harness działa jako atrapa WinMail — zapisuje wiersz do pliku
 * wskazanego zmienną WMTEST_FAKE_OUT i kończy działanie.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "../src/wm_mapi.h"

#define WM_URL_MAX 28000

typedef ULONG (FAR PASCAL *PFN_LOGON)(ULONG_PTR, LPSTR, LPSTR, FLAGS, ULONG, LPLHANDLE);
typedef ULONG (FAR PASCAL *PFN_LOGOFF)(LHANDLE, ULONG_PTR, FLAGS, ULONG);
typedef ULONG (FAR PASCAL *PFN_SEND)(LHANDLE, ULONG_PTR, lpMapiMessage, FLAGS, ULONG);
typedef ULONG (FAR PASCAL *PFN_RESOLVE)(LHANDLE, ULONG_PTR, LPSTR, FLAGS, ULONG, lpMapiRecipDesc FAR *);
typedef ULONG (FAR PASCAL *PFN_ADDRESS)(LHANDLE, ULONG_PTR, LPSTR, ULONG, LPSTR, ULONG, lpMapiRecipDesc, FLAGS, ULONG, lpMapiRecipDesc FAR *);
typedef ULONG (FAR PASCAL *PFN_FINDNEXT)(LHANDLE, ULONG_PTR, LPSTR, LPSTR, FLAGS, ULONG, LPSTR);
typedef ULONG (FAR PASCAL *PFN_FREEBUF)(LPVOID);
typedef ULONG (FAR PASCAL *PFN_VER)(char *, DWORD);
typedef ULONG (FAR PASCAL *PFN_URL)(lpMapiMessage, FLAGS, char *, DWORD);
typedef ULONG (FAR PASCAL *PFN_DIAG)(char *, DWORD);

static const char *g_exportNames[] = {
    "MAPILogon", "MAPILogoff", "MAPISendMail", "MAPISendDocuments",
    "MAPIFindNext", "MAPIReadMail", "MAPISaveMail", "MAPIDeleteMail",
    "MAPIFreeBuffer", "MAPIAddress", "MAPIDetails", "MAPIResolveName",
    "WmMapiGetVersion", "WmMapiPreviewUrl", "WmMapiDiag", NULL
};

/* ------------------------------------------------------------------ */
/* atrapa WinMail.exe                                                  */
/* ------------------------------------------------------------------ */
static BOOL ArgHas(const char *needle)
{
    return strstr(GetCommandLineA(), needle) != NULL;
}

static int Fakelauncher(void)
{
    const char *out = getenv("WMTEST_FAKE_OUT");
    FILE *f;
    if (!out || !out[0]) out = "wmtest_fake_out.txt";
    f = fopen(out, "wb");
    if (f) {
        fputs(GetCommandLineA(), f);
        fclose(f);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
static int TestExports(HMODULE h)
{
    int i, fails = 0;
    for (i = 0; g_exportNames[i]; i++) {
        void *p = (void *)GetProcAddress(h, g_exportNames[i]);
        printf("  %-20s %s\n", g_exportNames[i], p ? "OK" : "BRAK");
        if (!p) fails++;
    }
    printf("Eksporty: %s (%d bledow)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}

static int TestCallFlow(HMODULE h)
{
    PFN_LOGON pLogon = (PFN_LOGON)GetProcAddress(h, "MAPILogon");
    PFN_LOGOFF pLogoff = (PFN_LOGOFF)GetProcAddress(h, "MAPILogoff");
    PFN_RESOLVE pResolve = (PFN_RESOLVE)GetProcAddress(h, "MAPIResolveName");
    PFN_ADDRESS pAddr = (PFN_ADDRESS)GetProcAddress(h, "MAPIAddress");
    PFN_FINDNEXT pFind = (PFN_FINDNEXT)GetProcAddress(h, "MAPIFindNext");
    PFN_FREEBUF pFree = (PFN_FREEBUF)GetProcAddress(h, "MAPIFreeBuffer");
    int fails = 0;
    LHANDLE hSession = 0;
    lpMapiRecipDesc rec = NULL, newRec = NULL;
    MapiRecipDesc one;
    char msgId[64];
    ULONG rc;

    if (!pLogon || !pLogoff || !pResolve || !pAddr || !pFind || !pFree) {
        printf("  ladowanie eksportow FAIL\n");
        return 1;
    }
    rc = pLogon(0, NULL, NULL, 0, 0, &hSession);
    printf("  MAPILogon          = %lu %s\n", (unsigned long)rc, rc == 0 ? "OK" : "FAIL");
    if (rc) fails++;

    rc = pResolve(0, 0, "jan@example.com", 0, 0, &rec);
    printf("  MAPIResolveName    = %lu %s\n", (unsigned long)rc, rc == 0 && rec ? "OK" : "FAIL");
    if (rc || !rec) fails++;

    ZeroMemory(&one, sizeof(one));
    one.flFlags = MAPI_TO;
    one.lpszName = "Test";
    one.lpszAddress = "test@example.com";
    rc = pAddr(0, 0, NULL, 0, NULL, 1, &one, 0, 0, &newRec);
    printf("  MAPIAddress        = %lu %s\n", (unsigned long)rc,
           (rc == 0 && newRec && newRec->lpszAddress &&
            lstrcmpA(newRec->lpszAddress, "test@example.com") == 0) ? "OK" : "FAIL");
    if (rc) fails++;

    /* pusta skrzynka -> MAPI_E_NO_MESSAGES (16); inne kody = FAIL */
    rc = pFind(0, 0, NULL, NULL, 0, 0, msgId);
    printf("  MAPIFindNext       = %lu %s\n", (unsigned long)rc,
           rc == MAPI_E_NO_MESSAGES ? "OK (brak magazynu)" :
           rc == 0 ? "OK (magazyn istnieje)" : "FAIL");
    if (rc && rc != MAPI_E_NO_MESSAGES) fails++;

    if (rec) pFree(rec);
    if (newRec) pFree(newRec);
    rc = pLogoff(0, 0, 0, 0);
    printf("  MAPILogoff         = %lu %s\n", (unsigned long)rc, rc == 0 ? "OK" : "FAIL");
    if (rc) fails++;

    printf("Wywolania: %s (%d bledow)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}

static int TestPreview(HMODULE h)
{
    PFN_URL pUrl = (PFN_URL)GetProcAddress(h, "WmMapiPreviewUrl");
    MapiRecipDesc recips[3];
    MapiFileDesc files[2];
    MapiMessage msg;
    char buf[WM_URL_MAX];
    ULONG rc;

    if (!pUrl) { printf("  WmMapiPreviewUrl BRAK\n"); return 1; }
    ZeroMemory(&msg, sizeof(msg));
    ZeroMemory(recips, sizeof(recips));
    ZeroMemory(files, sizeof(files));

    recips[0].flFlags = MAPI_TO;    recips[0].lpszAddress = "jan.kowalski@example.com";
    recips[1].flFlags = MAPI_TO;    recips[1].lpszAddress = "test@example.com";
    recips[2].flFlags = MAPI_CC;    recips[2].lpszAddress = "kopia@example.com";
    files[0].lpszPathName = "C:\\temp\\raport 1.txt";
    files[1].lpszPathName = "C:\\temp\\zdjecie.png";

    msg.lpszSubject = "WinMailMAPI selftest";
    msg.lpszNoteText = "linia 1\r\nlinia 2: koniec";
    msg.nRecipCount = 3;
    msg.lpRecips = recips;
    msg.nFileCount = 2;
    msg.lpFiles = files;

    rc = pUrl(&msg, MAPI_DIALOG, buf, sizeof(buf));
    printf("  rc = %lu\n", (unsigned long)rc);
    printf("  mailto = %s\n", buf);
    {
        const char *expect = "mailto:jan.kowalski@example.com,test@example.com"
                             "?cc=kopia@example.com"
                             "&subject=WinMailMAPI%20selftest"
                             "&body=linia%201%0D%0Alinia%202%3A%20koniec";
        int ok = (rc == 0) && (lstrcmpA(buf, expect) == 0);
        printf("  URL: %s\n", ok ? "PASS" : "FAIL (rozbieznosc powyzej)");
        return ok ? 0 : 1;
    }
}

static int TestSendMail(HMODULE h)
{
    PFN_SEND pSend = (PFN_SEND)GetProcAddress(h, "MAPISendMail");
    PFN_FREEBUF pFree = (PFN_FREEBUF)GetProcAddress(h, "MAPIFreeBuffer");
    HKEY hk;
    LONG r;
    char selfPath[MAX_PATH];
    char oldPath[MAX_PATH] = "";
    DWORD oldCb = sizeof(oldPath), oldType = 0;
    char fakeOut[MAX_PATH];
    FILE *f;
    MapiRecipDesc recips[2];
    MapiMessage msg;
    char line[1024];
    int fails = 0;
    ULONG rc;

    if (!pSend || !pFree) { printf("  eksporty FAIL\n"); return 1; }

    GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    GetEnvironmentVariableA("TEMP", fakeOut, MAX_PATH);
    strcat_s(fakeOut, MAX_PATH, "\\wmtest_fake_out.txt");
    DeleteFileA(fakeOut);
    SetEnvironmentVariableA("WMTEST_FAKE_OUT", fakeOut);

    /* ustaw atrapę WinMail = ten harness; zapamiętaj poprzednią wartość */
    r = RegGetValueA(HKEY_CURRENT_USER, "Software\\WinMailMAPI", "WinMailPath",
                     RRF_RT_REG_SZ, &oldType, (PBYTE)oldPath, &oldCb);
    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMailMAPI", 0, NULL, 0,
                    KEY_SET_VALUE, NULL, &hk, NULL);
    RegSetValueExA(hk, "WinMailPath", 0, REG_SZ, (const BYTE *)selfPath,
                   (DWORD)strlen(selfPath) + 1);
    RegCloseKey(hk);

    ZeroMemory(&msg, sizeof(msg));
    ZeroMemory(recips, sizeof(recips));
    recips[0].flFlags = MAPI_TO;
    recips[0].lpszAddress = "jan@example.com";
    recips[1].flFlags = MAPI_TO;
    recips[1].lpszAddress = "test@example.com";
    msg.lpszSubject = "WinMailMAPI sendtest";
    msg.lpszNoteText = "tresc\r\nwiadomosci";
    msg.nRecipCount = 2;
    msg.lpRecips = recips;

    rc = pSend(0, 0, &msg, MAPI_DIALOG, 0);
    printf("  MAPISendMail rc    = %lu %s\n", (unsigned long)rc,
           rc == 0 ? "OK" : "FAIL");
    if (rc) fails++;

    Sleep(400);
    f = fopen(fakeOut, "rb");
    if (!f) {
        printf("  atrapa nie otrzymala polecenia: FAIL\n");
        fails++;
    } else {
        size_t n = fread(line, 1, sizeof(line) - 1, f);
        line[n] = '\0';
        fclose(f);
        {
            const char *want = "/mailurl:mailto:jan@example.com,test@example.com"
                               "?subject=WinMailMAPI%20sendtest"
                               "&body=tresc%0D%0Awiadomosci";
            int ok = strstr(line, want) != NULL;
            printf("  cmdline: %s\n", line);
            printf("  wysylka: %s\n", ok ? "PASS" : "FAIL (rozbieznosc powyzej)");
            if (!ok) fails++;
        }
        DeleteFileA(fakeOut);
    }

    /* przywróć poprzednią wartość WinMailPath */
    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMailMAPI", 0, NULL, 0,
                    KEY_SET_VALUE, NULL, &hk, NULL);
    if (r == ERROR_SUCCESS && oldPath[0])
        RegSetValueExA(hk, "WinMailPath", 0, REG_SZ, (const BYTE *)oldPath, oldCb);
    else
        RegDeleteValueA(hk, "WinMailPath");
    RegCloseKey(hk);

    printf("SendTest: %s (%d bledow)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}

static int TestDiag(HMODULE h)
{
    PFN_DIAG pDiag = (PFN_DIAG)GetProcAddress(h, "WmMapiDiag");
    char buf[4096];
    ULONG rc;
    if (!pDiag) { printf("  WmMapiDiag BRAK\n"); return 1; }
    rc = pDiag(buf, sizeof(buf));
    if (rc != 0) { printf("  rc = %lu FAIL\n", (unsigned long)rc); return 1; }
    fputs(buf, stdout);
    return 0;
}

/* zywy test: otwiera okno tworzenia wiadomosci w Windows Mail
 * (z zalacznikiem testowym); NIC nie wysyla automatycznie */
static int SendLive(HMODULE h)
{
    PFN_SEND pSend = (PFN_SEND)GetProcAddress(h, "MAPISendMail");
    MapiRecipDesc recips[1];
    MapiFileDesc files[1];
    MapiMessage msg;
    char tmp[MAX_PATH], attach[MAX_PATH];
    FILE *f;
    ULONG rc;

    if (!pSend) { printf("  MAPISendMail BRAK\n"); return 1; }
    GetEnvironmentVariableA("TEMP", tmp, MAX_PATH);
    strcat_s(tmp, MAX_PATH, "\\");
    strcpy_s(attach, MAX_PATH, tmp);
    strcat_s(attach, MAX_PATH, "wmtest_live_zalacznik.txt");
    f = fopen(attach, "wb");
    if (f) { fputs("Test WinMailMAPI — zalacznik.\n", f); fclose(f); }

    ZeroMemory(&msg, sizeof(msg));
    recips[0].flFlags = MAPI_TO;
    recips[0].lpszAddress = "test@example.com";
    files[0].lpszPathName = attach;
    files[0].lpszFileName = "zalacznik.txt";
    msg.lpszSubject = "WinMailMAPI test zywy";
    msg.lpszNoteText = "Jesli widzisz ta wiadomosc z zalacznikiem, "
                       "patch MAPI dziala.\r\n"
                       "(Wiadomosc NIE zostala wyslana automatycznie "
                       "- zamknij okno lub wyslij ja do siebie.)";
    msg.nRecipCount = 1;
    msg.lpRecips = recips;
    msg.nFileCount = 1;
    msg.lpFiles = files;

    printf("  otwieram okno tworzenia wiadomosci w Windows Mail...\n");
    rc = pSend(0, 0, &msg, MAPI_DIALOG, 0);
    printf("  MAPISendMail rc = %lu %s\n", (unsigned long)rc,
           rc == 0 ? "OK (sprawdz okno i zalacznik)" : "FAIL");
    return rc == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* test marshallingu WM_DROPFILES                                      */
/* ------------------------------------------------------------------ */

/* długość ścieżki w znakach (do wyliczenia bufora UTF-16) */
static size_t wcslen_a(const char *s) { return strlen(s); }

static int DropServer(const char *outfile)
{
    static const char cls[] = "WMTestDropWnd";
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    FILE *f;
    UINT_PTR timer;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.lpszClassName = cls;
    wc.hInstance = GetModuleHandleA(NULL);
    if (!RegisterClassA(&wc)) { printf("register FAIL %lu\n", GetLastError()); return 1; }
    hwnd = CreateWindowExA(0, cls, "WMTestDrop", 0, 0, 0, 0, 0, NULL, NULL,
                           GetModuleHandleA(NULL), NULL);
    if (!hwnd) { printf("window FAIL %lu\n", GetLastError()); return 1; }

    timer = SetTimer(hwnd, 1, 6000, NULL);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_TIMER && (UINT_PTR)msg.wParam == timer) break;
        if (msg.message == WM_DROPFILES) {
            HDROP hDrop = (HDROP)msg.wParam;
            UINT count = DragQueryFileA(hDrop, (UINT)-1, NULL, 0);
            char name1[MAX_PATH] = "", name2[MAX_PATH] = "";
            DragQueryFileA(hDrop, 0, name1, MAX_PATH);
            if (count > 1) DragQueryFileA(hDrop, 1, name2, MAX_PATH);
            DragFinish(hDrop);
            f = fopen(outfile, "wb");
            if (f) {
                fprintf(f, "OK;%u;%s;%s", (unsigned)count, name1, name2);
                fclose(f);
            }
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

static void MakeDropFile(const char *path, const char *tag)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(tag, f); fclose(f); }
}

static int DropTest(const char *serverExe)
{
    char tmp[MAX_PATH], fA[MAX_PATH], fB[MAX_PATH], outfile[MAX_PATH];
    char cmdline[MAX_PATH * 3 + 128];
    char serverFull[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HWND hwnd = NULL;
    int attempt;
    int fails = 0;
    SIZE_T need;
    DWORD cbBytes = 0;
    HGLOBAL hDrop;
    WMDROPFILES *df;
    WCHAR *w, *wbase;
    FILE *f;
    char result[1024];

    if (!GetFullPathNameA(serverExe, MAX_PATH, serverFull, NULL)) {
        printf("  GetFullPathName FAIL\n");
        return 1;
    }
    GetEnvironmentVariableA("TEMP", tmp, MAX_PATH);
    strcat_s(tmp, MAX_PATH, "\\");
    strcpy_s(fA, MAX_PATH, tmp); strcat_s(fA, MAX_PATH, "wmtest_drop_a.txt");
    strcpy_s(fB, MAX_PATH, tmp); strcat_s(fB, MAX_PATH, "wmtest_drop_b.txt");
    strcpy_s(outfile, MAX_PATH, tmp); strcat_s(outfile, MAX_PATH, "wmtest_drop_result.txt");
    MakeDropFile(fA, "A");
    MakeDropFile(fB, "B");
    DeleteFileA(outfile);

    /* serwer jako osobny proces (architektura wg podanego exe) */
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    cmdline[0] = '\0';
    strcat_s(cmdline, sizeof(cmdline), "\"");
    strcat_s(cmdline, sizeof(cmdline), serverFull);
    strcat_s(cmdline, sizeof(cmdline), "\" --dropserver \"");
    strcat_s(cmdline, sizeof(cmdline), outfile);
    strcat_s(cmdline, sizeof(cmdline), "\"");
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("  uruchomienie serwera FAIL %lu\n  serverFull: %s\n  cmdline: %s\n",
               GetLastError(), serverFull, cmdline);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (attempt = 0; attempt < 20 && !hwnd; attempt++) {
        Sleep(150);
        hwnd = FindWindowA("WMTestDropWnd", NULL);
    }
    if (!hwnd) { printf("  nie znaleziono okna serwera FAIL\n"); return 1; }

    /* zbuduj HDROP w TYM procesie i wyślij do serwera */
    cbBytes = (DWORD)(wcslen_a(fA) + wcslen_a(fB) + 4) * sizeof(WCHAR);
    need = sizeof(WMDROPFILES) + cbBytes + sizeof(WCHAR) * 2;
    hDrop = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, need);
    if (!hDrop) { printf("  GlobalAlloc FAIL\n"); return 1; }
    df = (WMDROPFILES *)GlobalLock(hDrop);
    df->pFiles = sizeof(WMDROPFILES);
    df->fWide = TRUE;
    wbase = w = (WCHAR *)((char *)df + sizeof(WMDROPFILES));
    {
        int n;
        n = MultiByteToWideChar(CP_ACP, 0, fA, -1, w, (int)(cbBytes / sizeof(WCHAR)));
        w += n;
        n = MultiByteToWideChar(CP_ACP, 0, fB, -1, w, (int)((cbBytes / sizeof(WCHAR)) - (w - wbase)));
        w += n;
        *w = 0;
    }
    GlobalUnlock(hDrop);
    if (!PostMessageA(hwnd, WM_DROPFILES, (WPARAM)hDrop, 0)) {
        printf("  PostMessage FAIL %lu\n", GetLastError());
        GlobalFree(hDrop);
        return 1;
    }

    Sleep(2500);
    result[0] = '\0';
    f = fopen(outfile, "rb");
    if (f) {
        size_t n = fread(result, 1, sizeof(result) - 1, f);
        result[n] = '\0';
        fclose(f);
    }
    {
        const char *wantA = strrchr(fA, '\\') + 1;
        int ok = strncmp(result, "OK;2;", 5) == 0 && strstr(result, wantA) != NULL;
        printf("  wynik serwera: %s\n", result[0] ? result : "(brak odpowiedzi)");
        printf("  HDROP %s->%s: %s\n",
#ifdef _WIN64
               "x64", "x86",
#else
               "x86", "x64",
#endif
               ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int i;

    if (ArgHas("/mailurl:")) return Fakelauncher();

    if (argc < 2) {
        printf("wmtest — tryby: --exports --calltest --preview --sendtest --diag "
               "--dropserver <plik> --droptest [serwer.exe]  |  <dll>\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (lstrcmpA(argv[i], "--dropserver") == 0) {
            const char *out = (i + 1 < argc) ? argv[i + 1] : "wmtest_drop_result.txt";
            return DropServer(out);
        }
    }

    {
        const char *dllPath = NULL;
        const char *mode = NULL;
        const char *serverExe = NULL;
        HMODULE h;
        int rc = 0;
        for (i = 1; i < argc; i++) {
            if (strncmp(argv[i], "--", 2) == 0) {
                if (lstrcmpA(argv[i], "--droptest") == 0) {
                    mode = argv[i];
                    if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                        serverExe = argv[i + 1];
                        i++;
                    }
                } else {
                    mode = argv[i];
                }
            } else if (!dllPath && strstr(argv[i], ".dll")) {
                dllPath = argv[i];
            } else if (!serverExe) {
                serverExe = argv[i];
            }
        }
        if (lstrcmpA(mode ? mode : "", "--droptest") == 0) {
            if (!serverExe) { printf("--droptest wymaga sciezki do serwera\n"); return 2; }
            return DropTest(serverExe);
        }
        if (!dllPath) { printf("brak sciezki DLL\n"); return 2; }
        h = LoadLibraryA(dllPath);
        if (!h) {
            printf("LoadLibrary('%s') FAIL %lu\n", dllPath, GetLastError());
            return 1;
        }
        printf("DLL: %s (%s)\n", dllPath,
#ifdef _WIN64
               "x64");
#else
               "x86");
#endif
        if (lstrcmpA(mode, "--exports") == 0) rc = TestExports(h);
        else if (lstrcmpA(mode, "--calltest") == 0) rc = TestCallFlow(h);
        else if (lstrcmpA(mode, "--preview") == 0) rc = TestPreview(h);
        else if (lstrcmpA(mode, "--sendtest") == 0) rc = TestSendMail(h);
        else if (lstrcmpA(mode, "--sendlive") == 0) rc = SendLive(h);
        else if (lstrcmpA(mode, "--diag") == 0) rc = TestDiag(h);
        else if (lstrcmpA(mode, "--all") == 0) {
            rc  = TestExports(h);
            rc += TestCallFlow(h);
            rc += TestPreview(h);
            rc += TestSendMail(h);
        } else {
            printf("nieznany tryb: %s\n", mode ? mode : "(brak)");
            rc = 2;
        }
        FreeLibrary(h);
        return rc;
    }
}

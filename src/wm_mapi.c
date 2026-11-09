/* wm_mapi.c — WinMailMAPI.dll
 *
 * Dostawca Simple MAPI dla Windows Mail (WinMail.exe) na Windows 7/8/8.1/10,
 * gdzie Windows Mail nie eksportuje własnych funkcji Simple MAPI
 * (MSOE.dll z Windows Mail wycięła warstwę Simple MAPI znaną z Outlook Express).
 *
 * Zasada działania:
 *  - MAPISendMail/MAPISendDocuments składają wiadomość w adres mailto: i
 *    otwierają okno tworzenia wiadomości przez WinMail.exe /mailurl:;
 *    załączniki są wstrzykiwane komunikatem WM_DROPFILES do okna tworzenia
 *    (tryb domyślny "drop") albo parametrem attach= mailto (tryb "url").
 *  - MAPIFindNext/MAPIReadMail czytają pliki .eml lokalnego magazynu
 *    Windows Mail (tylko odczyt; ID = hasz nazwy pliku).
 *  - MAPILogon/Logoff: Windows Mail nie ma profili — każde zalogowanie
 *    kończy się powodzeniem.
 *
 * Konfiguracja (HKCU\Software\WinMailMAPI):
 *   WinMailPath      (REG_SZ)    ścieżka do WinMail.exe (installer ustawia)
 *   AttachMode       (REG_SZ)    "drop" | "url" | "off"   (domyślnie "drop")
 *   AllowDeleteMail  (REG_DWORD) 1 = MAPIDeleteMail przenosi plik do
 *                                "_wmapi_trash" w magazynie (domyślnie 0)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "wm_mapi.h"

#define WM_KEY       "Software\\WinMailMAPI"
#define WM_URL_MAX   28000          /* limit długości wiersza polecenia */
#define WM_ALLOC_MAX 4096           /* limit śledzonych alokacji */

/* ------------------------------------------------------------------ */
/* bezpieczny snprintf (msvcrt, zawsze dokańcza NUL)                   */
/* ------------------------------------------------------------------ */
static int WmSnprintf(char *buf, SIZE_T cb, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = _vsnprintf(buf, cb - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)cb - 1;
    buf[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* alokacje śledzone dla MAPIFreeBuffer                                */
/* ------------------------------------------------------------------ */
static CRITICAL_SECTION g_csAlloc;
static LPVOID           g_allocs[WM_ALLOC_MAX];
static DWORD            g_allocCount = 0;
static BOOL             g_initDone = FALSE;

static void WmInit(void)
{
    if (!g_initDone) {
        InitializeCriticalSection(&g_csAlloc);
        g_initDone = TRUE;
    }
}

static LPVOID WmAlloc(SIZE_T cb)
{
    LPVOID p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cb);
    if (p) {
        WmInit();
        EnterCriticalSection(&g_csAlloc);
        if (g_allocCount < WM_ALLOC_MAX)
            g_allocs[g_allocCount++] = p;
        LeaveCriticalSection(&g_csAlloc);
    }
    return p;
}

static char *WmStrDup(const char *s)
{
    SIZE_T cb;
    char *p;
    if (!s) s = "";
    cb = strlen(s) + 1;
    p = (char *)WmAlloc(cb);
    if (p) memcpy(p, s, cb);
    return p;
}

static char *WmStrDupN(const char *s, SIZE_T n)
{
    char *p = (char *)WmAlloc(n + 1);
    if (p && n) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* pomocnicze: rejestr / ścieżki                                       */
/* ------------------------------------------------------------------ */
static BOOL WmReadRegSz(HKEY root, const char *key, const char *value,
                        char *out, DWORD cbOut)
{
    LONG r = RegGetValueA(root, key, value,
                          RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                          NULL, (PBYTE)out, &cbOut);
    return r == ERROR_SUCCESS && out[0] != '\0';
}

static BOOL WmReadRegDword(HKEY root, const char *key, const char *value,
                           DWORD *out)
{
    DWORD cb = sizeof(DWORD);
    return RegGetValueA(root, key, value, RRF_RT_REG_DWORD, NULL,
                        (PBYTE)out, &cb) == ERROR_SUCCESS;
}

/* Znajdź WinMail.exe: konfiguracja patcha -> klucz klienta -> katalogi */
static BOOL WmFindWinMail(char *out, DWORD cbOut)
{
    char cmd[MAX_PATH + 260];
    const char *envs[] = { "ProgramFiles(x86)", "ProgramFiles", "ProgramW6432", NULL };
    char envval[MAX_PATH];
    int i;

    if (WmReadRegSz(HKEY_CURRENT_USER, WM_KEY, "WinMailPath", out, cbOut) &&
        GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES)
        return TRUE;

    if (WmReadRegSz(HKEY_LOCAL_MACHINE,
                    "SOFTWARE\\Clients\\Mail\\Windows Mail\\shell\\open\\command",
                    NULL, cmd, sizeof(cmd))) {
        /* format: "C:\...\WinMail.exe" /mailurl:"%1" — weź fragment w cudzysłowach */
        if (cmd[0] == '"') {
            char *endq = strchr(cmd + 1, '"');
            if (endq) {
                SIZE_T n = (SIZE_T)(endq - (cmd + 1));
                if (n > 0 && n < cbOut) {
                    memcpy(out, cmd + 1, n);
                    out[n] = '\0';
                    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
                }
            }
        } else {
            char *sp = strchr(cmd, ' ');
            if (sp && (SIZE_T)(sp - cmd) < cbOut) {
                memcpy(out, cmd, (SIZE_T)(sp - cmd));
                out[sp - cmd] = '\0';
                if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
            }
        }
    }

    for (i = 0; envs[i]; i++) {
        if (!GetEnvironmentVariableA(envs[i], envval, MAX_PATH)) continue;
        if (!envval[0]) continue;
        if (WmSnprintf(out, cbOut, "%s\\Windows Mail\\WinMail.exe", envval) >= (int)cbOut)
            continue;
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }
    out[0] = '\0';
    return FALSE;
}

static BOOL WmGetStoreRoot(char *out, DWORD cbOut)
{
    char laf[MAX_PATH];
    if (WmReadRegSz(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows Mail",
                    "Store Root", out, cbOut))
        return TRUE;
    if (!GetEnvironmentVariableA("LOCALAPPDATA", laf, MAX_PATH)) return FALSE;
    return WmSnprintf(out, cbOut, "%s\\Microsoft\\Windows Mail", laf) < (int)cbOut;
}

/* ------------------------------------------------------------------ */
/* kodowanie URL                                                       */
/* ------------------------------------------------------------------ */
static int IsUrlUnreserved(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return 1;
    return strchr("-_.!~*'()@", c) != NULL;
}

/* ANSI (strona kodowa systemu, np. CP-1250) -> UTF-8.
 * Windows Mail dekoduje sekwencje %XX w mailto jako UTF-8, więc treść
 * i temat muszą zostać przekonwertowane, by polskie znaki były czytelne. */
static char *AnsiToUtf8(const char *s)
{
    int nWide, nUtf8;
    WCHAR *wide;
    char *utf8;
    if (!s) return NULL;
    nWide = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (nWide <= 0) return WmStrDup(s);
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nWide * sizeof(WCHAR));
    if (!wide) return WmStrDup(s);
    MultiByteToWideChar(CP_ACP, 0, s, -1, wide, nWide);
    nUtf8 = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    utf8 = (char *)WmAlloc((SIZE_T)nUtf8 + 1);
    if (utf8) WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, nUtf8, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, wide);
    return utf8;
}

/* dokleja zakodowany tekst; 0 = zabrakło bufora */
static int AppendEncoded(char **dst, const char *end, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)src;
    if (!src) return 1;
    for (; *p; p++) {
        if (end - *dst < 4) return 0;
        if (p[0] == '\r' && p[1] == '\n') {          /* CRLF -> %0D%0A */
            *(*dst)++ = '%'; *(*dst)++ = '0'; *(*dst)++ = 'D';
            *(*dst)++ = '%'; *(*dst)++ = '0'; *(*dst)++ = 'A';
            p++;
        } else if (IsUrlUnreserved(*p)) {
            *(*dst)++ = (char)*p;
        } else {
            *(*dst)++ = '%';
            *(*dst)++ = hex[(p[0] >> 4) & 0xF];
            *(*dst)++ = hex[p[0] & 0xF];
        }
    }
    return 1;
}

static int AppendRaw(char **dst, const char *end, const char *src)
{
    SIZE_T n = strlen(src);
    if ((SIZE_T)(end - *dst) < n + 1) return 0;
    memcpy(*dst, src, n);
    *dst += n;
    **dst = '\0';
    return 1;
}

/* adres odbiorcy: lpszAddress (bez prefiksu typu, np. "SMTP:") lub nazwa z '@' */
static const char *RecipAddr(const MapiRecipDesc *r)
{
    const char *a;
    if (!r) return NULL;
    a = r->lpszAddress;
    if (a) {
        const char *colon = strchr(a, ':');
        if (colon && colon != a && _strnicmp(a, "mailto", 6) != 0) a = colon + 1;
        if (a[0]) return a;
    }
    if (r->lpszName && strchr(r->lpszName, '@')) return r->lpszName;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* budowa adresu mailto:                                               */
/* ------------------------------------------------------------------ */
static ULONG BuildMailto(const MapiMessage *msg, FLAGS flFlags,
                         char *url, DWORD cbUrl)
{
    char *p = url, *end = url + cbUrl - 1;
    static const char *paramNames[] = { NULL, NULL, "cc", "bcc" };
    DWORD i;
    int haveTo = 0, haveParam = 0;
    int wantAttach;

    *p = '\0';
    if (!AppendRaw(&p, end, "mailto:")) return MAPI_E_FAILURE;

    if (msg->lpRecips) {
        for (i = 0; i < msg->nRecipCount; i++) {
            const MapiRecipDesc *r = &msg->lpRecips[i];
            const char *a;
            if (!r || r->flFlags != MAPI_TO) continue;   /* ORIG pomijamy */
            a = RecipAddr(r);
            if (!a) continue;
            if (haveTo && !AppendRaw(&p, end, ",")) return MAPI_E_FAILURE;
            if (!AppendEncoded(&p, end, a)) return MAPI_E_FAILURE;
            haveTo = 1;
        }
    }

    if (msg->lpRecips) {
        for (i = 0; i < msg->nRecipCount; i++) {
            const MapiRecipDesc *r = &msg->lpRecips[i];
            const char *a;
            if (!r || (r->flFlags != MAPI_CC && r->flFlags != MAPI_BCC)) continue;
            a = RecipAddr(r);
            if (!a) continue;
            if (!AppendRaw(&p, end, haveParam ? "&" : "?")) return MAPI_E_FAILURE;
            if (!AppendRaw(&p, end, paramNames[r->flFlags])) return MAPI_E_FAILURE;
            if (!AppendRaw(&p, end, "=")) return MAPI_E_FAILURE;
            if (!AppendEncoded(&p, end, a)) return MAPI_E_FAILURE;
            haveParam = 1;
        }
    }

    if (msg->lpszSubject && msg->lpszSubject[0]) {
        char *s8 = AnsiToUtf8(msg->lpszSubject);
        if (!AppendRaw(&p, end, haveParam ? "&" : "?")) return MAPI_E_FAILURE;
        if (!AppendRaw(&p, end, "subject=")) return MAPI_E_FAILURE;
        if (!AppendEncoded(&p, end, s8 ? s8 : msg->lpszSubject)) return MAPI_E_FAILURE;
        haveParam = 1;
    }
    if (msg->lpszNoteText && msg->lpszNoteText[0]) {
        char *b8 = AnsiToUtf8(msg->lpszNoteText);
        if (!AppendRaw(&p, end, haveParam ? "&" : "?")) return MAPI_E_FAILURE;
        if (!AppendRaw(&p, end, "body=")) return MAPI_E_FAILURE;
        if (!AppendEncoded(&p, end, b8 ? b8 : msg->lpszNoteText)) return MAPI_E_FAILURE;
        haveParam = 1;
    }

    /* załączniki w trybie "url": parametr attach (WinMail może je zignorować) */
    wantAttach = msg->lpFiles && msg->nFileCount &&
                 !(flFlags & MAPI_SUPPRESS_ATTACH) &&
                 !(msg->flFlags & MAPI_SUPPRESS_ATTACH);
    if (wantAttach) {
        char mode[16];
        if (WmReadRegSz(HKEY_CURRENT_USER, WM_KEY, "AttachMode", mode, sizeof(mode)) &&
            lstrcmpiA(mode, "url") == 0) {
            for (i = 0; i < msg->nFileCount; i++) {
                const MapiFileDesc *f = &msg->lpFiles[i];
                char urlfile[MAX_PATH * 3];
                char *q = urlfile, *qe = urlfile + sizeof(urlfile) - 1;
                if (!f || !f->lpszPathName) continue;
                if (!AppendRaw(&q, qe, "file:///")) return MAPI_E_FAILURE;
                if (!AppendEncoded(&q, qe, f->lpszPathName)) return MAPI_E_FAILURE;
                *q = '\0';
                if (!AppendRaw(&p, end, haveParam ? "&" : "?")) return MAPI_E_FAILURE;
                if (!AppendRaw(&p, end, "attach=\"")) return MAPI_E_FAILURE;
                if (!AppendRaw(&p, end, urlfile)) return MAPI_E_FAILURE;
                if (!AppendRaw(&p, end, "\"")) return MAPI_E_FAILURE;
                haveParam = 1;
            }
        }
    }

    *p = '\0';
    if (p >= end) return MAPI_E_TEXT_TOO_LARGE;
    return SUCCESS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* uruchomienie okna tworzenia wiadomości                              */
/* ------------------------------------------------------------------ */
struct FindWinCtx { DWORD pid; HWND hwnd; };

static BOOL CALLBACK FindWinProc(HWND hwnd, LPARAM lp)
{
    struct FindWinCtx *ctx = (struct FindWinCtx *)lp;
    DWORD pid = 0;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && ctx->pid) { ctx->hwnd = hwnd; return FALSE; }
    return TRUE;
}

/* wstrzykiwanie załączników: HDROP (WM_DROPFILES) do okna tworzenia */
static BOOL WmInjectDropFiles(DWORD pid, const MapiMessage *msg)
{
    struct FindWinCtx ctx;
    UINT nFiles = 0, i;
    SIZE_T need;
    DWORD cbPathBytes = 0;
    HGLOBAL hDrop;
    WMDROPFILES *df;
    WCHAR *wpaths, *w;
    char **paths;
    BOOL posted = FALSE;
    int attempt;

    paths = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               sizeof(char *) * (msg->nFileCount + 1));
    if (!paths) return FALSE;
    for (i = 0; i < msg->nFileCount; i++) {
        if (msg->lpFiles[i].lpszPathName &&
            GetFileAttributesA(msg->lpFiles[i].lpszPathName) != INVALID_FILE_ATTRIBUTES)
            paths[nFiles++] = msg->lpFiles[i].lpszPathName;
    }
    if (!nFiles) {
        HeapFree(GetProcessHeap(), 0, paths);
        return FALSE;
    }

    for (i = 0; i < nFiles; i++) {
        int n = MultiByteToWideChar(CP_ACP, 0, paths[i], -1, NULL, 0);
        cbPathBytes += (DWORD)n * sizeof(WCHAR);
    }

    need = sizeof(WMDROPFILES) + cbPathBytes + sizeof(WCHAR) * 2;
    hDrop = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, need);
    if (!hDrop) {
        HeapFree(GetProcessHeap(), 0, paths);
        return FALSE;
    }
    df = (WMDROPFILES *)GlobalLock(hDrop);
    if (!df) {
        GlobalFree(hDrop);
        HeapFree(GetProcessHeap(), 0, paths);
        return FALSE;
    }
    df->pFiles = sizeof(WMDROPFILES);
    df->fWide = TRUE;
    wpaths = (WCHAR *)((char *)df + sizeof(WMDROPFILES));
    w = wpaths;
    for (i = 0; i < nFiles; i++) {
        int n = MultiByteToWideChar(CP_ACP, 0, paths[i], -1, w,
            (int)((cbPathBytes / sizeof(WCHAR)) - (w - wpaths)));
        if (n > 0) w += n;
    }
    *w = 0;
    GlobalUnlock(hDrop);
    HeapFree(GetProcessHeap(), 0, paths);

    /* czekaj na okno tworzenia wiadomości (do ~12 s) */
    ctx.pid = pid;
    ctx.hwnd = NULL;
    for (attempt = 0; attempt < 24 && !ctx.hwnd; attempt++) {
        Sleep(500);
        EnumWindows(FindWinProc, (LPARAM)&ctx);
    }
    if (!ctx.hwnd) {
        GlobalFree(hDrop);
        return FALSE;
    }
    posted = PostMessageA(ctx.hwnd, WM_DROPFILES, (WPARAM)hDrop, 0);
    if (!posted) GlobalFree(hDrop);   /* gdy poszło, właścicielem jest adresat */
    return posted;
}

static ULONG LaunchCompose(const char *url, const MapiMessage *msg, FLAGS flFlags)
{
    char winmail[MAX_PATH + 64];
    static char cmdline[WM_URL_MAX + MAX_PATH + 64];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    int inject = 1;

    if (!WmFindWinMail(winmail, sizeof(winmail)))
        return MAPI_E_FAILURE;

    if (WmSnprintf(cmdline, sizeof(cmdline), "\"%s\" /mailurl:%s", winmail, url)
        >= (int)sizeof(cmdline))
        return MAPI_E_TEXT_TOO_LARGE;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        DETACHED_PROCESS, NULL, NULL, &si, &pi))
        return MAPI_E_FAILURE;

    if (msg && msg->lpFiles && msg->nFileCount &&
        !(flFlags & MAPI_SUPPRESS_ATTACH) &&
        !(msg->flFlags & MAPI_SUPPRESS_ATTACH)) {
        char mode[16];
        if (WmReadRegSz(HKEY_CURRENT_USER, WM_KEY, "AttachMode", mode, sizeof(mode)) &&
            lstrcmpiA(mode, "drop") != 0)
            inject = 0;
        if (inject) WmInjectDropFiles(pi.dwProcessId, msg);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return SUCCESS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* magazyn: wyliczanie plików .eml                                     */
/* ------------------------------------------------------------------ */
#define WM_MAX_EMLS 4096
static char g_emlFiles[WM_MAX_EMLS][MAX_PATH];
static DWORD g_emlCount = 0;

static void CollectEmls(const char *dir, int depth)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char spec[MAX_PATH];

    if (depth > 4 || g_emlCount >= WM_MAX_EMLS) return;

    if (WmSnprintf(spec, sizeof(spec), "%s\\*.eml", dir) < (int)sizeof(spec)) {
        h = FindFirstFileA(spec, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    g_emlCount < WM_MAX_EMLS &&
                    WmSnprintf(g_emlFiles[g_emlCount], MAX_PATH, "%s\\%s",
                               dir, fd.cFileName) < MAX_PATH)
                    g_emlCount++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    if (depth >= 4) return;
    if (WmSnprintf(spec, sizeof(spec), "%s\\*", dir) < (int)sizeof(spec)) {
        h = FindFirstFileA(spec, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    fd.cFileName[0] != '.' && lstrcmpiA(fd.cFileName, "Backup") != 0) {
                    char sub[MAX_PATH];
                    if (WmSnprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName)
                        < (int)sizeof(sub))
                        CollectEmls(sub, depth + 1);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
}

static int SortCmp(const void *a, const void *b)
{
    return lstrcmpiA((const char *)a, (const char *)b);
}

static void RefreshStoreList(void)
{
    char root[MAX_PATH];
    g_emlCount = 0;
    if (!WmGetStoreRoot(root, sizeof(root))) return;
    CollectEmls(root, 0);
    if (g_emlCount > 1) qsort(g_emlFiles, g_emlCount, MAX_PATH, SortCmp);
}

static DWORD FnvHash(const char *s)
{
    DWORD h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* ID wiadomości: "WM1:XXXXXXXX" — hasz FNV nazwy pliku */
static BOOL MakeMsgId(const char *path, char *id, DWORD cbId)
{
    return WmSnprintf(id, cbId, "WM1:%08X", FnvHash(path)) < (int)cbId;
}

static BOOL FindByMsgId(const char *id, char *out, DWORD cbOut)
{
    DWORD want, i;
    if (_strnicmp(id, "WM1:", 4) != 0) return FALSE;
    want = (DWORD)strtoul(id + 4, NULL, 16);
    RefreshStoreList();
    for (i = 0; i < g_emlCount; i++) {
        if (FnvHash(g_emlFiles[i]) == want)
            return WmSnprintf(out, cbOut, "%s", g_emlFiles[i]) < (int)cbOut;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* analiza .eml                                                        */
/* ------------------------------------------------------------------ */
static char *ReadWholeFile(const char *path)
{
    HANDLE h;
    DWORD size, got;
    char *buf;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > (32u << 20)) {
        CloseHandle(h);
        return NULL;
    }
    buf = (char *)WmAlloc((SIZE_T)size + 1);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }
    if (!ReadFile(h, buf, size, &got, NULL)) {
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    buf[got] = '\0';
    return buf;
}

/* wartość nagłówka "Name:" z początku wiadomości (jedna linia, bez rozwijania) */
static char *HeaderDup(const char *msg, const char *header)
{
    const char *line = msg;
    SIZE_T hlen = strlen(header);
    while (line && *line) {
        const char *eol = strstr(line, "\r\n");
        if (!eol) eol = line + strlen(line);
        if (_strnicmp(line, header, hlen) == 0 && line[hlen] == ':') {
            const char *v = line + hlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            if (v < eol) return WmStrDupN(v, (SIZE_T)(eol - v));
            return WmStrDup("");
        }
        if (!eol[0]) break;
        line = eol + 2;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* eksportowane funkcje Simple MAPI                                    */
/* ------------------------------------------------------------------ */

ULONG FAR PASCAL MAPILogon(ULONG_PTR ulUIParam, LPSTR lpszProfileName,
                           LPSTR lpszPassword, FLAGS flFlags, ULONG ulReserved,
                           LPLHANDLE lplhSession)
{
    (void)ulUIParam; (void)lpszProfileName; (void)lpszPassword;
    (void)flFlags; (void)ulReserved;
    WmInit();
    if (!lplhSession) return MAPI_E_INVALID_PARAMETER;
    /* Windows Mail nie ma profili — logowanie zawsze się udaje */
    *lplhSession = (LHANDLE)0x574D4D41;   /* 'WMMA' */
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPILogoff(LHANDLE lhSession, ULONG_PTR ulUIParam,
                            FLAGS flFlags, ULONG ulReserved)
{
    (void)lhSession; (void)ulUIParam; (void)flFlags; (void)ulReserved;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPISendMail(LHANDLE lhSession, ULONG_PTR ulUIParam,
                              lpMapiMessage lpMessage, FLAGS flFlags,
                              ULONG ulReserved)
{
    char url[WM_URL_MAX];
    ULONG rc;

    (void)lhSession; (void)ulUIParam; (void)ulReserved;
    WmInit();
    if (!lpMessage) return MAPI_E_INVALID_PARAMETER;
    if (lpMessage->nRecipCount && !lpMessage->lpRecips)
        return MAPI_E_INVALID_PARAMETER;
    if (lpMessage->nFileCount && !lpMessage->lpFiles)
        return MAPI_E_INVALID_PARAMETER;

    rc = BuildMailto(lpMessage, flFlags, url, sizeof(url));
    if (rc != SUCCESS_SUCCESS) return rc;

    return LaunchCompose(url, lpMessage, flFlags);
}

/* dzielenie łańcucha po pojedynczym ograniczniku */
static DWORD SplitByDelim(char *s, char delim, char **out, DWORD maxOut)
{
    DWORD n = 0;
    char *p;
    if (!s) return 0;
    out[n++] = s;
    for (p = s; *p; p++) {
        if (*p == delim && n < maxOut) {
            *p = '\0';
            out[n++] = p + 1;
        }
    }
    return n;
}

ULONG FAR PASCAL MAPISendDocuments(ULONG_PTR ulUIParam, LPSTR lpszDelimChar,
                                   LPSTR lpszFilePaths, LPSTR lpszFileNames,
                                   ULONG ulReserved)
{
    MapiMessage msg;
    MapiFileDesc files[64];
    char *pathParts[64], *nameParts[64];
    char *pathsDup, *namesDup;
    DWORD n, nNames, i;
    char delim = ';';

    (void)ulUIParam; (void)ulReserved;
    WmInit();
    if (lpszDelimChar && lpszDelimChar[0]) delim = lpszDelimChar[0];
    if (!lpszFilePaths || !lpszFilePaths[0]) return MAPI_E_INVALID_PARAMETER;

    pathsDup = WmStrDup(lpszFilePaths);
    namesDup = WmStrDup(lpszFileNames ? lpszFileNames : "");
    if (!pathsDup) return MAPI_E_INSUFFICIENT_MEMORY;
    n = SplitByDelim(pathsDup, delim, pathParts, 64);
    nNames = SplitByDelim(namesDup, delim, nameParts, 64);

    ZeroMemory(&msg, sizeof(msg));
    ZeroMemory(files, sizeof(files));
    for (i = 0; i < n; i++) {
        const char *base;
        files[i].lpszPathName = pathParts[i];
        files[i].lpszFileName = (i < nNames && nameParts[i] && nameParts[i][0])
                                ? nameParts[i] : NULL;
        if (!files[i].lpszFileName) {
            base = strrchr(pathParts[i], '\\');
            files[i].lpszFileName = WmStrDup(base ? base + 1 : pathParts[i]);
        }
    }
    msg.nFileCount = n;
    msg.lpFiles = files;
    msg.lpszSubject = files[0].lpszFileName;
    return MAPISendMail(0, ulUIParam, &msg, MAPI_DIALOG, 0);
}

ULONG FAR PASCAL MAPIFindNext(LHANDLE lhSession, ULONG_PTR ulUIParam,
                              LPSTR lpszMessageType, LPSTR lpszSeedMessageID,
                              FLAGS flFlags, ULONG ulReserved,
                              LPSTR lpszMessageID)
{
    char seedPath[MAX_PATH];
    DWORD start = 0, i;

    (void)lhSession; (void)ulUIParam; (void)lpszMessageType; (void)ulReserved;
    WmInit();
    if (!lpszMessageID) return MAPI_E_INVALID_PARAMETER;
    RefreshStoreList();
    if (!g_emlCount) return MAPI_E_NO_MESSAGES;

    if (lpszSeedMessageID && lpszSeedMessageID[0] &&
        FindByMsgId(lpszSeedMessageID, seedPath, sizeof(seedPath))) {
        for (i = 0; i < g_emlCount; i++)
            if (lstrcmpiA(g_emlFiles[i], seedPath) == 0) { start = i + 1; break; }
    }
    if (start >= g_emlCount) return MAPI_E_NO_MESSAGES;

    if (!MakeMsgId(g_emlFiles[start], lpszMessageID,
                   (flFlags & MAPI_LONG_MSGID) ? 512 : 64))
        return MAPI_E_FAILURE;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIReadMail(LHANDLE lhSession, ULONG_PTR ulUIParam,
                              LPSTR lpszMessageID, FLAGS flFlags,
                              ULONG ulReserved, lpMapiMessage FAR *lppMessage)
{
    char path[MAX_PATH];
    char *raw, *body;
    MapiMessage *msg;
    MapiRecipDesc *recips;
    DWORD nRecips = 0;
    char *hdr;

    (void)lhSession; (void)ulUIParam; (void)ulReserved;
    WmInit();
    if (!lpszMessageID || !lppMessage) return MAPI_E_INVALID_PARAMETER;
    if (!FindByMsgId(lpszMessageID, path, sizeof(path)))
        return MAPI_E_INVALID_MESSAGE;
    raw = ReadWholeFile(path);
    if (!raw) return MAPI_E_INVALID_MESSAGE;

    msg = (MapiMessage *)WmAlloc(sizeof(MapiMessage));
    if (!msg) return MAPI_E_INSUFFICIENT_MEMORY;
    ZeroMemory(msg, sizeof(*msg));
    msg->lpszSubject = HeaderDup(raw, "Subject");
    msg->lpszDateReceived = HeaderDup(raw, "Date");

    /* nadawca -> lpOriginator; To/Cc -> lpRecips */
    hdr = HeaderDup(raw, "From");
    if (hdr) {
        MapiRecipDesc *o = (MapiRecipDesc *)WmAlloc(sizeof(MapiRecipDesc));
        o->flFlags = MAPI_ORIG;
        o->lpszName = hdr;
        o->lpszAddress = hdr;
        msg->lpOriginator = o;
    }
    recips = (MapiRecipDesc *)WmAlloc(sizeof(MapiRecipDesc) * 2);
    hdr = HeaderDup(raw, "To");
    if (hdr) {
        recips[nRecips].flFlags = MAPI_TO;
        recips[nRecips].lpszName = hdr;
        recips[nRecips].lpszAddress = hdr;
        nRecips++;
    }
    hdr = HeaderDup(raw, "Cc");
    if (hdr) {
        recips[nRecips].flFlags = MAPI_CC;
        recips[nRecips].lpszName = hdr;
        recips[nRecips].lpszAddress = hdr;
        nRecips++;
    }
    if (nRecips) {
        msg->lpRecips = recips;
        msg->nRecipCount = nRecips;
    }

    /* treść: wszystko po pierwszej pustej linii */
    body = strstr(raw, "\r\n\r\n");
    if (body) body += 4;
    else {
        body = strstr(raw, "\n\n");
        if (body) body += 2;
    }
    msg->lpszNoteText = WmStrDup(body ? body : "");

    if (flFlags & MAPI_ENVELOPE_ONLY) {
        msg->lpszNoteText = WmStrDup("");
    }
    *lppMessage = msg;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPISaveMail(LHANDLE lhSession, ULONG_PTR ulUIParam,
                              lpMapiMessage lpMessage, FLAGS flFlags,
                              ULONG ulReserved, LPSTR lpszMessageID)
{
    (void)lhSession; (void)ulUIParam; (void)lpMessage; (void)flFlags;
    (void)ulReserved; (void)lpszMessageID;
    /* magazyn Windows Mail jest oparty na bazie danych bez publicznego API
     * zapisu — zapis nie jest obsługiwany */
    return MAPI_E_NOT_SUPPORTED;
}

ULONG FAR PASCAL MAPIDeleteMail(LHANDLE lhSession, ULONG_PTR ulUIParam,
                                LPSTR lpszMessageID, FLAGS flFlags,
                                ULONG ulReserved)
{
    char path[MAX_PATH];
    char root[MAX_PATH], dst[MAX_PATH + 128];
    DWORD allow = 0;
    const char *base;

    (void)lhSession; (void)ulUIParam; (void)flFlags; (void)ulReserved;
    WmInit();
    if (!lpszMessageID) return MAPI_E_INVALID_PARAMETER;
    /* domyślnie brak usuwania — chroni skrzynkę przed utratą maili */
    WmReadRegDword(HKEY_CURRENT_USER, WM_KEY, "AllowDeleteMail", &allow);
    if (!allow) return MAPI_E_NOT_SUPPORTED;
    if (!FindByMsgId(lpszMessageID, path, sizeof(path)))
        return MAPI_E_INVALID_MESSAGE;
    if (!WmGetStoreRoot(root, sizeof(root))) return MAPI_E_FAILURE;
    base = strrchr(path, '\\');
    if (!base) return MAPI_E_FAILURE;
    if (WmSnprintf(dst, sizeof(dst), "%s\\_wmapi_trash\\%s", root, base + 1)
        >= (int)sizeof(dst))
        return MAPI_E_FAILURE;
    {
        char trashDir[MAX_PATH + 32];
        WmSnprintf(trashDir, sizeof(trashDir), "%s\\_wmapi_trash", root);
        CreateDirectoryA(trashDir, NULL);
    }
    /* przeniesienie, nie usuwanie — plik można odzyskać */
    if (!MoveFileA(path, dst)) return MAPI_E_FAILURE;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIFreeBuffer(LPVOID pv)
{
    DWORD i;
    if (!pv) return SUCCESS_SUCCESS;
    WmInit();
    EnterCriticalSection(&g_csAlloc);
    for (i = 0; i < g_allocCount; i++) {
        if (g_allocs[i] == pv) {
            g_allocs[i] = g_allocs[g_allocCount - 1];
            g_allocCount--;
            LeaveCriticalSection(&g_csAlloc);
            HeapFree(GetProcessHeap(), 0, pv);
            return SUCCESS_SUCCESS;
        }
    }
    LeaveCriticalSection(&g_csAlloc);
    HeapFree(GetProcessHeap(), 0, pv);
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIResolveName(LHANDLE lhSession, ULONG_PTR ulUIParam,
                                 LPSTR lpszName, FLAGS flFlags,
                                 ULONG ulReserved, lpMapiRecipDesc FAR *lppRecips)
{
    MapiRecipDesc *r;

    (void)lhSession; (void)ulUIParam; (void)flFlags; (void)ulReserved;
    WmInit();
    if (!lppRecips) return MAPI_E_INVALID_PARAMETER;
    r = (MapiRecipDesc *)WmAlloc(sizeof(MapiRecipDesc));
    if (!r) return MAPI_E_INSUFFICIENT_MEMORY;
    r->flFlags = MAPI_TO;
    r->lpszName = WmStrDup(lpszName ? lpszName : "");
    r->lpszAddress = (lpszName && strchr(lpszName, '@')) ? WmStrDup(lpszName) : NULL;
    *lppRecips = r;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIAddress(LHANDLE lhSession, ULONG_PTR ulUIParam,
                             LPSTR lpszCaption, ULONG nEditFields,
                             LPSTR lpszLabels, ULONG nRecips,
                             lpMapiRecipDesc lpRecips, FLAGS flFlags,
                             ULONG ulReserved, lpMapiRecipDesc FAR *lppNewRecips)
{
    DWORD i;
    MapiRecipDesc *out;

    (void)lhSession; (void)ulUIParam; (void)lpszCaption; (void)nEditFields;
    (void)lpszLabels; (void)ulReserved; (void)flFlags;
    WmInit();
    if (!lppNewRecips) return MAPI_E_INVALID_PARAMETER;
    /* okno wyboru z książki adresowej nie jest dostępne — przekazujemy
     * odbiorców bez zmian */
    out = (MapiRecipDesc *)WmAlloc(sizeof(MapiRecipDesc) * (nRecips ? nRecips : 1));
    if (!out) return MAPI_E_INSUFFICIENT_MEMORY;
    for (i = 0; i < nRecips; i++) {
        out[i] = lpRecips[i];
        out[i].lpszName = lpRecips[i].lpszName ? WmStrDup(lpRecips[i].lpszName) : NULL;
        out[i].lpszAddress = lpRecips[i].lpszAddress ? WmStrDup(lpRecips[i].lpszAddress) : NULL;
        if (lpRecips[i].lpEntryID && lpRecips[i].ulEIDSize) {
            out[i].lpEntryID = (LPBYTE)WmAlloc(lpRecips[i].ulEIDSize);
            memcpy(out[i].lpEntryID, lpRecips[i].lpEntryID, lpRecips[i].ulEIDSize);
        }
    }
    *lppNewRecips = out;
    return SUCCESS_SUCCESS;
}

ULONG FAR PASCAL MAPIDetails(LHANDLE lhSession, ULONG_PTR ulUIParam,
                             lpMapiRecipDesc lpRecips, FLAGS flFlags,
                             ULONG ulReserved)
{
    (void)lhSession; (void)ulUIParam; (void)lpRecips; (void)flFlags; (void)ulReserved;
    return MAPI_E_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ */
/* funkcje pomocnicze patcha — diagnostyka i testy (nie należą do MAPI) */
/* ------------------------------------------------------------------ */

ULONG FAR PASCAL WmMapiGetVersion(char *out, DWORD cbOut)
{
    if (!out || !cbOut) return MAPI_E_INVALID_PARAMETER;
    WmSnprintf(out, cbOut, "WinMailMAPI 1.0");
    return SUCCESS_SUCCESS;
}

/* zbuduj adres mailto: bez uruchamiania Windows Mail (testy / podgląd) */
ULONG FAR PASCAL WmMapiPreviewUrl(lpMapiMessage lpMessage, FLAGS flFlags,
                                  char *out, DWORD cbOut)
{
    WmInit();
    if (!lpMessage || !out || !cbOut) return MAPI_E_INVALID_PARAMETER;
    return BuildMailto(lpMessage, flFlags, out, cbOut);
}

/* zbiorcza diagnostyka: WinMail.exe, magazyn, konfiguracja */
ULONG FAR PASCAL WmMapiDiag(char *out, DWORD cbOut)
{
    char winmail[MAX_PATH + 64];
    char root[MAX_PATH];
    char mode[16];
    int n;

    WmInit();
    if (!out || !cbOut) return MAPI_E_INVALID_PARAMETER;
    n = WmSnprintf(out, cbOut,
        "WinMailMAPI.dll 1.0 (Simple MAPI -> Windows Mail)\r\n");
    if (WmFindWinMail(winmail, sizeof(winmail)))
        n += WmSnprintf(out + n, cbOut - n, "WinMail.exe: %s\r\n", winmail);
    else
        n += WmSnprintf(out + n, cbOut - n, "WinMail.exe: NIE ZNALEZIONO\r\n");
    if (WmGetStoreRoot(root, sizeof(root))) {
        n += WmSnprintf(out + n, cbOut - n, "Magazyn: %s\r\n", root);
        RefreshStoreList();
        n += WmSnprintf(out + n, cbOut - n, "Plikow .eml w magazynie: %lu\r\n",
                        (unsigned long)g_emlCount);
    } else {
        n += WmSnprintf(out + n, cbOut - n, "Magazyn: brak (Store Root nieustalony)\r\n");
    }
    if (WmReadRegSz(HKEY_CURRENT_USER, WM_KEY, "AttachMode", mode, sizeof(mode)))
        n += WmSnprintf(out + n, cbOut - n, "AttachMode: %s\r\n", mode);
    else
        n += WmSnprintf(out + n, cbOut - n, "AttachMode: drop (domyslny)\r\n");
    return SUCCESS_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hinstDLL);
    return TRUE;
}

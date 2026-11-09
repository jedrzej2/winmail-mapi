/* wm_mapi.h — definicje Simple MAPI zgodne z mapi.h (Windows SDK).
 * Cel: WinMailMAPI.dll — dostawca Simple MAPI dla Windows Mail (WinMail.exe)
 * na Windows 7/8/10, gdzie Windows Mail nie rejestruje własnego MAPI.
 *
 * Wszystkie funkcje mają umowę CALLING CONVENTION stdcall (FAR PASCAL),
 * ciągi ANSI, tak jak wymaga tego dokumentacja Simple MAPI.
 */
#ifndef WM_MAPI_H
#define WM_MAPI_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long FLAGS;
typedef unsigned long ULONG;
typedef unsigned long FAR *LPULONG;
typedef ULONG LHANDLE, FAR *LPLHANDLE;

/* --- flagi --- */
#define MAPI_LOGON_UI          0x00000001
#define MAPI_NEW_SESSION       0x00000002
#define MAPI_FORCE_DOWNLOAD    0x00000004
#define MAPI_LOGOFF_SHARED     0x00000001
#define MAPI_DIALOG            0x00000008
#define MAPI_UNREAD_ONLY       0x00000020
#define MAPI_LONG_MSGID        0x00004000
#define MAPI_GUARANTEE_FIFO    0x00000100
#define MAPI_ENVELOPE_ONLY     0x00000040
#define MAPI_BODY_AS_FILE      0x00000200
#define MAPI_SUPPRESS_ATTACH   0x00000800
#define MAPI_AB_NOMODIFY       0x00000400

/* --- klasy odbiorców --- */
#define MAPI_ORIG  0
#define MAPI_TO    1
#define MAPI_CC    2
#define MAPI_BCC   3

/* --- kody błędów --- */
#define SUCCESS_SUCCESS                     0
#define MAPI_USER_ABORT                     1
#define MAPI_E_FAILURE                      2
#define MAPI_E_LOGIN_FAILURE                3
#define MAPI_E_DISK_FULL                    4
#define MAPI_E_INSUFFICIENT_MEMORY          5
#define MAPI_E_ACCESS_DENIED                6
#define MAPI_E_TOO_MANY_SESSIONS            8
#define MAPI_E_TOO_MANY_FILES               9
#define MAPI_E_TOO_MANY_RECIPIENTS          10
#define MAPI_E_ATTACHMENT_NOT_FOUND         11
#define MAPI_E_ATTACHMENT_OPEN_FAILURE      12
#define MAPI_E_ATTACHMENT_WRITE_FAILURE     13
#define MAPI_E_UNKNOWN_RECIPIENT            14
#define MAPI_E_BAD_RECIPTYPE                15
#define MAPI_E_NO_MESSAGES                  16
#define MAPI_E_INVALID_MESSAGE              17
#define MAPI_E_TEXT_TOO_LARGE               18
#define MAPI_E_INVALID_SESSION              19
#define MAPI_E_TYPE_NOT_SUPPORTED           20
#define MAPI_E_AMBIGUOUS_RECIPIENT          21
#define MAPI_E_MESSAGE_IN_USE               22
#define MAPI_E_NETWORK_FAILURE              23
#define MAPI_E_INVALID_EDITFIELDS           24
#define MAPI_E_INVALID_RECIPS               25
#define MAPI_E_NOT_SUPPORTED                26
#define MAPI_E_NO_LIBRARY                   27
#define MAPI_E_INVALID_PARAMETER            28

/* --- struktury --- */
typedef struct {
    ULONG  ulReserved;
    ULONG  flFlags;
    ULONG  ulReserved2;
    LPSTR  lpszName;
    LPSTR  lpszAddress;
    ULONG  ulEIDSize;
    LPBYTE lpEntryID;
} MapiRecipDesc, FAR *lpMapiRecipDesc;

typedef struct {
    ULONG  ulReserved;
    ULONG  flFlags;
    ULONG  ulPosition;
    LPSTR  lpszPathName;
    LPSTR  lpszFileName;
    LPVOID lpFileType;
} MapiFileDesc, FAR *lpMapiFileDesc;

typedef struct {
    ULONG            ulReserved;
    LPSTR            lpszSubject;
    LPSTR            lpszNoteText;
    LPSTR            lpszMessageType;
    LPSTR            lpszDateReceived;
    LPSTR            lpszConversationID;
    FLAGS            flFlags;
    lpMapiRecipDesc  lpOriginator;
    ULONG            nRecipCount;
    lpMapiRecipDesc  lpRecips;
    ULONG            nFileCount;
    lpMapiFileDesc   lpFiles;
} MapiMessage, FAR *lpMapiMessage;

/* Nagłówek listy plików WM_DROPFILES (format HDROP).
 * Własna nazwa struktury, by nie kolidować z nagłówkami SDK,
 * układ pól identyczny ze strukturą DROPFILES. */
typedef struct tagWMDropFiles {
    DWORD pFiles;   /* offset listy ścieżek od początku struktury */
    POINT pt;
    BOOL  fNC;
    BOOL  fWide;    /* lista ścieżek w UTF-16 */
} WMDROPFILES;

#ifdef __cplusplus
}
#endif

#endif /* WM_MAPI_H */

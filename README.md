# winmail-mapi — Simple MAPI provider for Windows Mail on Windows 7–10

[🇵🇱 Polski opis: README.pl.md](README.pl.md)

Windows Mail (the Vista successor of Outlook Express) shipped **without**
Simple MAPI support. On Windows 7/8/10 people still run it by copying the
32-bit `Windows Mail` folder from Vista — and then "Send to Mail recipient",
Office's *Send as Attachment*, ERP/fiscal apps and fax software all fail,
because those integrations talk Simple MAPI and Windows Mail has no provider.

**winmail-mapi** is that missing provider: a small, self-contained DLL that
exports the full Simple MAPI set and bridges it to Windows Mail.

```
right-click a file → Send to → Mail recipient  →  Windows Mail compose window
Word/Excel "Send as Attachment" (MAPI)         →  Windows Mail compose window
```

- **Not installed yet?** Grab a signed build from
  [Releases](../../releases) (verify with `SHA256SUMS.txt`), or build from
  source in two minutes (see *Building*).
- Polish Windows users: polska dokumentacja jest w [README.pl.md](README.pl.md).

## What it does

| MAPI function | Implementation |
|---|---|
| `MAPISendMail`, `MAPISendDocuments` | builds a `mailto:` URL (recipients, subject, body — ANSI→UTF-8 conversion for non-ASCII text) and opens the compose window via `WinMail.exe /mailurl:`; attachments are injected into the window with a `WM_DROPFILES` message |
| `MAPIFindNext`, `MAPIReadMail` | read-only enumeration/parsing of `.eml` files in the local Windows Mail store |
| `MAPILogon`, `MAPILogoff` | Windows Mail has no profiles; logon always succeeds |
| `MAPIResolveName`, `MAPIAddress` | pass-through (no address book dialog) |
| `MAPISaveMail`, `MAPIDeleteMail`, `MAPIDetails` | not supported (see *Limitations*) |

The installer never touches your mail store. Before changing anything it
makes a full backup (store copy + registry exports), counts `.eml` files
before/after, and **auto-restores** if the counts differ. The uninstaller
returns the machine to its pre-install state (verified end-to-end).

## Requirements

1. **Windows Mail installed and runnable** (32-bit `WinMail.exe` under
   `C:\Program Files (x86)\Windows Mail`). The installer detects it and aborts
   with a clear message if it's missing. winmail-mapi does **not** include
   Windows Mail binaries — they are Microsoft's copyrighted property; copy
   them from your own Windows Vista installation.
2. **At least one mail account configured inside Windows Mail**
   (Tools → Accounts), otherwise composing works but sending can't.
3. Run `installer\install.cmd` **as administrator** (right-click → Run as
   administrator) for a machine-wide registration. Without elevation it
   registers per-user, which covers 32-bit MAPI apps only.

That's the whole configuration. No default-apps changes, no reboot, no extra
runtimes.

## Security notes

- The released DLL is **not code-signed**, so Windows Defender SmartScreen may
  warn when you run the installer. Before dismissing it, verify the download:
  compare `Get-FileHash` of the zip with `SHA256SUMS.txt` from the release,
  and/or build the DLL yourself from source (CI builds are reproducible, see
  *Building*).
- If you moved the zip across the internet, Windows may mark the files
  (Mark-of-the-Web) and block the `.cmd` scripts: right-click → Properties →
  **Unblock**, or unblock the zip before extracting.
- What the patch does to your system is intentionally tiny and fully
  reversible: it copies two DLLs into `Program Files (x86)\WinMailMAPI`,
  sets `DLLPath` under `HKLM\SOFTWARE\Clients\Mail\Windows Mail` (a shared
  32/64-bit registry key) and sets the per-user default mail client
  (`HKCU\Software\Clients\Mail`). Nothing else — no services, no autostart,
  no scheduled tasks, no background process.

## Limitations (by design, documented honestly)

- **Sending always opens the compose window.** Windows Mail has no silent
  send path; apps get a filled-in compose window and the user clicks Send.
- **Attachments** are delivered via `WM_DROPFILES` marshalling (verified for
  x64→x86, x86→x86 and x64→x64 process combinations). The only combination
  that doesn't carry attachments is a 32-bit MAPI client talking to a
  64-bit Windows Mail — an unusual setup, since Windows Mail for Win7–10 is
  the 32-bit Vista build.
- `MAPISaveMail` is unsupported: the Windows Mail store is a database without
  a public write API. `MAPIDeleteMail` is blocked by default to protect your
  mail (opt-in registry value moves files to a quarantine folder instead of
  deleting). `MAPIDetails` is unsupported.
- Message IDs used by `MAPIFindNext` are filename-derived and may change if
  the store is reorganized.

## Configuration

Registry values under `HKCU\Software\WinMailMAPI` (installer sets the first):

| Value | Meaning |
|---|---|
| `WinMailPath` | path to `WinMail.exe` (set by installer) |
| `AttachMode` | `drop` (default) = `WM_DROPFILES` injection; `url` = mailto `attach` parameter; `off` |
| `AllowDeleteMail` | `1` = `MAPIDeleteMail` moves the file to `_wmapi_trash` inside the store (default: refuse) |

## Building

The build needs no Visual Studio and no Windows SDK — it uses
[`zig cc`](https://ziglang.org/download/) as the C compiler and produces
statically-linked CRT DLLs.

```cmd
tools\build.cmd
```

The toolchain version is pinned: **zig 0.17.1**,
`zig-x86_64-windows-0.17.1.zip`,
SHA-256 `b03f477a1943673e02aca0f570e8030dffd536ce1f45cbc4d3326ec31159562e`
(the script downloads it into `tools\` automatically; set the `ZIG` environment
variable to use your own copy).

Continuous integration (`.github/workflows/build.yml`) builds x86+x64 on
`windows-latest` with the same pinned hash, runs the smoke tests, and attaches
the release zip + `SHA256SUMS.txt` to GitHub Releases on tag pushes.

## Tests

`bin\wmtest86.exe` / `bin\wmtest64.exe` exercise the provider without sending
anything: export resolution via `GetProcAddress`, call-flow (`MAPILogon`,
`MAPIResolveName`, `MAPIAddress`, `MAPIFreeBuffer`), exact `mailto:` URL
construction, and a full `MAPISendMail` round-trip against a fake launcher
that captures the command line. `installer\selftest.cmd` runs the whole set on
a target machine; add `--sendlive` to open a real Windows Mail compose window
with a test attachment (nothing is sent automatically).

## See also

- [jonaski/winmailtest](https://github.com/jonaski/winmailtest) — GUI tester
  for Simple MAPI on Windows.
- [go-mapi](https://github.com/marcfargas/go-mapi) — a different take on the
  same registration mechanism: intercepts *Send to Mail recipient* and
  redirects it to Gmail instead of a local client.
- If MAPI routing ever misbehaves after uninstalling other mail software,
  Microsoft's built-in `fixmapi.exe` resets the MAPI32 stub.

## License

MIT — see [LICENSE](LICENSE). Windows Mail itself is a Microsoft product and
is **not** included or distributed by this project.

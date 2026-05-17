# hisol

`hisol` is short for **HTTPS IPMI serial-over-LAN**.

It's basically what `ipmitool ... sol activate` would be, if it had anything to do with modern security. Or it's what `ssh -t ... "start /system1/console1"` would be IF IT FREAKIN' EXISTED. (It does on some systems, doesn't on others.)

But in more concrete terms, it is a terminal bridge for BMCs that expose their browser SOL console through an HTTPS/WebSocket web UI. The only supported provider is **MegaRAC**, which covers **ASRockRack** and **ASUS** boards tested so far.

The goal is narrow: speak HTTPS, verify TLS certificates, authenticate through the BMC web session, and bridge the resulting WebSocket terminal stream to local stdin/stdout.

## Scope

Supported today:

- MegaRAC-style HTTPS login at `/api/session`.
- Cookie and CSRF handling for the MegaRAC WebSocket SOL path.
- WebSocket terminal bridge at `/sol`.
- Windows console input handling for interactive terminal use.
- Raw stdin/stdout bridge mode for piping or external terminal experiments.
- OS certificate verification on Windows through OpenSSL's Windows store loader.
- Optional `--insecure` mode for early lab debugging or lazy homelab setups.

Out of scope:

- Reimplementing IPMI SOL over UDP 623.
- Wrapping `ipmitool`.
- Supermicro/ATEN Java SOL launchers that download JNLP/JAR files and then speak RMCP+/IPMI SOL directly.

## Build

### Requirements

- Windows.
- Visual Studio C++ tools.
- CMake.
- vcpkg with the repository dependencies installable from `vcpkg.json`.
- Ninja is optional. The build script uses Ninja when present, then NMake, then lets CMake choose a default generator.

### Command

```powershell
.\scripts\build_windows.ps1
```

Useful options:

```powershell
.\scripts\build_windows.ps1 -BuildType Release
.\scripts\build_windows.ps1 -VcpkgRoot C:\src\vcpkg
.\scripts\build_windows.ps1 -VcVarsAll "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
```

The default debug binary is written under:

```text
build\windows-debug\hisol.exe
```

## Usage

### MegaRAC SOL

`sol` is the generic command name. `megarac` is the explicit provider name. They currently do the same thing:

```powershell
.\build\windows-debug\hisol.exe sol --username marton https://ipmi-host.lan
.\build\windows-debug\hisol.exe megarac --username marton https://ipmi-host.lan
```

If no password is provided through `--password`, `--password-env`, or `HISOL_PASSWORD`, `hisol` prompts interactively with masked input.

Credentials can also come from the environment:

```powershell
$env:HISOL_USERNAME = "marton"
$env:HISOL_PASSWORD = "secret"
.\build\windows-debug\hisol.exe megarac https://ipmi-host.lan
```

Or from a named password variable:

```powershell
$env:MY_BMC_PASSWORD = "secret"
.\build\windows-debug\hisol.exe megarac --username marton --password-env MY_BMC_PASSWORD https://ipmi-host.lan
```

The interactive bridge disables local console echo and sends Ctrl+C to the remote console. Ctrl+] is the local escape key.

On remote disconnect, interactive mode prints a timestamped status and waits:

```text
Press Enter to reconnect or Ctrl+] to exit.
```

Raw mode does not install console handling or reconnect prompts:

```powershell
.\build\windows-debug\hisol.exe megarac --raw --username marton https://ipmi-host.lan
```

Use raw mode to capture bootlogs or connect the `hisol` stdin/stdout to something else.

### Fetch Test

The `get` command is a small HTTPS test helper:

```powershell
.\build\windows-debug\hisol.exe get --include https://ipmi-host.lan/
```

It advertises gzip support and decompresses gzip responses before writing the body.

## Diagnostics

Use `--verbose` to log the HTTP setup requests/responses and WebSocket handshake to stderr:

```powershell
.\build\windows-debug\hisol.exe megarac --verbose --username marton https://ipmi-host.lan
```

Sensitive headers and form fields are redacted in verbose logs.

Use `--debug-frames` to log WebSocket frame sizes while bridged:

```powershell
.\build\windows-debug\hisol.exe megarac --debug-frames --username marton https://ipmi-host.lan
```

## TLS

By default, `hisol` verifies the server certificate and hostname. On Windows, OpenSSL is configured to use the OS certificate store, so a private root CA trusted by Windows should work for lab BMC certificates.

For temporary lab debugging only:

```powershell
.\build\windows-debug\hisol.exe megarac --insecure --username marton https://ipmi-host.lan
```

## Provider Notes

### MegaRAC

The current implementation follows this flow:

1. `POST /api/session` with `username` and `password`.
2. Store session cookies from `Set-Cookie`.
3. Read the JSON `CSRFToken`.
4. Add the browser-compatible `__Host-garc` cookie.
5. `POST /api/sol/solcfg`.
6. Open `wss://host/sol` with the session cookies and bridge the terminal stream.

This is known to fit the ASRockRack and ASUS MegaRAC web UI behavior seen so far.

MegaRAC exposes the browser SOL console through HTTPS/WebSocket, but the BMC SSH services tested so far do not offer a serial console option.

### Supermicro / ATEN

The Supermicro path investigated so far is not an HTTPS/WebSocket SOL console. The web UI returns a JNLP launcher, downloads a Pack200-compressed Java JAR, and the Java app speaks IPMI SOL over UDP 623 using the supplied credentials.

Hilariously, the .jnlp file contains an AES-encrypted copy of the user's credentials, the key for which is hardcoded into the JAR.

That is effectively the same as `ipmitool -I lanplus ... sol activate` but somehow even worse, so it is out of scope for this project.

Supermicro/ATEN BMCs may still expose serial console access through the BMC's SSH service; the systems I tested do.

## Terminal Behavior

Interactive mode is a bridge, not a full terminal emulator. The real terminal emulator is still Windows Terminal, conhost, or whatever is hosting `hisol`.

`hisol` handles:

- Local echo suppression.
- Raw-ish key input.
- Arrow keys, navigation keys, and function keys.
- Ctrl+] as a local escape.
- Ctrl+C passthrough to the remote console.
- A warning if the visible console is smaller than 80x25.
- A light terminal reset on exit.

`hisol` does not currently send terminal resize events to the BMC. If the remote console assumes 80x25, tools such as BIOS setup screens or TUIs may still behave as if they are in an 80x25 terminal.

## TODO

- Add TLS certificate pinning.
- Add tests for URL parsing, cookie handling, gzip decoding, key mapping, and MegaRAC flow construction.
- Decide where provider code should be split once a second HTTPS/WebSocket SOL vendor needs support.
- Add Linux and macOS support.

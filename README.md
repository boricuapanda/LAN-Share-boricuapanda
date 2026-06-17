<p align="center">
    <img src="src/img/icon.png" alt="LAN Share Icon"/>
</p>

# LAN Share — boricuapanda fork

A continuation of [LAN-Share](https://github.com/abdularis/LAN-Share) by Abdul Aris R. and contributors, maintained by **[boricuapanda](https://github.com/boricuapanda)**. The original project is GPLv3; this repository keeps that license and credits upstream as the source.

**Repository:** [github.com/boricuapanda/LAN-Share-boricuapanda](https://github.com/boricuapanda/LAN-Share-boricuapanda)  
**Upstream:** [abdularis/LAN-Share](https://github.com/abdularis/LAN-Share)  
**Fork version:** 1.2.2+ (settings schema v3)  
**Detailed changelog:** [CHANGES.md](CHANGES.md)

If upstream maintainers want to pull changes back, open an issue here or contact **boricuapanda** on GitHub. We are happy to help upstream in smaller, reviewable pieces.

This fork exists to make **reliable large-file transfers on a LAN** practical — especially multi‑GB AI model weights, checkpoints, and datasets — while staying easy to use on Linux desktops.

---

## Why this fork?

Upstream LAN-Share is a solid Qt LAN file transfer app, but its last tagged release targets an older feature set. This fork adds:

- **SHA256 end-to-end checksum verification** (default on)
- **Resume interrupted downloads** via `.part` files
- **TLS transport** with TOFU certificate pinning and trust management UI
- **Optional parallel transfer streams** for single large files (with safe fallback)
- **Transfer journal** and crash recovery on startup
- **Upload queue**, per-peer download limits, retry/backoff
- **Speed / ETA** in the transfer table
- **File logging** and in-app log viewer
- **Wayland-friendly** non-native file pickers with mount shortcuts
- **Free-space checks** before accepting downloads
- **Protocol hardening** (packet bounds, idle timeouts, typed failure reasons)
- **Automated tests** (transfer protocol + UI smoke)

Both peers should run **this fork** (or a compatible build) to use the new protocol features. Many settings can fall back gracefully when talking to older peers; see [CHANGES.md](CHANGES.md).

---

## Quick start (Linux)

### Build and install locally

```bash
git clone https://github.com/boricuapanda/LAN-Share-boricuapanda.git
cd LAN-Share-boricuapanda
./build-and-install.sh
```

Installs to `~/.local/bin/LANShare` and `~/.local/bin/lanshare`.

Quit any already-running copy, then launch:

```bash
pkill -x LANShare 2>/dev/null || true
lanshare
```

### Run from build tree (without install)

```bash
cd LAN-Share-boricuapanda/build
qmake-qt5 ../src/LANShare.pro CONFIG+=release
make -j"$(nproc)"
./LANShare
```

### Fedora / Qt deps

The build script uses `qmake-qt5` or `qmake-qt6` and installs Qt development packages via `dnf` when needed.

---

## Windows development

To build and launch the app from the terminal for debugging:

```powershell
.\run-windows-app.ps1
```

On this machine the script defaults to the local Qt install at `C:\tmp\Qt\5.15.2\msvc2019_64` and imports the Visual Studio 2022 compiler environment automatically.

To rebuild cleanly before launch:

```powershell
.\run-windows-app.ps1 -Clean
```

If the app is already running from the dev build and Windows locks `LANShare.exe`, stop it automatically:

```powershell
.\run-windows-app.ps1 -Clean -StopExisting
```

To rebuild and stage the app without opening the window:

```powershell
.\run-windows-app.ps1 -Clean -StopExisting -NoLaunch
```

The dev runner also copies the OpenSSL 1.1 runtime DLLs needed by Qt TLS transfers. If transfers fail with `TLS initialization failed`, check the latest log for `Qt SSL support=yes`:

```powershell
Get-Content "$env:LOCALAPPDATA\LANShare\lanshare.log" -Tail 40
```

To build the wizard-style Windows installer later:

```powershell
.\build-windows-installer.ps1 -Clean
```

The installer output is written to `dist/windows/`.

---

## Tests

```bash
# Protocol / transfer integration tests
./scripts/run-transfer-tests.sh

# UI smoke tests (settings, dialogs, main window)
./scripts/run-ui-tests.sh
```

CI runs both suites on push/PR (see `.github/workflows/`).

---

## Usage

1. Connect sender and receiver to the same LAN.
2. Launch LAN Share on both machines.
3. On the sender: **Send** → pick files or folders → choose receiver → transfer.
4. On the receiver: downloads land in the configured folder (default `~/LANShareDownloads`).

**Settings → General** — checksum, resume, TLS, auth token, journal, retries, log path.  
**Settings → Network** — ports, buffer size, parallel streams, idle timeout, packet limits.

Logs: `~/.local/share/LANShare/lanshare.log`

---

## Screenshots

![Screenshot 1](screenshot.png)  
![Screenshot 2](screenshot2.png)  
![Screenshot 3](screenshot3.png)

---

## Relationship to upstream

| | Upstream | This fork |
|---|----------|-----------|
| Goal | General LAN file share | Large-file reliability + security |
| Protocol | Original v1 | Extended v2 (backward fallback) |
| Releases | Infrequent tagged releases | Rolling `master` + [CHANGES.md](CHANGES.md) |

We are **not** replacing upstream. This is a public continuation by **boricuapanda** ([LAN-Share-boricuapanda](https://github.com/boricuapanda/LAN-Share-boricuapanda)). If the original authors want to merge ideas or collaborate, reach out via GitHub issues on this repository.

---

## License

GPLv3 — same as upstream. See [LICENSE](LICENSE) and [src/text/gpl-3.0.txt](src/text/gpl-3.0.txt).

Original copyright remains with Abdul Aris R. and upstream contributors; fork modifications are offered under the same license.

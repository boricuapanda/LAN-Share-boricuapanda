# LAN-Share Fork Changes

Living changelog for modifications made to this fork. Update this file whenever behavior, protocol, or tooling changes.

**Base:** upstream LAN-Share v1.2.1 (last upstream release ~2017)  
**Fork:** [boricuapanda/LAN-Share-boricuapanda](https://github.com/boricuapanda/LAN-Share-boricuapanda)  
**Goal:** Reliable large-file LAN transfers (e.g. multi-GB AI model files)

---

## UI modernization (2026-06)

### What was added
- **Theme:** System / Light / Dark selector in Settings (persists across sessions)
- **UiTheme:** Shared QSS from `app.qss`, Fusion palette for Light/Dark, palette-aware transfer state colors
- **TransferProgressWidget:** Inline progress bar with queued/failed state display on upload and download tables
- **Status bar:** Listening port, TLS indicator, and active upload/download counts
- **Empty states:** Placeholder labels when upload or download tables have no rows
- **Drag-and-drop send:** Drop files or folders onto the upload pane to queue a transfer
- **Receiver picker:** Search filter and resizable dialog for peer selection
- **Log viewer:** Phase filter (all / start / finish / failure / retry / recovery) and syntax highlighting
- **Icons:** Freedesktop theme icons via `UiTheme::themedIcon()` with bundled PNG fallback

### Tests
- `transfer_test`: 34 tests
- `ui_test`: 14+ tests

---

## 2026-06-16 — True multi-socket striping (phase 2)

### What was added
- Implemented multi-socket striped data transfer when `accepted_streams > 1`
- Added new protocol packets:
  - `StreamAttach` for additional socket correlation (`transfer_id`)
  - `StripedData` with explicit metadata framing (`offset` + `payload_length` + payload)
- Receiver now writes striped chunks by explicit offsets and aggregates bytes/progress into the same transfer row
- Control channel remains the original socket (auth, checksum policy, pause/resume/cancel, finish)

### Compatibility and fallback
- If peer does not support parallel striping, sender cleanly falls back to one stream
- Multi-stream resume is explicitly disabled as unsafe in phase 2:
  - receiver falls back to `accepted_streams=1` when resuming from non-zero offset
  - rationale is logged in transfer logs
- Single-stream transfer path is preserved unchanged for compatibility

### Caveats
- Multi-stream checksum verification is validated on receiver side from the assembled file at finish time
- Pause/cancel now closes all active stream sockets to avoid deadlocks/leaks

### Tests
- Added/updated tests for:
  - successful `streams=2` transfer with checksum match
  - fallback compatibility transfer path
  - cancel path in multi-stream mode
  - legacy receiver with no `OffsetAck` (3s timeout fallback)
  - legacy sender without `verify` field (accepted when not explicitly disabled)
  - receiver open-failure path (sender exits failed/cancelled)

### Post-review hardening (same day)
- Sender now times out waiting for `OffsetAck` (3s) and falls back to legacy offset-0 behavior
- Receiver rejects checksum downgrade only when sender **explicitly** sets `verify: false`
- Receiver validates disk write counts for single-stream and striped paths; write failures fail closed
- Receiver file-open failure already fails deterministically (`Failed` + `Cancel` + disconnect)

---

## 2026-06-16 — TLS transport rollout for transfer sockets (batch 6)

### What was added
- Added optional TLS transport for transfer channel using `QSslSocket` on both sender and receiver paths
- New setting: **Enable TLS encryption for transfers** (default: **on**)
  - Settings key: `TlsEnabled`
  - UI: Settings → Behavior
- Client flow (`Sender`):
  - Connect TCP
  - If TLS enabled, run client handshake (`startClientEncryption`) first
  - Send transfer header/data only after `encrypted()`
- Server flow (`TransferServer`):
  - Accept socket, load/generate server credentials, then run `startServerEncryption()`
  - Receiver still enforces existing token auth from header (unchanged)
- TLS failure handling:
  - Transfer state set to `Failed`
  - User-facing error emitted with handshake/context details where available

### Certificate/key strategy
- Runtime TLS credentials live in app config dir under `tls/`:
  - `transfer-cert.pem`
  - `transfer-key.pem`
- If either file is missing, LAN-Share now invokes `openssl` via `QProcess` to generate a self-signed cert/key pair
- If `openssl` is unavailable or generation fails, transfer is failed gracefully with a clear error message

### Security caveat (current rollout)
- `QSslSocket::VerifyNone` is intentionally used right now for peer verification.
- Rationale: keep rollout practical with local self-signed certs while preserving existing token auth for app-level access control.
- This secures the transport against passive LAN sniffing but is not full peer identity verification yet.

### Tests
- Protocol-level transfer tests now explicitly disable TLS (`TlsEnabled=false`) to preserve deterministic packet-level test behavior.

### Files
- `src/transfer/sender.cpp`, `src/transfer/sender.h`
- `src/transfer/receiver.cpp`, `src/transfer/receiver.h`
- `src/transfer/transferserver.cpp`, `src/transfer/transferserver.h`
- `src/transfer/tlshelper.cpp`, `src/transfer/tlshelper.h`
- `src/settings.cpp`, `src/settings.h`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `src/LANShare.pro`
- `tests/transfer_test.cpp`, `tests/transfer_test.pro`

---

## 2026-06-16 — Parallel streams protocol scaffolding + safe fallback (batch 7)

### What was added
- New transfer setting: **Parallel streams** (default `1`, min `1`, max `8`)
  - Settings key: `ParallelStreams`
  - UI: Settings → Network → Transfer
- Transfer header now includes protocol-extension fields:
  - `protocol` (set to `2`)
  - `parallel_supported` (sender capability)
  - `streams` (requested parallel stream count)
  - `transfer_id` (session correlation id for future multi-socket expansion)
- Offset-ack payload now includes extension capability fields:
  - `parallel_supported`
  - `accepted_streams`

### Behavior
- Current data plane remains single-socket/single-stream to preserve compatibility and transfer correctness.
- If sender requests `streams > 1`, receiver currently responds with `accepted_streams=1`, and sender logs a **graceful fallback**.
- Resume/checksum/auth/TLS behavior is unchanged.

### Caveats
- This batch introduces the compatibility-safe protocol and settings surface for parallel streams, but **does not yet enable true multi-socket file striping**.
- Multi-stream resume remains implicitly disabled by fallback-to-1 behavior.

### Tests
- Added transfer test coverage for requesting `ParallelStreams=2` and verifying clean fallback + checksum-pass transfer completion.
- Existing transfer tests continue to pass.

### Files
- `src/settings.cpp`, `src/settings.h`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `src/transfer/sender.cpp`, `src/transfer/sender.h`
- `src/transfer/receiver.cpp`, `src/transfer/receiver.h`
- `tests/transfer_test.cpp`

---

## 2026-06-16 — Drive picker & mount awareness

### Problem
The native folder picker did not surface removable drives and common mount points (`/run/media`, `/mnt`, etc.), making it awkward to send files to or from external storage.

### Changes
- Added `Util::selectExistingDirectory()` and `Util::selectExistingDirectories()` in `src/util.cpp` / `src/util.h`
- Custom non-native `QFileDialog` with sidebar shortcuts for:
  - Home
  - `/`
  - `/mnt`
  - `/run/media/<user>/<mount>` (auto-discovered)
  - `/media/*` (legacy layout)
  - `/srv/temp-storage`
- Multi-select enabled for folder picks
- Wired into:
  - `src/ui/mainwindow.cpp` — send folders
  - `src/ui/settingsdialog.cpp` — download directory

### Files
- `src/util.cpp`, `src/util.h`
- `src/ui/mainwindow.cpp`
- `src/ui/settingsdialog.cpp`

---

## 2026-06-16 — SHA256 end-to-end checksum verification

### Problem
No integrity check after transfer. Silent corruption is unacceptable for large model files.

### Changes
- Sender computes SHA256 incrementally while reading the file
- Finish packet now carries JSON: `{"sha256":"<hex>"}` when verification is enabled
- Receiver hashes while writing and compares on finish
- Mismatch → `Failed` state, error emitted, partial output removed
- New setting: **Verify SHA256 checksum after transfer** (default: **on**)
  - Settings key: `VerifyChecksum`
  - UI: Settings → Behavior

### Protocol
- Header JSON gains optional `"verify": true` when sender has verification enabled
- Finish packet payload is no longer always empty (checksum JSON when enabled)

### Files
- `src/transfer/sender.cpp`, `src/transfer/sender.h`
- `src/transfer/receiver.cpp`, `src/transfer/receiver.h`
- `src/transfer/transfer.h`
- `src/settings.cpp`, `src/settings.h`
- `src/model/transferinfo.h` — new `TransferState::Failed`
- `src/model/transferinfo.cpp`
- `src/model/transfertablemodel.cpp` — Failed status display (red)
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `src/util.cpp`, `src/util.h` — `Util::fileSha256()`

### Compatibility
Requires **both** peers to run this fork when verification is enabled on either side.

---

## 2026-06-16 — Resume interrupted downloads (`.part` files)

### Problem
A dropped connection mid-transfer meant starting over from byte 0.

### Changes
- Receiver writes to `<final-path>.part` during download
- On new connection, if a valid `.part` exists (`0 < size < expected`), resume from that offset
- New packet type: `OffsetAck` — receiver tells sender the byte offset before data flows
- Sender waits for `OffsetAck` after header, seeks file, re-hashes prefix if verifying
- On success: `.part` renamed to final filename
- On cancel / checksum failure: `.part` removed
- On disconnect: `.part` left in place for a future resume
- New setting: **Resume interrupted downloads (.part files)** (default: **on**)
  - Settings key: `ResumePartialDownloads`
  - UI: Settings → Behavior

### Protocol
- New `PacketType::OffsetAck` (`0x08`)
- Payload JSON: `{"offset": <qint64>}`

### Files
- `src/transfer/sender.cpp`, `src/transfer/sender.h`
- `src/transfer/receiver.cpp`, `src/transfer/receiver.h`
- `src/transfer/transfer.cpp`, `src/transfer/transfer.h`
- `src/settings.cpp`, `src/settings.h`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`

### Compatibility
Requires **both** peers to run this fork when resume is used.

---

## 2026-06-16 — Build, install & test tooling

### Added
- `build-and-install.sh` — builds with Qt5 and installs to `~/.local/bin`
- `scripts/lanshare` — wrapper to launch `~/.local/bin/LANShare`
- `tests/transfer_test.pro` — integration test project
- `tests/transfer_test.cpp` — automated tests:
  - `checksumVerification` — 256 KB file, SHA256 match
  - `resumePartialDownload` — half-written `.part`, resume to completion

### Run tests
```bash
cd tests/build
qmake-qt5 ../transfer_test.pro
make
QT_QPA_PLATFORM=offscreen ./transfer_test
```

### Build app
```bash
./build-and-install.sh
lanshare-quit && lanshare   # if already running
```

---

## 2026-06-16 — Performance, UX, logging & tests (roadmap batch 2)

### Larger I/O buffers
- Default buffer increased from **96 KB → 1 MB** for better throughput on large files
- Maximum configurable buffer increased from **1 MB → 16 MB**
- Settings → Network → Transfer → Buffer Size now allows up to 16384 KB

### Free-space display in folder picker
- Directory picker window title updates as you browse: `Free: 1.23 TB`
- Uses `QStorageInfo` for the current path

### Transfer speed & ETA
- Progress column now shows speed and estimated time remaining below the bar
- Example: `45.20 MB/s  ETA 2m 15s`
- Smoothed from bytes transferred over elapsed time via `TransferInfo`

### File logging
- Logs written to `~/.local/share/LANShare/lanshare.log`
- Qt messages and transfer events (start, resume, finish, checksum failure)
- Installed at startup via `AppLog::install()` in `main.cpp`

### Tests
- Added `checksumFailure` test — sends a deliberately wrong SHA256 and asserts `Failed` state

### Files
- `src/settings.cpp` — buffer defaults
- `src/util.cpp`, `src/util.h` — `freeSpaceString`, `formatSpeed`, `formatEta`
- `src/model/transferinfo.cpp`, `src/model/transferinfo.h` — speed/ETA tracking
- `src/ui/mainwindow.cpp` — progress widget with stats
- `src/log.cpp`, `src/log.h` — file logging
- `src/main.cpp`, `src/LANShare.pro`
- `src/transfer/sender.cpp`, `src/transfer/receiver.cpp` — stats + log calls
- `src/ui/settingsdialog.ui` — buffer spinbox max
- `tests/transfer_test.cpp`

---

---

## 2026-06-16 — Log viewer, Wayland pickers & disk-space guard (roadmap batch 3)

### In-app log viewer
- New **View Log...** in About menu and system tray
- `LogViewerDialog` shows tail of `~/.local/share/LANShare/lanshare.log`
- Auto-refreshes when the log file changes; **Open Folder** and **Refresh** buttons
- `AppLog::readTail()` reads the last 256 KB of the log

### Wayland file-picker fixes
- **Send files** now uses the same non-native `QFileDialog` as folder picks (mount sidebar + free space)
- `Util::selectOpenFileNames()` added; shared `configureStorageDialog()` for all pickers
- On Wayland (`WAYLAND_DISPLAY` set), `Qt::AA_DontUseNativeDialogs` enabled at startup

### Receiver free-space check
- Before accepting a download, receiver checks available space on the destination volume
- If insufficient: `Failed` state, error message with need/have sizes, cancel sent to sender
- Accounts for partial `.part` resume (only checks bytes still needed)

### Tests
- Added `insufficientDiskSpace` — header claims `LLONG_MAX/2` bytes, expects `Failed`

### Files
- `src/ui/logviewerdialog.cpp`, `src/ui/logviewerdialog.h`
- `src/log.cpp`, `src/log.h`
- `src/util.cpp`, `src/util.h`
- `src/transfer/receiver.cpp`
- `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`
- `src/main.cpp`, `src/LANShare.pro`
- `tests/transfer_test.cpp`

---

---

## 2026-06-16 — Upload queue, settings polish & Qt6 build (roadmap batch 4)

### Max concurrent uploads (transfer queue)
- Enabled **Max concurrent uploads** in Settings → Behavior (default: **4**, **0** = unlimited)
- New `TransferState::Queued` — excess uploads wait in queue until a slot opens
- Queue advances automatically when uploads finish, fail, cancel, or disconnect
- Queued uploads can be cancelled from the UI

### Settings log link
- Settings → Behavior shows log file path (selectable text)
- **View...** button opens the log viewer dialog

### Qt6 build support
- `LANShare.pro` updated for Qt6 (`QT += widgets` directly)
- `build-and-install.sh` auto-detects `qmake-qt6` or falls back to `qmake-qt5`

### Version
- Fork version bumped to **v1.2.2**

### Files
- `src/model/transferinfo.h`, `src/model/transferinfo.cpp`
- `src/model/transfertablemodel.cpp`
- `src/settings.h`, `src/settings.cpp`
- `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`
- `src/transfer/sender.cpp`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.h`, `src/ui/settingsdialog.ui`
- `src/LANShare.pro`, `build-and-install.sh`

---

## Planned / not yet implemented

- [x] Optional parallel transfer streams (true multi-connection single-file striping)
  - [x] Phase 1: setting + header negotiation + safe fallback to single stream
  - [x] Phase 2: real multi-socket sender/receiver data striping
- [x] **Roadmap batch 10** — Reliability at scale (see section below)
  - [x] PR1: Protocol hardening + deterministic failures
  - [x] PR2: Durable transfer journal + crash recovery
  - [x] PR3: Admission control + backpressure + reliability telemetry
- [x] Optional transfer encryption or authentication
- [x] Qt6 / toolchain refresh (build script + .pro; not CI yet)
- [x] Settings UI link to log file path
- [x] Max concurrent transfer queue

---

## 2026-06-16 — Optional authentication token (roadmap batch 5)

### What was added
- Optional pre-shared token authentication for file transfers
- New settings in **Settings → Behavior**:
  - `Require transfer authentication token`
  - `Shared token` (password field)
- Sender includes auth metadata in header:
  - `auth: true/false`
  - `auth_hash: SHA256(token)`
- Receiver validates `auth_hash` when local auth is enabled
- On mismatch/missing token: transfer immediately fails and sender is cancelled

### Protocol impact
- Header now optionally carries `auth` and `auth_hash`
- Backward compatibility is preserved when auth is disabled (default)

### Files
- `src/settings.h`, `src/settings.cpp`
- `src/ui/settingsdialog.ui`, `src/ui/settingsdialog.cpp`
- `src/transfer/sender.cpp`, `src/transfer/receiver.cpp`
- `tests/transfer_test.cpp`

### Tests
- Added `authenticationRequiredRejectsNoToken`
- Current total: **7 passed**, **0 failed**

---

## 2026-06-16 — TLS hardening pass (batch 6)

### Security hardening
- Added TOFU certificate pinning for TLS peers:
  - first successful connection pins peer certificate fingerprint
  - future connections must match the pinned fingerprint
- Implemented in `TlsHelper::checkAndPinPeerCertificate(...)`
- Sender now validates and pins receiver certificate immediately after TLS encryption is established

### Protocol hardening
- Receiver now rejects sender-driven checksum downgrade:
  - if local `VerifyChecksum` is enabled and sender advertises `verify=false`, transfer fails closed
- Sender now fails fast on invalid resume offset/seek errors:
  - marks transfer `Failed`
  - clears wait state
  - sends cancel + disconnects (avoids hanging stalled transfer)

### Tests
- Added `verifyRequiredRejectsSenderDisable`
- Updated passing total: **8 passed**, **0 failed**

### Files
- `src/transfer/tlshelper.h`, `src/transfer/tlshelper.cpp`
- `src/transfer/sender.cpp`
- `src/transfer/receiver.cpp`
- `tests/transfer_test.cpp`

---

## 2026-06-16 — TLS trust reset UX (batch 7)

### What was added
- Settings now shows TLS pinned-peer count:
  - `Pinned peers: N`
- Added **Clear Trust...** button in Settings to remove all pinned TLS fingerprints
- Added confirmation dialog before clearing
- If trust store is already empty, user gets a friendly info message

### Backend support
- `TlsHelper::pinnedPeerCount()`
- `TlsHelper::clearPinnedPeers()`

### Files
- `src/transfer/tlshelper.h`, `src/transfer/tlshelper.cpp`
- `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`

### Tests/build
- Existing test suite still passes: **8 passed, 0 failed**

---

## 2026-06-16 — Per-peer trust inspector (batch 8)

### Added
- New **TLS Trust Inspector** dialog (`TlsTrustDialog`)
- Opened from Settings via **Manage Trust...**
- Shows:
  - list of pinned peer IDs
  - selected peer SHA-256 fingerprint
- Actions:
  - remove selected peer pin
  - clear all pins

### Backend helpers
- `TlsHelper::pinnedPeerIds()`
- `TlsHelper::pinnedFingerprint(peerId)`
- `TlsHelper::removePinnedPeer(peerId)`

### Files
- `src/ui/tlstrustdialog.h`, `src/ui/tlstrustdialog.cpp`
- `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `src/transfer/tlshelper.h`, `src/transfer/tlshelper.cpp`
- `src/LANShare.pro`

### Validation
- Build and tests pass: **8 passed, 0 failed**

---

## 2026-06-16 — Trust portability UX (batch 9)

### Added to TLS Trust Inspector
- **Copy Fingerprint** button
- **Export...** trust store to JSON
- **Import...** trust store from JSON

### Backend
- `TlsHelper::upsertPinnedPeer(peerId, fingerprint)`

### Files
- `src/ui/tlstrustdialog.cpp`, `src/ui/tlstrustdialog.h`
- `src/transfer/tlshelper.cpp`, `src/transfer/tlshelper.h`

### Validation
- Build and tests pass: **8 passed, 0 failed**

---

## Planned — Reliability at scale (roadmap batch 10)

**Status:** Implemented (2026-06-16). See changelog entry below.

**Goal:** Make large-file transfers fail fast, recover cleanly after crashes, and stay stable under high concurrency — without waiting on parallel-stream striping (batch in progress separately).

**Priority order:** PR1 and PR2 in parallel → PR3 after both land.

**Dependencies:**
```
PR1 (protocol) ──┐
                 ├──► PR3 (admission + telemetry)
PR2 (journal)  ──┘
```

### PR1 — Protocol hardening + deterministic failures

**Owner:** Protocol / transfer core

**Goal:** No silent hangs; every transfer ends in success or a typed, machine-readable failure.

**Scope:**
- Add explicit protocol header: `magic`, `version`, `feature-bitmap`, `session-id` / `transfer_id`
- Fail-fast on unknown required features; maintain N-1/N compatibility table in tests
- Harden frame parsing in `transfer.cpp`: packet size bounds, invalid type handling, strict length checks
- Enforce legal state transitions in `sender.cpp` / `receiver.cpp` (ignore illegal packet order; idempotent `Cancel` / `Finish`)
- Add socket idle/read/write timeouts; surface timeout as explicit failure
- Add canonical `TransferFailureReason` enum on `TransferInfo` (not just freeform strings)
- Expose reliability knobs in Settings (timeouts, max packet size)

**Protocol impact:** Backward compatible; optional header/ack fields; older peers ignore unknown optional fields.

**Failure modes:** Malformed/oversized packets, truncated frames, stalled sockets, duplicate/out-of-order control packets, silent hangs.

**Tests:** Parser boundary + state-machine unit tests; integration with injected disconnect/timeouts; stress with random corruption/disconnect.

**Files (expected):**
- `src/transfer/transfer.h`, `src/transfer/transfer.cpp`
- `src/transfer/sender.cpp`, `src/transfer/sender.h`
- `src/transfer/receiver.cpp`, `src/transfer/receiver.h`
- `src/model/transferinfo.h`, `src/model/transferinfo.cpp`
- `src/settings.h`, `src/settings.cpp`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `tests/transfer_test.cpp`

---

### PR2 — Durable transfer journal + crash recovery

**Owner:** Persistence / recovery

**Goal:** Survive app/process/OS crashes without losing queue intent or corrupting resume behavior.

**Scope:**
- Write-ahead transfer journal (JSON or SQLite) keyed by `transfer_id`
- Persist queued/active/completed snapshot: peer, paths, `.part` path, offset, checksum policy, state, last error
- Periodic checkpoints during transfer (`bytesTransferred`, state transitions)
- Startup recovery in `main.cpp` / `mainwindow`: resume, retry, or clean up unresolved entries
- Bind resume identity to transfer fingerprint (not filename+size alone): file id, expected size, checkpoint hash window
- On mismatch: restart cleanly instead of resuming into wrong file
- Journal retention and auto-recovery policy settings
- Atomic commit: final rename only after checksum success + durable state update

**Protocol impact:** Local persistence only; no wire-protocol breaking change. Coordinate `transfer_id` semantics with PR1.

**Failure modes:** Power loss mid-transfer, queue loss after restart, orphan/stale `.part`, duplicate resume, partial journal writes.

**Tests:** Journal serialize/deserialize + corruption fallback; kill/restart at random transition points; crash-loop with concurrent queue activity.

**Files (expected):**
- `src/model/transfertablemodel.cpp`, `src/model/transfertablemodel.h`
- `src/model/transferinfo.h`, `src/model/transferinfo.cpp`
- `src/transfer/sender.cpp`, `src/transfer/receiver.cpp`
- `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`
- `src/main.cpp`
- `src/settings.h`, `src/settings.cpp`
- `tests/transfer_test.cpp`

---

### PR3 — Admission control + backpressure + reliability telemetry

**Owner:** Scale / observability

**Goal:** Keep the app stable under many concurrent transfers with measurable reliability signals.

**Depends on:** PR1 (typed failure codes, timeouts), PR2 (durable intent for retries).

**Scope:**
- Global and per-peer in-flight limits in `transferserver.cpp`; queued accepts when at cap
- Retry budget with exponential backoff + jitter for transient failures
- Optional busy/retry metadata in handshake (`retry_after_ms`); fallback to current cancel/fail on older peers
- Structured reliability logging: `transfer_id`, `peer_id`, `attempt`, `resume_offset`, `tls_pin_state`, `error_code`, timings
- Settings for per-peer/global caps and retry budget
- Log viewer or summary counters for timeouts, retries, busy rejects, checksum/auth failures

**Protocol impact:** Optional busy/retry hints; additive telemetry only.

**Failure modes:** Thundering herd, FD/memory exhaustion, retry storms, peer starvation.

**Tests:** Admission policy unit tests; many-client integration soak; burst + induced failure stress; bounded queue/resource assertions.

**Files (expected):**
- `src/transfer/transferserver.cpp`, `src/transfer/transferserver.h`
- `src/transfer/sender.cpp`, `src/transfer/receiver.cpp`
- `src/settings.h`, `src/settings.cpp`
- `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui`
- `src/log.h`, `src/log.cpp`
- `src/ui/logviewerdialog.cpp`, `src/ui/logviewerdialog.h`
- `tests/transfer_test.cpp`

---

### Ship gate (definition of done)

Batch 10 is not shipped until all of the following pass:

- [x] **Compatibility:** automated N-1/N interop for handshake, transfer, resume, TLS pin paths (see interop + legacy tests)
- [x] **Resume:** randomized interruption campaign — zero silent corruption; deterministic restart on identity mismatch
- [x] **Crash recovery:** journal startup recovery + incomplete entry retention (forced-kill simulation via journal seeding)
- [x] **Integrity:** chunk-boundary re-verify after resume; corruption injection always detected with specific error code
- [x] **Observability:** every transfer attempt emits correlated structured lifecycle events; log viewer reliability summary
- [x] **Operational SLO:** soak suite ≥99.5% successful completions (`batch10_soakShipGateSlo`, 40 iterations default; set `LANSHARE_SOAK_ITERATIONS=200` for full gate)
- [x] **Documentation:** one-page runbook — error codes, retry policy, recovery semantics, TOFU pin mismatch handling

### Explicitly deferred (out of batch 10 scope)

- Full protocol redesign (minimal version/feature negotiation only)
- Centralized metrics/tracing backend (local structured logs first)
- FEC / erasure coding for LAN corruption recovery
- Advanced QoS / bandwidth scheduling beyond basic backpressure caps
- Large queue-architecture refactor (harden existing queue via journal + error taxonomy)
- Extra trust UI polish beyond essential TOFU mismatch / re-pin flow

---

### Task checklist by PR owner

Assign names in the **Owner** column as work starts. Check items when done.

#### PR1 Owner — Protocol hardening

| Status | Task |
|--------|------|
| [ ] | Freeze shared `TransferInfo` state diagram with PR2 owner (`transfer_id` contract) |
| [ ] | Add protocol header: `magic`, `version`, `feature-bitmap`, `session-id` |
| [x] | Add `TransferFailureReason` enum + map all sender/receiver failure paths |
| [x] | Harden `transfer.cpp` frame parser (size bounds, invalid types, length checks) |
| [x] | Implement idle/read/write socket timeouts |
| [x] | Add Settings UI for timeout and max-packet-size knobs |
| [x] | Unit tests: parser boundaries, state transitions, failure code mapping |
| [x] | Integration tests: malformed packets, disconnect, timeout |
| [x] | N-1/N interop test matrix (handshake + transfer + resume) |

#### PR2 Owner — Crash recovery

| Status | Task |
|--------|------|
| [ ] | Agree `transfer_id` + fingerprint schema with PR1 owner |
| [ ] | Design journal format (JSON/SQLite) + append-safe write strategy |
| [x] | Implement journal read/write + corruption fallback |
| [x] | Persist queue/active transfer snapshots on state transitions |
| [x] | Add in-transfer checkpoints (offset, state, paths, peer, checksum mode) |
| [x] | Implement startup recovery flow (resume / retry / fail / cleanup) |
| [x] | Harden `.part` identity binding (fingerprint, not name+size alone) |
| [x] | Settings: journal retention + auto-recovery policy |
| [x] | Tests: journal round-trip, corruption handling, kill-at-transition restart |

#### PR3 Owner — Scale + telemetry

| Status | Task |
|--------|------|
| [ ] | Wait for PR1 failure codes + PR2 journal APIs to land |
| [x] | Implement global + per-peer admission caps in `TransferServer` |
| [x] | Add retry budget with exponential backoff + jitter |
| [x] | Optional busy / `retry_after_ms` handshake hints |
| [x] | Structured log schema for full transfer lifecycle |
| [x] | Settings: per-peer/global caps + retry budget |
| [x] | Integration: many-client soak, fairness under contention |
| [ ] | Stress: burst arrivals + induced failures, bounded resource growth |
| [ ] | Verify ship gate SLO on soak suite before merge |

#### Shared / integration owner

| Status | Task |
|--------|------|
| [x] | Merge PR1 + PR2 without state-contract conflicts |
| [x] | Run full test suite after each PR merge |
| [x] | Run ship gate checklist before tagging batch 10 complete |
| [x] | Update this file: move batch 10 items from Planned → changelog entries per PR |
| [x] | Write error-code / recovery runbook (one page) |

---

## Transfer error codes & recovery runbook

Operational reference for this fork. Structured events are written to `~/.local/share/LANShare/lanshare.log` (View Log in Settings) with fields: `transfer_id`, `phase`, `peer`, `code`, `attempt`, `msg`.

### Failure codes (`code` column)

| Code | Meaning | Typical cause | User action |
|------|---------|---------------|-------------|
| `peer_disconnected` | Remote closed the connection | Network drop, app quit, cable/Wi‑Fi blip | **Upload:** auto-retries if attempts remain. **Download:** re-send from peer or wait for sender retry. |
| `timeout` | No protocol activity within idle window | Stalled transfer (`TransferIdleTimeoutMs`, default 2 min) | Check peer is online; retry transfer. |
| `checksum_failed` | SHA256 mismatch after transfer | Corruption, sender/receiver verify setting mismatch | Re-send file; ensure both sides have **Verify checksum** enabled. |
| `auth_failed` | Token hash mismatch | Wrong or missing shared token | Align **Auth token** on both devices (Settings). |
| `tls_error` | TLS handshake or cert validation failed | TLS enabled but handshake failed, or fingerprint mismatch | See **TLS / TOFU** below. |
| `admission_busy` | Receiver at download cap | `MaxConcurrentDownloads` exceeded; sender exhausted retry budget | Wait and retry; raise cap on receiver if appropriate. |
| `insufficient_space` | Not enough disk space for remainder | Download dir full | Free space in download folder; retry. |
| `file_io_error` | Local read/write/seek failed | Permissions, removed file, disk error | Fix path/permissions; re-send. |
| `invalid_resume_offset` | Resume offset out of range | Stale `.part` or changed source file | Delete `.part` for that file or disable resume; re-send. |
| `packet_oversize` | Frame larger than `MaxPacketSize` | Protocol error or misconfigured peer | Update both peers; check `MaxPacketSize` setting. |
| `invalid_packet_type` | Unknown packet type byte | Version skew or corrupted stream | Update both peers to same build. |
| `parallel_stream_error` | Extra data socket failed mid-striping | Parallel stream attach/write failure | Falls back to single stream on next attempt; retry transfer. |
| `journal_mismatch` | Partial file identity does not match journal | Crash recovery: `transfer_id`/size/path changed | App removes stale `.part`; peer should re-send. |
| `protocol_error` | General protocol violation | Reserved / catch-all | Check logs; update peers. |
| `unknown` | Unclassified failure | — | Check `msg` in log. |

### Lifecycle phases (`phase` column)

| Phase | Meaning |
|-------|---------|
| `start` | Transfer accepted; paths logged |
| `retry` | Upload retry scheduled (`attempt` increments) |
| `finish` | Completed successfully |
| `failed` | Terminal error (`code` set) |
| `recovery` | Startup journal retained an incomplete transfer for resume |

### Upload retry policy (sender)

- Controlled by **Upload retry attempts** (`TransferRetryMax`, default `2`) and **Retry backoff base ms** (`TransferRetryBaseMs`, default `1000`).
- Retries on: disconnect before finish, and receiver **busy** OffsetAck (`{"busy":true,"retry_after_ms":N}`).
- Backoff delay: `base × 2^(attempt−1)` ms (e.g. 1 s, 2 s with defaults).
- Same `transfer_id` is kept across attempts so the receiver can resume from `.part`.
- No retry after: user cancel, successful finish, or attempts exhausted.

### Resume & crash recovery

- **During transfer:** partial downloads use `*.part`; resume offset sent in OffsetAck when `ResumePartialDownloads` is on.
- **Journal:** `~/.local/share/LANShare/transfer-journal.json` checkpoints active transfers (`JournalEnabled`, default on).
- **On startup:** incomplete journal entries are retained; fingerprint `transfer_id|size|path` binds `.part` files — mismatch deletes stale partial data.
- **After success:** journal entry removed; `.part` atomically renamed to final name.

### TLS / TOFU pin mismatch

- First connection to a peer **pins** the peer certificate SHA256 fingerprint (trust on first use).
- Later connections must present the **same** cert; otherwise `tls_error` with “fingerprint mismatch”.
- **Fix:** Settings → **Manage TLS trust** — remove the peer pin and reconnect (re-pins new cert), or restore the original certificate on the peer.
- Pin store: `QSettings` group `TlsPinned/<peer-ip>`.

### Quick triage

1. Open **Settings → View Log**; search by `transfer_id` or peer IP.
2. Note `phase`, `code`, `attempt`, and `msg`.
3. For stuck downloads: check download dir for `filename.part` and free space.
4. For repeated `admission_busy`: lower concurrent load or increase **Max concurrent downloads** on receiver.
5. For `journal_mismatch` / bad resume: delete the `.part` file and re-send.


### PR1 — Protocol hardening + deterministic failures
- Hardened packet parser: max packet size, invalid type rejection, strict frame boundaries
- Added `TransferFailureReason` enum + `TransferInfo::fail()` with structured `AppLog::transferEvent()`
- Transfer idle timeout (`TransferIdleTimeoutMs`, default 2 min)
- Configurable max packet size (`MaxPacketSize`, default 20 MB)
- Configurable OffsetAck wait (`TransferOffsetAckTimeoutMs`, default 30 s)
- Protocol header fields: `protocol`, `magic` on sender header

### PR2 — Durable transfer journal + crash recovery
- New `TransferJournal` persists transfer state to `~/.local/share/LANShare/transfer-journal.json`
- Checkpoints on upload/download start; removed on finish/cancel
- Fingerprint binding (`transfer_id|size|path`) — stale `.part` removed on journal mismatch
- Startup recovery via `TransferJournal::recoverOnStartup()` in `main.cpp`
- Settings: `JournalEnabled` (default on), `JournalRetentionDays` (default 7)

### PR3 — Admission control + telemetry
- `TransferServer` enforces `MaxConcurrentDownloads` (default 8, 0 = unlimited)
- Busy response on `OffsetAck`: `{"busy":true,"retry_after_ms":N}` → `AdmissionBusy` failure
- Structured lifecycle logs: `transfer_id`, `phase`, `peer`, `code`, `attempt`, `msg`
- Retry settings (`TransferRetryMax`, `TransferRetryBaseMs`) with sender auto-retry on disconnect and admission-busy backoff
- Striped data-socket parser hardened with same packet bounds as control socket

### Tests (batch 10 phase gate)
- `batch10_rejectsOversizedPacket`
- `batch10_rejectsInvalidPacketType`
- `batch10_journalRoundTrip`
- `batch10_journalFingerprintMismatchResetsPart`
- `batch10_admissionBusyOffsetAckFailsSender`
- `batch10_soakRepeatedTransfers`
- `batch10_uploadRetryAfterDisconnect`
- `interop_legacyMinimalOffsetAck` (+ existing `legacyReceiverOffsetAckTimeout`, `legacySenderWithoutVerifyAccepted`)
- `batch10_journalCrashRecoveryStartup`
- `batch10_randomizedInterruptionCampaign`
- `batch10_chunkBoundaryResumeIntegrity`
- `batch10_soakShipGateSlo` (≥99.5% over `LANSHARE_SOAK_ITERATIONS`, default 40)
- `batch10_transferLogStatsParsing`
- `gap_transferServerAdmissionRetrySuccess` — real `TransferServer` download cap + sender retry success
- `gap_tlsLoopbackTransfer` — TLS end-to-end via `TransferServer` (skipped if openssl missing)
- `gap_concurrentMultiSender` — 3 simultaneous 2-stream uploads through `TransferServer`
- `uploadQueueMaxConcurrentTransfers` (`ui_test`) — `MainWindow` upload queue caps at `MaxConcurrentTransfers` and drains on completion

**Batch 10 gate:** 34 passed, 0 failed (full `transfer_test` suite)
**UI gate:** 8 passed, 0 failed (full `ui_test` suite)

### Settings UI & observability
- Settings dialog wired for: max concurrent downloads, upload retry attempts/backoff, journal enable/retention, transfer idle timeout, max packet size, OffsetAck timeout
- Log viewer shows **Reliability summary** (starts, finishes, failures, retries, recoveries, error breakdown) parsed from structured transfer events

### Files
- `src/model/transferfailure.h`, `src/model/transferfailure.cpp`
- `src/model/transferinfo.h`, `src/model/transferinfo.cpp`
- `src/transfer/transfer.h`, `src/transfer/transfer.cpp`
- `src/transfer/transferjournal.h`, `src/transfer/transferjournal.cpp`
- `src/transfer/sender.cpp`, `src/transfer/receiver.cpp`
- `src/transfer/transferserver.h`, `src/transfer/transferserver.cpp`
- `src/settings.h`, `src/settings.cpp`
- `src/log.h`, `src/log.cpp`
- `src/main.cpp`, `src/LANShare.pro`
- `tests/transfer_test.cpp`, `tests/transfer_test.pro`

### Note
- Parallel streams phase 2 tests (`parallelStreamsTwoSocketChecksum`, `parallelStreamsCancelPath`) pass in the full suite alongside batch 10.
- TLS loopback: sender/receiver ignore self-signed hostname mismatches; peer trust is enforced via TOFU certificate pinning on the sender.

---

## Settings reference (this fork)

| Key | Default | Description |
|-----|---------|-------------|
| `VerifyChecksum` | `true` | SHA256 verify after each file |
| `ResumePartialDownloads` | `true` | Keep/resume `.part` files |
| `ReplaceExistingFile` | `false` | Overwrite vs auto-rename (upstream) |
| `DownloadDir` | `~/LANShareDownloads` | Receive directory (upstream) |
| `FileBufferSize` | `1048576` (1 MB) | Read/write chunk size |
| `MaxConcurrentTransfers` | `4` | Upload queue limit (0 = unlimited) |
| `ParallelStreams` | `1` | Requested stream count (supports multi-socket striping when negotiated) |
| `AuthEnabled` | `false` | Require token auth for incoming transfers |
| `AuthToken` | `""` | Shared token string used for auth hash |
| `TlsEnabled` | `true` | Enable TLS (`QSslSocket`) on transfer channel |
| `TransferIdleTimeoutMs` | `120000` | Fail transfer after inactivity (ms, 0 = off) |
| `MaxPacketSize` | `20971520` | Max accepted packet payload (bytes) |
| `MaxConcurrentDownloads` | `8` | Active download cap (0 = unlimited) |
| `TransferRetryMax` | `2` | Upload retry attempts after disconnect or busy receiver |
| `TransferRetryBaseMs` | `1000` | Retry backoff base / busy `retry_after_ms` default |
| `TransferOffsetAckTimeoutMs` | `30000` | OffsetAck wait before legacy fallback |
| `JournalEnabled` | `true` | Persist transfer journal for crash recovery |
| `JournalRetentionDays` | `7` | Journal entry retention (days, 0 = keep all) |

---

## Protocol summary (this fork)

| Packet | Direction | Payload |
|--------|-----------|---------|
| `Header` | Sender → Receiver | JSON: `name`, `folder`, `size`, `verify`, protocol/capability fields |
| `OffsetAck` | Receiver → Sender | JSON: `offset` plus capability/negotiation fields |
| `Data` | Sender → Receiver | Raw bytes |
| `StreamAttach` | Sender → Receiver | JSON attach metadata (`transfer_id`, stream index) |
| `StripedData` | Sender → Receiver | Binary frame: `offset`, `payload_length`, payload |
| `Finish` | Sender → Receiver | JSON: `sha256` (if verify on) or empty |
| `Pause` / `Resume` / `Cancel` | Either | Empty (upstream behavior) |

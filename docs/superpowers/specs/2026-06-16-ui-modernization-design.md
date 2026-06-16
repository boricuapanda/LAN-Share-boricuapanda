# UI Modernization — Full Polish Pass

**Date:** 2026-06-16  
**Status:** Approved (user selected option D — full polish)  
**Scope:** Qt Widgets polish across main window, dialogs, theme, and UX workflows. No QML rewrite. No Qt 6 migration in this batch.

---

## Goals

1. Make LAN Share feel current on modern Linux desktops (GNOME/KDE) while staying native.
2. Surface reliability features already built (upload queue, TLS, journal) in the UI without opening Settings/Log.
3. Keep all existing behavior and test suites green (`transfer_test`, `ui_test`).

## Non-goals

- Qt 6 port
- QML rewrite
- Replacing every PNG with custom SVG art (use theme icons + selective asset updates)
- New transfer protocol features

---

## Architecture

### New module: `src/ui/uitheme.{h,cpp}`

Central theme application:

| Responsibility | Detail |
|----------------|--------|
| `UiTheme::apply(QApplication*)` | Called once from `main.cpp` after `QApplication` construction |
| Theme mode | `System` (default), `Light`, `Dark` — persisted in `Settings` as `UiTheme` key |
| Style | `Fusion` when user picks Light/Dark; System uses platform style |
| Palette | Dark/light custom palettes for forced modes |
| QSS | Load `:/style/app.qss` for spacing, tables, progress bars, section headers |
| Helpers | `UiTheme::stateColor(TransferState, QPalette)` for model/delegate colors |

### New module: `src/ui/transferprogresswidget.{h,cpp}`

Extract `createTransferProgressWidget()` from `mainwindow.cpp` anonymous namespace into a reusable widget:

- Progress bar + speed/ETA label (existing behavior)
- **Queued:** show muted “Waiting in queue…” instead of empty bar
- **Failed:** show failure reason snippet when available
- Palette-aware label colors (no hardcoded `#666`)

### Main window UX

| Feature | Behavior |
|---------|----------|
| Status bar | Port, TLS on/off, active upload/download counts, startup journal recovery one-liner (5s) |
| Empty states | `QStackedWidget` overlay per table pane when `rowCount()==0` |
| Drag-and-drop | Drop files/folders on upload pane → `selectReceiversAndSendTheFiles()` |
| Table polish | Alternating rows, no grid, uniform row height (~52px), stretch filename column |
| Copy fixes | “Transferring”, align `.ui` title with `PROGRAM_NAME` |
| Accessibility | `accessibleName` on icon-only header buttons |

### Settings dialog

- Add **Appearance** group on General tab: theme combo (System / Light / Dark)
- Add helper `QLabel` hints under concurrency/retry spinboxes (“0 = unlimited”)
- “Restore defaults” already exists — ensure it resets `UiTheme`
- Live preview: applying theme on Save re-calls `UiTheme::apply`

### Receiver selector

- Remove fixed 380×300 constraints; default 420×360, resizable
- Search `QLineEdit` filters `DeviceListModel` via `QSortFilterProxyModel`
- Empty label when no peers: “No devices found. Check firewall or click Refresh.”
- List shows `name (ip)` — existing display role, verify formatting

### Log viewer

- Phase filter combo: All / start / finish / failure / retry
- `QSyntaxHighlighter` or `QTextDocument` block formatting for `phase=` tokens
- Palette-based path/stats label colors
- Filter is client-side on loaded tail (no log format change)

### Icons

- Linux: `QIcon::fromTheme()` fallback chain for standard actions (document-open, folder, media-playback-pause, etc.)
- Keep `:/img/` PNGs as fallback when theme icon missing
- New helper `UiTheme::icon(const char* freedesktopName, const char* resourceFallback)`

---

## Settings key

| Key | Default | Values |
|-----|---------|--------|
| `UiTheme` | `system` | `system`, `light`, `dark` |

Add to `settings.h/cpp`, `settingsdialog.cpp`, persist in existing `LANSConfig` file.

---

## Testing strategy

| Area | Test |
|------|------|
| Theme setting round-trip | `ui_test::settingsThemeRoundTrip` |
| Empty state visibility | `ui_test::mainWindowEmptyStateLabels` (smoke: labels exist) |
| Drag-drop | Manual / optional `ui_test` with `QTest::mouseClick` deferred — document in CHANGES |
| Upload queue + progress | Existing `uploadQueueMaxConcurrentTransfers` still passes |
| Full regression | `scripts/run-transfer-tests.sh` + `scripts/run-ui-tests.sh` |

---

## Phased delivery

| Phase | Deliverable | Risk |
|-------|-------------|------|
| **1** | Theme module, QSS, palette state colors, settings theme picker | Low |
| **2** | Progress widget extract, status bar, empty states, table polish, copy fixes | Medium |
| **3** | Drag-and-drop send, receiver search/resize, log filter/highlight | Medium |
| **4** | Theme icons, accessibility names, CHANGES.md | Low |

Each phase leaves the app buildable and test-green.

---

## File map

| Action | Path |
|--------|------|
| Create | `src/ui/uitheme.h`, `src/ui/uitheme.cpp` |
| Create | `src/ui/transferprogresswidget.h`, `src/ui/transferprogresswidget.cpp` |
| Create | `src/style/app.qss` |
| Modify | `src/main.cpp`, `src/settings.h`, `src/settings.cpp` |
| Modify | `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`, `src/ui/mainwindow.ui` |
| Modify | `src/model/transfertablemodel.cpp` |
| Modify | `src/ui/settingsdialog.cpp`, `src/ui/settingsdialog.ui` |
| Modify | `src/ui/receiverselectordialog.cpp`, `src/ui/receiverselectordialog.ui` |
| Modify | `src/ui/logviewerdialog.cpp`, `src/ui/logviewerdialog.h` |
| Modify | `src/LANShare.pro`, `tests/ui_test.pro`, `src/res.qrc` |
| Modify | `tests/ui_test.cpp`, `CHANGES.md` |

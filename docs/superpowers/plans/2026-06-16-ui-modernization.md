# UI Modernization — Full Polish Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modernize LAN Share's Qt Widgets UI with theme support, improved main-window UX (status bar, empty states, drag-drop), dialog polish, and palette-aware transfer display — without changing transfer protocol behavior.

**Architecture:** New `UiTheme` module centralizes Fusion palette/QSS and theme-aware colors; `TransferProgressWidget` replaces inline progress construction; main window gains status bar and stacked empty-state overlays; receiver picker gets proxy filter search; log viewer gets phase filter + syntax coloring.

**Tech Stack:** Qt 5 Widgets, C++17, qmake, existing `Settings` persistence, `ui_test` / `transfer_test`

**Design spec:** `docs/superpowers/specs/2026-06-16-ui-modernization-design.md`

---

## Phase 1 — Theme foundation

### Task 1: Settings `UiTheme` key

**Files:**
- Modify: `src/settings.h`, `src/settings.cpp`

- [ ] **Step 1: Add enum + getter/setter to `settings.h`**

```cpp
enum class UiThemeMode { System, Light, Dark };

UiThemeMode getUiTheme() const;
void setUiTheme(UiThemeMode mode);
```

- [ ] **Step 2: Implement in `settings.cpp`**

Persist as string key `UiTheme` with values `system`/`light`/`dark`, default `system`. Wire into `loadSettings()`, `saveSettings()`, `reset()`.

- [ ] **Step 3: Build app**

Run: `cd src && qmake LANShare.pro && make -j$(nproc)`  
Expected: compiles cleanly.

---

### Task 2: `UiTheme` module + QSS

**Files:**
- Create: `src/ui/uitheme.h`, `src/ui/uitheme.cpp`, `src/style/app.qss`
- Modify: `src/LANShare.pro`, `src/res.qrc`, `src/main.cpp`

- [ ] **Step 1: Create `uitheme.h`**

```cpp
#pragma once
#include <QApplication>
#include <QIcon>
#include <QPalette>
#include "model/transferinfo.h"

class UiTheme {
public:
    static void apply(QApplication* app);
    static QColor stateColor(TransferState state, const QPalette& palette);
    static QIcon themedIcon(const QString& freedesktopName, const QString& resourcePath);
};
```

- [ ] **Step 2: Implement `apply()`**

Read `Settings::instance()->getUiTheme()`:
- `System`: do not force style; still load QSS for spacing/table rules only (no palette override)
- `Light`/`Dark`: `app->setStyle("Fusion")`, apply custom `QPalette`, append mode-specific QSS overrides

- [ ] **Step 3: Implement `stateColor()`**

Map states to palette roles:
- `Queued` → `QPalette::PlaceholderText`
- `Waiting`/`Paused` → `QPalette::ToolTipText` or mid-tone
- `Transfering` → `QPalette::Link`
- `Finish` → `QPalette::Highlight` (adjusted for readability)
- `Failed`/`Cancelled`/`Disconnected` → `QPalette::LinkVisited` or custom red from `QColor("#c0392b")` only when contrast OK

- [ ] **Step 4: Create `src/style/app.qss`**

```css
QMainWindow { }
QTableView { alternate-background-color: palette(alternate-base); gridline-color: transparent; }
QHeaderView::section { font-weight: bold; padding: 4px; }
QProgressBar { border: 1px solid palette(mid); border-radius: 3px; text-align: center; }
QProgressBar::chunk { background-color: palette(highlight); }
QLabel#sectionHeader { font-size: 13px; font-weight: bold; }
QLabel#emptyStateLabel { color: palette(mid); font-size: 12px; }
```

- [ ] **Step 5: Register QSS in `res.qrc`**, add sources to `LANShare.pro`

- [ ] **Step 6: Call in `main.cpp`**

After `QApplication app(argc, argv);` add `UiTheme::apply(&app);`

- [ ] **Step 7: Build and smoke-run**

Run: `cd src && make -j$(nproc) && QT_QPA_PLATFORM=offscreen ./LANShare --help 2>/dev/null || true`  
Expected: app starts without crash (may exit immediately if second instance).

---

### Task 3: Palette-aware transfer table colors

**Files:**
- Modify: `src/model/transfertablemodel.cpp`

- [ ] **Step 1: Replace `getStateColor()` hardcoded QColor literals**

```cpp
#include "ui/uitheme.h"

QColor TransferTableModel::getStateColor(TransferState state) const
{
    QWidget w;
    return UiTheme::stateColor(state, w.palette());
}
```

Avoid QWidget per call — use a static helper that accepts palette from caller, or pass palette from view. Prefer:

```cpp
// uitheme.cpp — no QWidget needed if caller passes palette from table view
```

In `data()` for `Qt::ForegroundRole`, use `UiTheme::stateColor(state, /* palette from index */)`. Since model has no palette, use `QApplication::palette()` as fallback.

- [ ] **Step 2: Fix typo in display string**

`Transfering` → `Transferring` in `getStateString()`.

- [ ] **Step 3: Run transfer tests**

Run: `bash scripts/run-transfer-tests.sh`  
Expected: 34 passed.

---

### Task 4: Settings theme picker + ui_test

**Files:**
- Modify: `src/ui/settingsdialog.ui`, `src/ui/settingsdialog.cpp`
- Modify: `tests/ui_test.cpp`, `tests/ui_test.pro` (if new uitheme.cpp needed in ui_test)

- [ ] **Step 1: Add `QComboBox` `themeComboBox` to General tab** with items System / Light / Dark

- [ ] **Step 2: Wire in `assign()` and `onSaveClicked()`**

Map combo index ↔ `UiThemeMode`. On save, call `UiTheme::apply(qApp)`.

- [ ] **Step 3: Add ui_test**

```cpp
void UiTest::settingsThemeRoundTrip()
{
    Settings* s = Settings::instance();
    SettingsDialog dialog;
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("themeComboBox"));
    QVERIFY(combo);
    combo->setCurrentIndex(2); // Dark
    dialog.findChild<QPushButton*>(QStringLiteral("pushButton"))->click();
    QCOMPARE(s->getUiTheme(), UiThemeMode::Dark);
}
```

- [ ] **Step 4: Run ui tests**

Run: `bash scripts/run-ui-tests.sh`  
Expected: all pass.

---

## Phase 2 — Main window UX

### Task 5: `TransferProgressWidget`

**Files:**
- Create: `src/ui/transferprogresswidget.h`, `src/ui/transferprogresswidget.cpp`
- Modify: `src/ui/mainwindow.cpp`, `src/LANShare.pro`, `tests/ui_test.pro`

- [ ] **Step 1: Create widget class**

Constructor takes `TransferInfo*`. Layout: `QVBoxLayout` with `QProgressBar` + `QLabel` stats. Connect `progressChanged`, `statsChanged`, `stateChanged`.

On `stateChanged`:
- `Queued` → stats label “Waiting in queue…”, progress bar at 0, enabled false
- `Failed` → show `transferFailureReasonName` if set
- else existing speed/ETA logic

- [ ] **Step 2: Replace `createTransferProgressWidget()` in mainwindow.cpp**

Use `new TransferProgressWidget(info)` in `sendFile()` and `onNewReceiverAdded()`.

- [ ] **Step 3: Run full test suites**

Expected: green.

---

### Task 6: Status bar

**Files:**
- Modify: `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`

- [ ] **Step 1: Add `QStatusBar* mStatusBar` and `updateStatusBar()`**

Show: `Listening on port %1 | TLS %2 | Uploads: %3 | Downloads: %4`

- [ ] **Step 2: Connect updates**

Call `updateStatusBar()` from:
- Constructor (after server listen)
- `sendFile`, `onNewReceiverAdded`, `TransferInfo::stateChanged` on both models (use `QTimer::singleShot(0, ...)` debounce if needed)

- [ ] **Step 3: Journal recovery toast**

In constructor, if `TransferJournal` recovery happened (read from `AppLog` or pass summary from `main` via setter), show `statusBar()->showMessage(summary, 5000)`.

Optional: add `MainWindow::showStartupMessage(const QString&)` called from `main.cpp`.

- [ ] **Step 4: Manual smoke** — status text visible.

---

### Task 7: Empty states + table polish

**Files:**
- Modify: `src/ui/mainwindow.ui`, `src/ui/mainwindow.cpp`

- [ ] **Step 1: Wrap each `QTableView` in `QStackedWidget`**

Page 0: table; Page 1: centered `QLabel#emptyStateLabel` with upload/download specific text.

Or implement in code after `setupUi()` by reparenting table into stacked widget.

Upload empty text: `No uploads yet. Drop files here or use Send.`
Download empty text: `No downloads yet. Incoming transfers appear here.`

- [ ] **Step 2: Add `updateEmptyStates()`**

Switch stack index based on `mSenderModel->rowCount()` / `mReceiverModel->rowCount()`. Call after insert/remove/clear.

- [ ] **Step 3: Table polish in constructor**

```cpp
ui->senderTableView->setAlternatingRowColors(true);
ui->senderTableView->setShowGrid(false);
ui->senderTableView->verticalHeader()->setDefaultSectionSize(52);
ui->senderTableView->horizontalHeader()->setStretchLastSection(true);
```

Same for receiver table.

- [ ] **Step 4: Fix `mainwindow.ui` window title** to `LAN Share` (match `PROGRAM_NAME`).

- [ ] **Step 5: Add ui_test smoke for empty labels**

```cpp
void UiTest::mainWindowEmptyStateLabels()
{
    MainWindow w;
    QVERIFY(w.findChild<QLabel*>(QStringLiteral("senderEmptyLabel")));
    QVERIFY(w.findChild<QLabel*>(QStringLiteral("receiverEmptyLabel")));
}
```

Set object names in code/UI.

---

### Task 8: Accessibility on header buttons

**Files:**
- Modify: `src/ui/mainwindow.cpp` (post-setupUi)

- [ ] **Step 1: Set accessible names**

```cpp
ui->pauseSenderBtn->setAccessibleName(tr("Pause upload"));
ui->resumeSenderBtn->setAccessibleName(tr("Resume upload"));
// ... all icon-only sender/receiver header buttons
```

- [ ] **Step 2: Run ui_test** — green.

---

## Phase 3 — Workflow dialogs

### Task 9: Drag-and-drop send

**Files:**
- Modify: `src/ui/mainwindow.h`, `src/ui/mainwindow.cpp`

- [ ] **Step 1: Enable drops on central widget / upload stack**

```cpp
setAcceptDrops(true);
// or upload pane widget only
```

- [ ] **Step 2: Implement `dragEnterEvent` / `dropEvent`**

Accept `text/uri-list` with local file URLs. Collect paths, build `QVector<QPair<QString,QString>>` with `("", path)` for files; for directories use `Util::getInnerDirNameAndFullFilePath`.

Call existing `selectReceiversAndSendTheFiles(pairs)`.

- [ ] **Step 3: `dragEnterEvent`**

Accept if `event->mimeData()->hasUrls()` and at least one local file exists.

- [ ] **Step 4: Document in CHANGES.md** — manual test: drop file → picker → transfer.

---

### Task 10: Receiver selector modernization

**Files:**
- Modify: `src/ui/receiverselectordialog.ui`, `src/ui/receiverselectordialog.cpp`, `src/ui/receiverselectordialog.h`

- [ ] **Step 1: Update `.ui`**

Remove fixed sizePolicy max 380×300. Add `QLineEdit` `searchLineEdit` above list. Add `QLabel` `emptyLabel` (hidden by default).

- [ ] **Step 2: Add `QSortFilterProxyModel`**

Filter on device name + IP string. Connect `searchLineEdit::textChanged` → `setFilterFixedString`.

- [ ] **Step 3: Show empty label when `proxy->rowCount()==0`**

- [ ] **Step 4: ui_test smoke**

```cpp
void UiTest::receiverSelectorSearchField()
{
    DeviceListModel model(new DeviceBroadcaster(nullptr));
    ReceiverSelectorDialog dialog(&model);
    QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("searchLineEdit")));
}
```

Note: DeviceBroadcaster needs parent or stack model — use `DeviceBroadcaster broadcaster; DeviceListModel model(&broadcaster);` in test.

---

### Task 11: Log viewer filter + highlighting

**Files:**
- Modify: `src/ui/logviewerdialog.h`, `src/ui/logviewerdialog.cpp`

- [ ] **Step 1: Add `QComboBox` phase filter** (All, start, finish, failure, retry, recovery)

- [ ] **Step 2: Filter in `reloadLog()`**

Split content by lines; keep lines where `phase=<filter>` or show all.

- [ ] **Step 3: Simple highlighter class `LogSyntaxHighlighter`**

Color `phase=finish` green, `phase=failure` red, `phase=retry` orange using `palette().link()` variants.

- [ ] **Step 4: Replace hardcoded `#666`/`#333` with `palette().color(QPalette::PlaceholderText)` and `QPalette::WindowText`.

- [ ] **Step 5: ui_test** — dialog still opens, combo exists.

---

## Phase 4 — Icons + docs

### Task 12: Themed icons helper

**Files:**
- Modify: `src/ui/uitheme.cpp`, `src/ui/mainwindow.cpp`

- [ ] **Step 1: Implement `UiTheme::themedIcon()`**

```cpp
QIcon UiTheme::themedIcon(const QString& name, const QString& resource)
{
    const QIcon themed = QIcon::fromTheme(name);
  if (!themed.isNull()) return themed;
    return QIcon(resource);
}
```

- [ ] **Step 2: Use in `setupActions()` for pause/resume/cancel/clear/send/settings**

Example: `UiTheme::themedIcon("media-playback-pause", ":/img/pause.png")`

- [ ] **Step 3: Visual check on Fedora** — icons appear (theme or fallback PNG).

---

### Task 13: CHANGES.md + final regression

**Files:**
- Modify: `CHANGES.md`

- [ ] **Step 1: Add section "UI modernization (2026-06)"** listing theme, status bar, empty states, drag-drop, receiver search, log filter, palette colors.

- [ ] **Step 2: Run both test scripts**

```bash
bash scripts/run-transfer-tests.sh
bash scripts/run-ui-tests.sh
```

Expected: transfer 34/34, ui 10+/10+.

- [ ] **Step 3: Commit** (only if user requests).

---

## Self-review (spec coverage)

| Spec item | Task |
|-----------|------|
| UiTheme module | 2 |
| Settings UiTheme key | 1, 4 |
| Palette state colors | 3 |
| TransferProgressWidget | 5 |
| Status bar | 6 |
| Empty states | 7 |
| Drag-drop | 9 |
| Table polish | 7 |
| Copy fixes | 3, 7 |
| Accessibility | 8 |
| Settings hints | 4 (extend with QLabel hints under spinboxes in same task) |
| Receiver search/resize | 10 |
| Log filter/highlight | 11 |
| Themed icons | 12 |
| Tests | 4, 7, 10, 11, 13 |

**Gap fix — settings hints:** In Task 4 Step 1, also add small `QLabel` hint widgets next to `maxTransfersSpinBox`, `maxDownloadsSpinBox`, `transferRetryMaxSpinBox` with text `0 = unlimited` via `.ui` or code.

---

## Execution order

```
Phase 1: Tasks 1 → 2 → 3 → 4
Phase 2: Tasks 5 → 6 → 7 → 8
Phase 3: Tasks 9 → 10 → 11
Phase 4: Tasks 12 → 13
```

Each phase ends with test run before proceeding.

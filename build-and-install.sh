#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${ROOT}/src"
OUT="${HOME}/.local/bin"
BUILD="${ROOT}/build"

if command -v qmake-qt6 >/dev/null 2>&1; then
  QMAKE="qmake-qt6"
  QT_PACKAGES="qt6-qtbase-devel qt6-qttools-devel"
  echo "==> Using Qt 6 ($(qmake-qt6 -query QT_VERSION))"
elif command -v qmake-qt5 >/dev/null 2>&1; then
  QMAKE="qmake-qt5"
  QT_PACKAGES="qt5-qtbase-devel qt5-qttools-devel"
  echo "==> Using Qt 5 ($(qmake-qt5 -query QT_VERSION))"
else
  echo "error: qmake-qt6 or qmake-qt5 not found" >&2
  exit 1
fi

echo "==> Installing build dependencies (needs sudo)"
sudo dnf install -y ${QT_PACKAGES} gcc-c++ make

echo "==> Building LAN Share"
rm -rf "${BUILD}"
mkdir -p "${BUILD}"
cd "${BUILD}"
"${QMAKE}" "${SRC}/LANShare.pro" CONFIG+=release
make -j"$(nproc)"

echo "==> Installing to ${OUT}"
install -m 755 "${BUILD}/LANShare" "${OUT}/LANShare"
install -m 755 "${ROOT}/scripts/lanshare" "${OUT}/lanshare"

echo
echo "Done. Quit old copy and launch:"
echo "  lanshare-quit && lanshare"

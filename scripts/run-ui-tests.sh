#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/tests/ui_build"
TEST_STATE_DIR="${BUILD_DIR}/state"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if command -v qmake-qt6 >/dev/null 2>&1; then
  qmake-qt6 "${ROOT}/tests/ui_test.pro"
elif command -v qmake-qt5 >/dev/null 2>&1; then
  qmake-qt5 "${ROOT}/tests/ui_test.pro"
else
  qmake "${ROOT}/tests/ui_test.pro"
fi

make -j"$(nproc)"

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
rm -rf "${TEST_STATE_DIR}"
mkdir -p "${TEST_STATE_DIR}/config" "${TEST_STATE_DIR}/data"
export XDG_CONFIG_HOME="${TEST_STATE_DIR}/config"
export XDG_DATA_HOME="${TEST_STATE_DIR}/data"
./ui_test "$@"

#!/usr/bin/env bash
# Publish this fork to GitHub (boricuapanda/LAN-Share-boricuapanda).
# Usage: ./scripts/publish-to-github.sh [GITHUB_USERNAME] [REPO_NAME]
#
# Defaults:
#   GITHUB_USERNAME=boricuapanda
#   REPO_NAME=LAN-Share-boricuapanda
#
# Example:
#   ./scripts/publish-to-github.sh
#   ./scripts/publish-to-github.sh boricuapanda LAN-Share-boricuapanda
#
# Requires: git, GitHub repo created (empty, no README), and push access
# (HTTPS + credential helper, or SSH remote).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GITHUB_USER="${1:-boricuapanda}"
REPO_NAME="${2:-LAN-Share-boricuapanda}"
REPO_URL="https://github.com/${GITHUB_USER}/${REPO_NAME}.git"
SSH_URL="git@github.com:${GITHUB_USER}/${REPO_NAME}.git"

cd "${ROOT}"

if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo "error: not a git repository" >&2
  exit 1
fi

# Keep upstream read-only remote
if git remote get-url upstream >/dev/null 2>&1; then
  :
elif git remote get-url origin 2>/dev/null | grep -q 'abdularis/LAN-Share'; then
  echo "==> Renaming origin -> upstream (abdularis/LAN-Share)"
  git remote rename origin upstream
else
  git remote add upstream https://github.com/abdularis/LAN-Share.git 2>/dev/null || true
fi

if git remote get-url origin >/dev/null 2>&1; then
  echo "==> Updating origin -> ${REPO_URL}"
  git remote set-url origin "${REPO_URL}"
else
  echo "==> Adding origin -> ${REPO_URL}"
  git remote add origin "${REPO_URL}"
fi

CURRENT_BRANCH="$(git branch --show-current)"
echo "==> Pushing branch '${CURRENT_BRANCH}' to origin"
if git push -u origin "${CURRENT_BRANCH}"; then
  echo
  echo "Published: https://github.com/${GITHUB_USER}/${REPO_NAME}"
  exit 0
fi

echo "HTTPS push failed; retrying with SSH (${SSH_URL})..."
git remote set-url origin "${SSH_URL}"
git push -u origin "${CURRENT_BRANCH}"
echo
echo "Published: https://github.com/${GITHUB_USER}/${REPO_NAME}"

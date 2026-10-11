#!/usr/bin/env bash
# build-rpm.sh — Build and optionally install the plasma-touchpad RPM.
#
# Usage:
#   bash scripts/build-rpm.sh          # build only
#   bash scripts/build-rpm.sh --install # build then dnf install

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"
SPEC="${REPO_DIR}/plasma-touchpad.spec"

VERSION="$(sed -n 's/^Version:[[:space:]]*//p' "${SPEC}")"
NAME="plasma-touchpad"
TARBALL="kde-plasma-touchpad-${VERSION}.tar.gz"
SOURCES_DIR="${HOME}/rpmbuild/SOURCES"
RPM_OUT="${HOME}/rpmbuild/RPMS/x86_64/${NAME}-${VERSION}-1.$(rpm --eval '%{dist}' | tr -d '.').x86_64.rpm"
RELEASES_DIR="${REPO_DIR}/releases"

mkdir -p "${SOURCES_DIR}"

echo "==> Creating source tarball ${TARBALL}..."
tar czf "${SOURCES_DIR}/${TARBALL}" \
    --transform "s,^\.,kde-plasma-touchpad-${VERSION}," \
    --exclude='./.git' \
    --exclude='./build' \
    --exclude='./CMakeFiles' \
    --exclude='./scripts/build-rpm.sh' \
    -C "${REPO_DIR}" .

echo "==> Building RPM..."
rpmbuild -ba "${SPEC}"

mkdir -p "${RELEASES_DIR}"
cp "${RPM_OUT}" "${RELEASES_DIR}/"

echo ""
echo "==> Built: ${RELEASES_DIR}/$(basename "${RPM_OUT}")"

if [[ "${1:-}" == "--install" ]]; then
    echo "==> Installing..."
    sudo dnf install -y "${RPM_OUT}"
    echo "==> Installed."
fi

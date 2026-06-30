#!/bin/sh
set -eu

VERSION="19.1.5"
FILENAME="LLVM-ET-Arm-${VERSION}-Darwin-AArch64.tar.xz"
URL="https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases/download/release-${VERSION}/${FILENAME}"
INSTALL_DIR="/opt/LLVM-ET-Arm"
TMPDIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

echo "Downloading ${FILENAME}..."
curl -fL --progress-bar -o "${TMPDIR}/${FILENAME}" "${URL}"

echo "Extracting to ${INSTALL_DIR}..."
sudo mkdir -p "${INSTALL_DIR}"
sudo tar -xJf "${TMPDIR}/${FILENAME}" -C "${INSTALL_DIR}" --strip-components=1

echo "Done. Toolchain installed at ${INSTALL_DIR}"

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${ROOT_DIR}/tools/repartition/fire_gki66_repartition.c"
OUT_DIR="${1:-${ROOT_DIR}/out/repartition}"
STAGE="${OUT_DIR}/stage"
BIN="${OUT_DIR}/update-binary"
ZIP="${OUT_DIR}/Fire-GKI66-vendor_boot-8M-repartition.zip"

mkdir -p "${OUT_DIR}" "${STAGE}/META-INF/com/google/android"
rm -rf "${STAGE:?}/"*
mkdir -p "${STAGE}/META-INF/com/google/android"

clang --target=aarch64-linux-gnu -fuse-ld=lld \
    -nostdlib -static -ffreestanding -fno-builtin -fno-stack-protector \
    -Wl,-e,_start -Wl,--build-id=none -Os \
    "${SRC}" -o "${BIN}"

if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip "${BIN}" || true
fi

cp -f "${BIN}" "${STAGE}/META-INF/com/google/android/update-binary"
chmod 0755 "${STAGE}/META-INF/com/google/android/update-binary"
printf '# Fire GKI 6.6 vendor_boot GPT migration\n' \
    > "${STAGE}/META-INF/com/google/android/updater-script"

rm -f "${ZIP}"
(
    cd "${STAGE}"
    zip -q -9 -r "${ZIP}" META-INF
)

sha256sum "${ZIP}"
printf '%s\n' "${ZIP}"

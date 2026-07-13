#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${ROOT_DIR}/tools/repartition/fire_gki66_repartition.c"
WRAPPER="${ROOT_DIR}/tools/repartition/update-binary.sh.in"
OUT_DIR="${1:-${ROOT_DIR}/out/repartition}"
BIN="${OUT_DIR}/fire-gki66-repartition"

mkdir -p "${OUT_DIR}"

clang --target=aarch64-linux-gnu -fuse-ld=lld \
    -nostdlib -static -ffreestanding -fno-builtin -fno-stack-protector \
    -Wl,-e,_start -Wl,--build-id=none -Os \
    "${SRC}" -o "${BIN}"

if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip "${BIN}" || true
fi

build_zip() {
    local mode="$1"
    local title="$2"
    local zip_name="$3"
    local stage="${OUT_DIR}/stage-${mode#--}"
    local zip="${OUT_DIR}/${zip_name}"

    rm -rf "${stage:?}/"
    mkdir -p "${stage}/META-INF/com/google/android" "${stage}/tools"

    sed \
        -e "s|__MODE__|${mode}|g" \
        -e "s|__TITLE__|${title}|g" \
        "${WRAPPER}" > "${stage}/META-INF/com/google/android/update-binary"
    chmod 0755 "${stage}/META-INF/com/google/android/update-binary"
    cp -f "${BIN}" "${stage}/tools/fire-gki66-repartition"
    chmod 0755 "${stage}/tools/fire-gki66-repartition"
    printf '# %s\n' "${title}" \
        > "${stage}/META-INF/com/google/android/updater-script"

    rm -f "${zip}"
    (
        cd "${stage}"
        zip -q -9 -r "${zip}" META-INF tools
    )

    sha256sum "${zip}"
    printf '%s\n' "${zip}"
}

build_zip \
    "--toggle" \
    "Fire vendor_boot GPT auto-toggle" \
    "Fire-GKI66-vendor_boot-toggle.zip"

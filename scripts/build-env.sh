#!/bin/bash
# Source before building: source scripts/build-env.sh
set -e
SORAB_HOME="${SORAB_HOME:-$HOME}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

KERNEL_TARBALL="${SORAB_HOME}/sorabbyngo-patches/../rootfs/dl/linux/linux-6.19.10-sorabbyngo.tar.gz"
# Allow override
KERNEL_TARBALL="${SORABBYNGO_KERNEL_TARBALL_PATH:-$SORAB_HOME/sorabbyngo-patches/../rootfs/dl/linux/linux-6.19.10-sorabbyngo.tar.gz}"
KERNEL_CONFIG="${SORABBYNGO_KERNEL_CONFIG_PATH:-$SORAB_HOME/sorabbyngo-patches/sorabbyngo_full.config}"

[ -f "$KERNEL_TARBALL" ] && echo "[OK]  Kernel tarball: $KERNEL_TARBALL" || \
    echo "[WARN] Kernel tarball not found: $KERNEL_TARBALL"
[ -f "$KERNEL_CONFIG" ]  && echo "[OK]  Kernel config:  $KERNEL_CONFIG"  || \
    echo "[WARN] Kernel config not found:  $KERNEL_CONFIG"

export SORABBYNGO_KERNEL_TARBALL="file://${KERNEL_TARBALL}"
export SORABBYNGO_KERNEL_CONFIG="$KERNEL_CONFIG"
export BR2_EXTERNAL="$REPO_DIR"
export SORABBYNGO_OUTPUT="${SORAB_HOME}/sorabbyngo-output"
mkdir -p "$SORABBYNGO_OUTPUT"

echo ""
echo "[OK]  BR2_EXTERNAL: $BR2_EXTERNAL"
echo "[OK]  Output:       $SORABBYNGO_OUTPUT"
echo ""
echo "Ready. Run:"
echo "  make -C ~/buildroot O=\$SORABBYNGO_OUTPUT BR2_EXTERNAL=\$BR2_EXTERNAL sorabbyngo_x86_64_defconfig"
echo "  make -C ~/buildroot O=\$SORABBYNGO_OUTPUT -j\$(nproc)"

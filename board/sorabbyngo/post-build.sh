#!/bin/bash
set -e
echo "[sorabbyngo] Post-build: setting up base system..."

# Identity
echo "Sorabbyngo OS 1.0 — Rhythm of Digital Life" > ${TARGET_DIR}/etc/issue
echo "sorabbyngo" > ${TARGET_DIR}/etc/hostname

# Directory layout
mkdir -p ${TARGET_DIR}/var/lib/sorabbyngo/{nats,audit,identity,byngonet}
mkdir -p ${TARGET_DIR}/var/log/sorabbyngo
mkdir -p ${TARGET_DIR}/etc/sorabbyngo
mkdir -p ${TARGET_DIR}/opt/sorabbyngo/{bin,lib,sentinel,chameha}
mkdir -p ${TARGET_DIR}/run/sorabbyngo

# Filesystem permissions
chmod 750 ${TARGET_DIR}/var/lib/sorabbyngo
chmod 750 ${TARGET_DIR}/var/log/sorabbyngo
chmod 755 ${TARGET_DIR}/opt/sorabbyngo/bin

# Install kernel modules
KV=$(cat $BUILD_DIR/linux-custom/include/config/kernel.release \
     2>/dev/null || echo "6.19.10-sorabbyngo")
MDIR="${TARGET_DIR}/lib/modules/${KV}/kernel/drivers/sorabbyngo"
mkdir -p "${MDIR}/byngonet"

# NodeID module
if cp $BUILD_DIR/linux-custom/drivers/sorabbyngo/sorabbyngo_nodeid.ko \
      "${MDIR}/" 2>/dev/null; then
    echo "[sorabbyngo] nodeid.ko installed"
else
    echo "[sorabbyngo] WARN: nodeid.ko not found (build may not have run)"
fi

# Byngonet module — now byngonet.ko (identity + syncookie combined)
if cp $BUILD_DIR/linux-custom/drivers/sorabbyngo/byngonet/byngonet.ko \
      "${MDIR}/byngonet/" 2>/dev/null; then
    echo "[sorabbyngo] byngonet.ko installed (identity + syncookie)"
else
    echo "[sorabbyngo] WARN: byngonet.ko not found"
fi

# Run depmod so modprobe works
depmod -b ${TARGET_DIR} ${KV} 2>/dev/null && \
    echo "[sorabbyngo] depmod complete" || \
    echo "[sorabbyngo] WARN: depmod failed"

echo "[sorabbyngo] Post-build complete."

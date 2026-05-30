#!/bin/bash
set -e
echo "[sorabbyngo] Post-build: setting up base system..."
# Remove buildroot default issue
echo "Sorabbyngo OS 1.0 — Rhythm of Digital Life" > ${TARGET_DIR}/etc/issue
echo "sorabbyngo" > ${TARGET_DIR}/etc/hostname
# Create sorabbyngo directories
mkdir -p ${TARGET_DIR}/var/lib/sorabbyngo/nats
mkdir -p ${TARGET_DIR}/var/log/sorabbyngo
mkdir -p ${TARGET_DIR}/etc/sorabbyngo
echo "[sorabbyngo] Post-build complete."

# Install sorabbyngo kernel modules
KV=$(cat $BUILD_DIR/linux-custom/include/config/kernel.release 2>/dev/null || echo "6.19.10-sorabbyngo")
MDIR="${TARGET_DIR}/lib/modules/${KV}/kernel/drivers/sorabbyngo"
mkdir -p "${MDIR}/byngonet"
cp $BUILD_DIR/linux-custom/drivers/sorabbyngo/sorabbyngo_nodeid.ko "${MDIR}/" 2>/dev/null && \
  echo "[sorabbyngo] nodeid.ko installed" || echo "[sorabbyngo] WARN: nodeid.ko not found"
cp $BUILD_DIR/linux-custom/drivers/sorabbyngo/byngonet/byngonet_identity.ko "${MDIR}/byngonet/" 2>/dev/null && \
  echo "[sorabbyngo] byngonet_identity.ko installed" || echo "[sorabbyngo] WARN: byngonet_identity.ko not found"

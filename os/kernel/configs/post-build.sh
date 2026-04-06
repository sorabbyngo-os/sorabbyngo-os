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

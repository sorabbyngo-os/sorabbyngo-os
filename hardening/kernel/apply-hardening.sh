#!/usr/bin/env bash
# ============================================================
# Sorabbyngo OS — Kernel Hardening Application Script
# Run as root at boot time
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="/var/log/sorabbyngo/hardening.log"

log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*" | tee -a "$LOG_FILE"; }

mkdir -p "$(dirname "$LOG_FILE")"

log "Sorabbyngo kernel hardening starting..."

# Apply sysctl settings
if [[ -f "$SCRIPT_DIR/sysctl-hardening.conf" ]]; then
    sysctl -p "$SCRIPT_DIR/sysctl-hardening.conf" >> "$LOG_FILE" 2>&1
    log "sysctl hardening applied"
else
    log "ERROR: sysctl-hardening.conf not found"
    exit 1
fi

# Install module blacklist
if [[ -f "$SCRIPT_DIR/modules-blacklist.conf" ]]; then
    cp "$SCRIPT_DIR/modules-blacklist.conf" /etc/modprobe.d/sorabbyngo-blacklist.conf
    log "Module blacklist installed"
fi

# Remove loaded blacklisted modules
DANGER_MODULES="dccp sctp rds tipc n-hdlc cramfs freevxfs hfs hfsplus vivid"
for mod in $DANGER_MODULES; do
    if lsmod | grep -q "^${mod}"; then
        rmmod "$mod" 2>/dev/null && log "Removed module: $mod" || true
    fi
done

# Set secure permissions on sensitive files
chmod 700 /etc/cron.d /etc/cron.daily /etc/cron.hourly /etc/cron.weekly 2>/dev/null || true
chmod 600 /etc/crontab 2>/dev/null || true
chmod 644 /etc/passwd
chmod 000 /etc/shadow
chmod 640 /etc/shadow
chmod 644 /etc/group
chmod 000 /etc/gshadow
chmod 640 /etc/gshadow
chmod 600 /boot/grub/grub.cfg 2>/dev/null || true

# Secure /tmp and /dev/shm
mount -o remount,noexec,nosuid,nodev /tmp 2>/dev/null || true
mount -o remount,noexec,nosuid,nodev /dev/shm 2>/dev/null || true

# Disable core dumps
echo "* hard core 0" >> /etc/security/limits.conf
echo "* soft core 0" >> /etc/security/limits.conf
ulimit -c 0

log "Sorabbyngo kernel hardening complete"

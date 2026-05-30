#!/usr/bin/env bash
# ============================================================
# Sorabbyngo OS — Hardening Installation Script
# Sets up all hardening layers for the specified environment
# Usage: ./install-hardening.sh [dev|lab|staging|prod|sandbox]
# ============================================================
set -euo pipefail

ENV="${1:-dev}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="/var/log/sorabbyngo/hardening-install.log"

mkdir -p "$(dirname "$LOG_FILE")"

log()  { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] INFO  $*" | tee -a "$LOG_FILE"; }
warn() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] WARN  $*" | tee -a "$LOG_FILE"; }
err()  { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] ERROR $*" | tee -a "$LOG_FILE"; exit 1; }

log "Installing Sorabbyngo hardening for environment: $ENV"

# Validate environment
case "$ENV" in
    dev|lab|staging|prod|sandbox) ;;
    *) err "Unknown environment: $ENV. Use: dev|lab|staging|prod|sandbox" ;;
esac

# --- 1. Kernel hardening ---
log "Applying kernel hardening..."
if [[ "$ENV" != "dev" ]]; then
    bash "$REPO_ROOT/hardening/kernel/apply-hardening.sh"
else
    warn "Skipping full kernel hardening in dev environment"
    sysctl -w kernel.randomize_va_space=2 2>/dev/null || true
fi

# --- 2. Install sysctl config ---
log "Installing sysctl config..."
if [[ "$ENV" == "prod" || "$ENV" == "staging" ]]; then
    cp "$REPO_ROOT/hardening/kernel/sysctl-hardening.conf" \
       /etc/sysctl.d/99-sorabbyngo.conf
    sysctl --system >> "$LOG_FILE" 2>&1
fi

# --- 3. AppArmor profiles ---
log "Installing AppArmor profiles..."
if command -v apparmor_parser &>/dev/null && [[ "$ENV" != "dev" ]]; then
    for profile in "$REPO_ROOT/hardening/apparmor/"*; do
        apparmor_parser -r "$profile" 2>/dev/null && \
            log "AppArmor profile loaded: $(basename "$profile")" || \
            warn "AppArmor profile failed: $(basename "$profile")"
    done
fi

# --- 4. Install Python package ---
log "Installing sorabbyngo_core Python package..."
pip install -e "$REPO_ROOT/packages/python/" \
    --break-system-packages --quiet && \
    log "sorabbyngo_core installed" || \
    warn "sorabbyngo_core install failed"

# --- 5. Build C library ---
log "Building libsorabbyngo..."
if command -v gcc &>/dev/null; then
    cd "$REPO_ROOT/packages/c/libsorabbyngo"
    make clean && make 2>> "$LOG_FILE" && \
        log "libsorabbyngo built successfully" || \
        warn "libsorabbyngo build failed (OpenSSL required)"
    cd "$REPO_ROOT"
else
    warn "gcc not found — skipping C library build"
fi

# --- 6. Set environment ---
log "Setting SORABBYNGO_ENV=$ENV..."
echo "SORABBYNGO_ENV=$ENV" > /etc/sorabbyngo/environment
mkdir -p /etc/sorabbyngo

cat > /etc/sorabbyngo/environment << ENVEOF
SORABBYNGO_ENV=$ENV
SORABBYNGO_VERSION=0.1.0
SORABBYNGO_ROOT=$REPO_ROOT
ENVEOF

log "Hardening installation complete for environment: $ENV"
log "Log: $LOG_FILE"

# Summary
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║   Sorabbyngo Hardening — Installation Complete           ║"
echo "╠══════════════════════════════════════════════════════════╣"
echo "║   Environment : $ENV"
echo "║   Kernel      : $(uname -r)"
echo "║   AppArmor    : $(aa-status --profiled 2>/dev/null || echo 'n/a')"
echo "╚══════════════════════════════════════════════════════════╝"

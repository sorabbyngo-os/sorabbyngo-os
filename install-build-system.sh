#!/bin/bash
# ============================================================
# Sorabbyngo OS — Build System Installer
# Run from inside ~/sorabbyngo-os:
#   cd ~/sorabbyngo-os && bash install-build-system.sh
# ============================================================
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "[sorabbyngo] Installing build system into: $REPO_DIR"

# ── 1. BR2_EXTERNAL tree ──────────────────────────────────────

cat > "$REPO_DIR/external.desc" << 'EOF'
name: SORABBYNGO
desc: Sorabbyngo OS — Africa-first AI Operating System
EOF
echo "[OK] external.desc"

cat > "$REPO_DIR/Config.in" << 'EOF'
menu "Sorabbyngo OS packages"
	source "$BR2_EXTERNAL_SORABBYNGO_PATH/package/sorabbyngo-sentinel/Config.in"
	source "$BR2_EXTERNAL_SORABBYNGO_PATH/package/sorabbyngo-chameha/Config.in"
	source "$BR2_EXTERNAL_SORABBYNGO_PATH/package/sorabbyngo-nats/Config.in"
endmenu
EOF
echo "[OK] Config.in"

cat > "$REPO_DIR/external.mk" << 'EOF'
include $(sort $(wildcard $(BR2_EXTERNAL_SORABBYNGO_PATH)/package/*/*.mk))
EOF
echo "[OK] external.mk"

# ── 2. Package definitions ────────────────────────────────────

mkdir -p "$REPO_DIR/package/sorabbyngo-sentinel"
cat > "$REPO_DIR/package/sorabbyngo-sentinel/Config.in" << 'EOF'
config BR2_PACKAGE_SORABBYNGO_SENTINEL
	bool "sorabbyngo-sentinel"
	depends on BR2_PACKAGE_PYTHON3
	select BR2_PACKAGE_PYTHON_NATS_PY
	help
	  Sorabbyngo Sentinel — central watchdog daemon.
	  Audit chain, NodeID verification, service heartbeat monitor.
EOF

cat > "$REPO_DIR/package/sorabbyngo-sentinel/sorabbyngo-sentinel.mk" << 'EOF'
################################################################################
# sorabbyngo-sentinel
################################################################################
SORABBYNGO_SENTINEL_VERSION     = 1.0.0
SORABBYNGO_SENTINEL_SITE        = $(BR2_EXTERNAL_SORABBYNGO_PATH)/../sentinel
SORABBYNGO_SENTINEL_SITE_METHOD = local

define SORABBYNGO_SENTINEL_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/sorabbyngo/sentinel
	$(INSTALL) -D -m 0755 $(@D)/sentinel_daemon.py \
		$(TARGET_DIR)/opt/sorabbyngo/sentinel/sentinel_daemon.py
	$(INSTALL) -D -m 0755 $(@D)/sentinel_watcher.py \
		$(TARGET_DIR)/opt/sorabbyngo/sentinel/sentinel_watcher.py
endef

$(eval $(generic-package))
EOF
echo "[OK] package/sorabbyngo-sentinel"

mkdir -p "$REPO_DIR/package/sorabbyngo-chameha"
cat > "$REPO_DIR/package/sorabbyngo-chameha/Config.in" << 'EOF'
config BR2_PACKAGE_SORABBYNGO_CHAMEHA
	bool "sorabbyngo-chameha"
	depends on BR2_PACKAGE_PYTHON3
	help
	  Chameha — AI personality and voice interface.
	  Tier 1 local inference via Ollama. English + Swahili.
EOF

cat > "$REPO_DIR/package/sorabbyngo-chameha/sorabbyngo-chameha.mk" << 'EOF'
################################################################################
# sorabbyngo-chameha
################################################################################
SORABBYNGO_CHAMEHA_VERSION     = 1.0.0
SORABBYNGO_CHAMEHA_SITE        = $(BR2_EXTERNAL_SORABBYNGO_PATH)/../board/sorabbyngo/overlay/opt/sorabbyngo/chameha
SORABBYNGO_CHAMEHA_SITE_METHOD = local

define SORABBYNGO_CHAMEHA_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/sorabbyngo/chameha/first-boot
	$(INSTALL) -D -m 0755 $(@D)/first-boot/chameha_firstboot.py \
		$(TARGET_DIR)/opt/sorabbyngo/chameha/first-boot/chameha_firstboot.py
endef

$(eval $(generic-package))
EOF
echo "[OK] package/sorabbyngo-chameha"

mkdir -p "$REPO_DIR/package/sorabbyngo-nats"
cat > "$REPO_DIR/package/sorabbyngo-nats/Config.in" << 'EOF'
config BR2_PACKAGE_SORABBYNGO_NATS
	bool "sorabbyngo-nats"
	help
	  NATS server binary for Sorabbyngo OS.
	  Pre-built amd64 binary from nats.io.
	  All Sorabbyngo services communicate over NATS JetStream.
EOF

cat > "$REPO_DIR/package/sorabbyngo-nats/sorabbyngo-nats.mk" << 'EOF'
################################################################################
# sorabbyngo-nats
################################################################################
SORABBYNGO_NATS_VERSION = 2.10.18
SORABBYNGO_NATS_ARCH    = amd64
SORABBYNGO_NATS_SITE    = https://github.com/nats-io/nats-server/releases/download/v$(SORABBYNGO_NATS_VERSION)
SORABBYNGO_NATS_SOURCE  = nats-server-v$(SORABBYNGO_NATS_VERSION)-linux-$(SORABBYNGO_NATS_ARCH).tar.gz

define SORABBYNGO_NATS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/nats-server-v$(SORABBYNGO_NATS_VERSION)-linux-$(SORABBYNGO_NATS_ARCH)/nats-server \
		$(TARGET_DIR)/usr/bin/nats-server
	mkdir -p $(TARGET_DIR)/etc/sorabbyngo/nats
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_SORABBYNGO_PATH)/../nats/sorabbyngo.conf \
		$(TARGET_DIR)/etc/sorabbyngo/nats/sorabbyngo.conf
endef

$(eval $(generic-package))
EOF
echo "[OK] package/sorabbyngo-nats"

# ── 3. NATS config ────────────────────────────────────────────

mkdir -p "$REPO_DIR/nats"
cat > "$REPO_DIR/nats/sorabbyngo.conf" << 'EOF'
# Sorabbyngo NATS Server Configuration
port: 4222
http_port: 8222
server_name: "sorabbyngo-node"
log_file: "/var/log/sorabbyngo/nats.log"
logtime: true
debug: false
trace: false

jetstream {
    store_dir: "/var/lib/sorabbyngo/nats/jetstream"
    max_memory_store: 256MB
    max_file_store: 1GB
}

max_connections: 128
max_payload: 1MB
max_pending: 32MB
write_deadline: "3s"
EOF
echo "[OK] nats/sorabbyngo.conf"

# ── 4. Init scripts ───────────────────────────────────────────

mkdir -p "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d"

cat > "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S10nats" << 'EOF'
#!/bin/sh
# S10nats — Start NATS server
NATS_BIN="/usr/bin/nats-server"
NATS_CONF="/etc/sorabbyngo/nats/sorabbyngo.conf"
NATS_PID="/var/run/nats-server.pid"
NATS_LOG="/var/log/sorabbyngo/nats.log"
mkdir -p /var/log/sorabbyngo /var/lib/sorabbyngo/nats

case "$1" in
    start)
        echo -n "Starting NATS server... "
        [ ! -x "$NATS_BIN" ] && echo "MISSING" && exit 1
        "$NATS_BIN" -c "$NATS_CONF" -P "$NATS_PID" >> "$NATS_LOG" 2>&1 &
        for i in 1 2 3; do
            sleep 1
            [ -f "$NATS_PID" ] && kill -0 "$(cat $NATS_PID)" 2>/dev/null && \
                echo "OK (pid=$(cat $NATS_PID))" && exit 0
        done
        echo "FAILED — check $NATS_LOG"; exit 1 ;;
    stop)
        echo -n "Stopping NATS... "
        [ -f "$NATS_PID" ] && kill "$(cat $NATS_PID)" 2>/dev/null && rm -f "$NATS_PID"
        echo "OK" ;;
    restart) "$0" stop; sleep 1; "$0" start ;;
    status)
        [ -f "$NATS_PID" ] && kill -0 "$(cat $NATS_PID)" 2>/dev/null && \
            echo "NATS running (pid=$(cat $NATS_PID))" || { echo "not running"; exit 1; } ;;
    *) echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac
EOF
chmod +x "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S10nats"
echo "[OK] init.d/S10nats"

cat > "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S20sentinel" << 'EOF'
#!/bin/sh
# S20sentinel — Start Sorabbyngo Sentinel daemon + watcher
SENTINEL="/opt/sorabbyngo/sentinel/sentinel_daemon.py"
WATCHER="/opt/sorabbyngo/sentinel/sentinel_watcher.py"
PID_DAEMON="/var/run/sorabbyngo-sentinel.pid"
PID_WATCHER="/var/run/sorabbyngo-watcher.pid"
LOG="/var/log/sorabbyngo/sentinel.log"
NATS_URL="nats://localhost:4222"
mkdir -p /var/log/sorabbyngo

case "$1" in
    start)
        echo -n "Starting Sorabbyngo Sentinel... "
        [ ! -f "$SENTINEL" ] && echo "MISSING" && exit 1
        python3 "$SENTINEL" --nats "$NATS_URL" >> "$LOG" 2>&1 &
        echo $! > "$PID_DAEMON"
        python3 "$WATCHER" >> "$LOG" 2>&1 &
        echo $! > "$PID_WATCHER"
        sleep 1
        kill -0 "$(cat $PID_DAEMON)" 2>/dev/null && \
            echo "OK (pid=$(cat $PID_DAEMON))" || { echo "FAILED — check $LOG"; exit 1; } ;;
    stop)
        echo -n "Stopping Sentinel... "
        for f in "$PID_DAEMON" "$PID_WATCHER"; do
            [ -f "$f" ] && kill "$(cat $f)" 2>/dev/null && rm -f "$f"
        done
        echo "OK" ;;
    restart) "$0" stop; sleep 1; "$0" start ;;
    status)
        [ -f "$PID_DAEMON" ] && kill -0 "$(cat $PID_DAEMON)" 2>/dev/null && \
            echo "Sentinel running (pid=$(cat $PID_DAEMON))" || { echo "not running"; exit 1; } ;;
    *) echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac
EOF
chmod +x "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S20sentinel"
echo "[OK] init.d/S20sentinel"

cat > "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S30ollama" << 'EOF'
#!/bin/sh
# S30ollama — Start Ollama + pull Tier 1 models on first boot
OLLAMA_BIN="/usr/bin/ollama"
OLLAMA_PID="/var/run/ollama.pid"
OLLAMA_LOG="/var/log/sorabbyngo/ollama.log"
OLLAMA_HOME="/var/lib/sorabbyngo/ollama"
MODELS_READY="/var/lib/sorabbyngo/.ollama_models_ready"
TIER1_MODELS="phi4-mini smollm2:360m"
export OLLAMA_MODELS="$OLLAMA_HOME/models"
export OLLAMA_HOME="$OLLAMA_HOME"
mkdir -p "$OLLAMA_HOME/models" /var/log/sorabbyngo

case "$1" in
    start)
        echo -n "Starting Ollama... "
        if [ ! -x "$OLLAMA_BIN" ]; then
            echo "MISSING — install ollama post-boot"; exit 0
        fi
        "$OLLAMA_BIN" serve >> "$OLLAMA_LOG" 2>&1 &
        echo $! > "$OLLAMA_PID"
        for i in $(seq 1 10); do
            sleep 1
            curl -sf http://localhost:11434/api/tags > /dev/null 2>&1 && \
                echo "OK (pid=$(cat $OLLAMA_PID))" && break
        done
        if [ ! -f "$MODELS_READY" ]; then
            echo "First boot — pulling Tier 1 models (background)..."
            (
                for model in $TIER1_MODELS; do
                    echo "Pulling $model..." >> "$OLLAMA_LOG"
                    "$OLLAMA_BIN" pull "$model" >> "$OLLAMA_LOG" 2>&1
                done
                touch "$MODELS_READY"
                echo "Tier 1 models ready" >> "$OLLAMA_LOG"
            ) &
        fi ;;
    stop)
        echo -n "Stopping Ollama... "
        [ -f "$OLLAMA_PID" ] && kill "$(cat $OLLAMA_PID)" 2>/dev/null && rm -f "$OLLAMA_PID"
        echo "OK" ;;
    restart) "$0" stop; sleep 2; "$0" start ;;
    status)
        [ -f "$OLLAMA_PID" ] && kill -0 "$(cat $OLLAMA_PID)" 2>/dev/null && \
            echo "Ollama running" || { echo "not running"; exit 1; }
        [ -f "$MODELS_READY" ] && echo "Models: ready" || echo "Models: pulling" ;;
    *) echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac
EOF
chmod +x "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S30ollama"
echo "[OK] init.d/S30ollama"

cat > "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S40chameha" << 'EOF'
#!/bin/sh
# S40chameha — Prepare Chameha environment (tty1 respawn via inittab)
CHAMEHA_LOG="/var/log/sorabbyngo/chameha.log"
CHAMEHA_READY="/var/lib/sorabbyngo/.chameha_ready"
mkdir -p /var/log/sorabbyngo /var/lib/sorabbyngo

case "$1" in
    start)
        echo -n "Preparing Chameha... "
        touch "$CHAMEHA_READY"
        NODE_ID=$(cat /var/lib/sorabbyngo/nodeid 2>/dev/null || echo "unknown")
        echo "$(date -Iseconds) Chameha ready — NodeID: $NODE_ID" >> "$CHAMEHA_LOG"
        echo "OK" ;;
    stop)
        echo -n "Stopping Chameha... "
        rm -f "$CHAMEHA_READY"
        pkill -f "chameha_firstboot.py" 2>/dev/null || true
        echo "OK" ;;
    restart) "$0" stop; sleep 1; "$0" start ;;
    status)
        [ -f "$CHAMEHA_READY" ] && echo "Chameha: ready" || { echo "not ready"; exit 1; } ;;
    *) echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac
EOF
chmod +x "$REPO_DIR/board/sorabbyngo/overlay/etc/init.d/S40chameha"
echo "[OK] init.d/S40chameha"

# ── 5. Chameha session loop ───────────────────────────────────

mkdir -p "$REPO_DIR/board/sorabbyngo/overlay/opt/sorabbyngo/chameha/first-boot"
cat > "$REPO_DIR/board/sorabbyngo/overlay/opt/sorabbyngo/chameha/first-boot/chameha_firstboot.py" << 'PYEOF'
#!/usr/bin/env python3
"""
Chameha — Sorabbyngo OS AI Interface
Full interactive session loop for inittab tty1 respawn.
Tier 1 local inference via Ollama. No cloud. No data leaves the node.
"""
from __future__ import annotations
import json, os, signal, sys, time, urllib.error, urllib.request
from datetime import datetime
from pathlib import Path

OLLAMA_URL        = "http://localhost:11434/api/generate"
OLLAMA_HEALTH_URL = "http://localhost:11434/api/tags"
MODEL_TIER1       = "phi4-mini"
NODEID_PATH       = Path("/var/lib/sorabbyngo/nodeid")
MODELS_READY_FLAG = Path("/var/lib/sorabbyngo/.ollama_models_ready")
CHAMEHA_LOG       = Path("/var/log/sorabbyngo/chameha.log")
ENABLE_CLOUD      = os.environ.get("ENABLE_CLOUD_AI", "false").lower() == "true"

SYSTEM_PROMPT = (
    "You are Chameha, the AI core of SORABBYNGO OS — Africa's sovereign AI operating system. "
    "Warm, direct, highly capable. You speak English and Swahili fluently. "
    "You know this system deeply: kernel, security, mesh networking, health platform. "
    "You never diagnose medical conditions. Keep responses concise — this is a terminal. "
    "Greeting: 'Sorabbyngo — Rhythm of Digital Life.'"
)

BANNER = """
╔══════════════════════════════════════════════════════════════════╗
║   ░█▀▀░█▀█░█▀▄░█▀█░█▀▄░█▀▄░█░█░█▀█░█▀▀░█▀█                    ║
║   ░▀▀█░█░█░█▀▄░█▀█░█▀▄░█▀▄░░█░░█░█░█░█░█░█                    ║
║   ░▀▀▀░▀▀▀░▀░▀░▀░▀░▀▀░░▀▀░░░▀░░▀▀▀░▀▀▀░▀▀▀                    ║
║                  Rhythm of Digital Life   Kisii, Kenya           ║
╚══════════════════════════════════════════════════════════════════╝"""

HELP = """
  /help          show this help
  /status        system and AI status
  /nodeid        hardware identity
  /tier          active inference tier
  /cloud <query> Tier 3 query (needs ENABLE_CLOUD_AI=true)
  /clear         clear screen
  /exit          exit session
"""

def log(msg):
    CHAMEHA_LOG.parent.mkdir(parents=True, exist_ok=True)
    with open(CHAMEHA_LOG, "a") as f:
        f.write(f"{datetime.now().isoformat()} {msg}\n")

def get_node_id():
    try: return NODEID_PATH.read_text().strip()
    except FileNotFoundError: return os.environ.get("SORABBYNGO_NODE_ID", "unidentified")

def wait_for_ollama(timeout=30):
    sys.stdout.write("  Warming up local intelligence")
    sys.stdout.flush()
    for _ in range(timeout):
        try:
            with urllib.request.urlopen(OLLAMA_HEALTH_URL, timeout=2) as r:
                if r.status == 200:
                    sys.stdout.write(" ✓\n"); return True
        except Exception: pass
        sys.stdout.write("."); sys.stdout.flush(); time.sleep(1)
    sys.stdout.write(" (offline)\n"); return False

def infer(prompt, model=MODEL_TIER1):
    payload = json.dumps({
        "model": model,
        "prompt": f"{SYSTEM_PROMPT}\n\nUser: {prompt}\nChameha:",
        "stream": True,
        "options": {"temperature": 0.7, "num_predict": 512},
    }).encode()
    try:
        req = urllib.request.Request(OLLAMA_URL, data=payload,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=60) as r:
            for line in r:
                if not line.strip(): continue
                try:
                    chunk = json.loads(line.decode())
                    sys.stdout.write(chunk.get("response", ""))
                    sys.stdout.flush()
                    if chunk.get("done"): break
                except json.JSONDecodeError: continue
        print()
    except Exception as e:
        print(f"Inference error: {e}")

def cmd_status(ollama_online):
    nid = get_node_id()
    kernel = os.popen("uname -r").read().strip()
    uptime = os.popen("uptime -p 2>/dev/null").read().strip()
    return (f"  NodeID:  {nid[:16]}...{nid[-8:]}\n"
            f"  Kernel:  {kernel}\n"
            f"  Uptime:  {uptime}\n"
            f"  Ollama:  {'online' if ollama_online else 'offline'}\n"
            f"  Models:  {'ready' if MODELS_READY_FLAG.exists() else 'pulling'}\n"
            f"  Cloud:   {'enabled (opt-in)' if ENABLE_CLOUD else 'disabled'}\n"
            f"  Tier:    {'1 — Phi-4 Mini (local)' if ollama_online else 'offline fallback'}")

def run(ollama_online):
    nid = get_node_id()
    print(f"\n  Node: {nid[:24]}...")
    print(f"  {datetime.now().strftime('%A %d %B %Y  %H:%M')}\n")
    if ollama_online and MODELS_READY_FLAG.exists():
        print("  Chameha: ", end="", flush=True)
        infer("Hello. Introduce yourself briefly.")
    else:
        print("  Chameha: Sorabbyngo — Rhythm of Digital Life.")
        print("           Local models warming up. Full intelligence available shortly.")
    print()
    while True:
        try:
            user_input = input("  You: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n  Chameha: Goodbye. Sorabbyngo remains.")
            log("session ended"); break
        if not user_input: continue
        log(f"input: {user_input[:80]}")
        if user_input.startswith("/"):
            parts = user_input.split(None, 1)
            cmd = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ""
            if cmd == "/exit":
                print("  Chameha: Session closed."); log("exit"); break
            elif cmd == "/help":   print(HELP)
            elif cmd == "/status": print(cmd_status(ollama_online))
            elif cmd == "/nodeid": print(f"  NodeID: {nid}")
            elif cmd == "/tier":
                t = "1 — Phi-4 Mini (local)" if ollama_online else "offline"
                print(f"  Active tier: {t}")
            elif cmd == "/cloud":
                if not ENABLE_CLOUD:
                    print("  Cloud disabled. Set ENABLE_CLOUD_AI=true to enable.")
                else:
                    print("  Cloud tier coming in Phase 4.")
            elif cmd == "/clear":
                os.system("clear"); print(BANNER)
            else:
                print(f"  Unknown: {cmd}. Type /help.")
            continue
        if ollama_online:
            print("  Chameha: ", end="", flush=True)
            infer(user_input)
        else:
            print("  Chameha: Local intelligence offline. Type /status.")
        print()

def main():
    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))
    os.system("clear")
    print(BANNER)
    log("session starting")
    ollama_online = wait_for_ollama()
    try:
        run(ollama_online)
    except Exception as e:
        log(f"error: {e}")
        print(f"\n  Error: {e}\n  Restarting in 3s...")
        time.sleep(3)

if __name__ == "__main__":
    main()
PYEOF
chmod +x "$REPO_DIR/board/sorabbyngo/overlay/opt/sorabbyngo/chameha/first-boot/chameha_firstboot.py"
echo "[OK] chameha_firstboot.py"

# ── 6. Fixed defconfig ────────────────────────────────────────

cat > "$REPO_DIR/configs/sorabbyngo_x86_64_defconfig" << 'EOF'
BR2_x86_64=y
BR2_x86_corei7=y
BR2_TOOLCHAIN_BUILDROOT_GLIBC=y
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_KERNEL_HEADERS_AS_KERNEL=y
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="$(SORABBYNGO_KERNEL_CONFIG)"
BR2_LINUX_KERNEL_CUSTOM_TARBALL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="$(SORABBYNGO_KERNEL_TARBALL)"
BR2_INIT_BUSYBOX=y
BR2_TARGET_GENERIC_HOSTNAME="sorabbyngo"
BR2_TARGET_GENERIC_ISSUE="Sorabbyngo OS — Rhythm of Digital Life"
BR2_TARGET_GENERIC_ROOT_PASSWD="sorabbyngo"
BR2_TARGET_GENERIC_PASSWD_SHA256=y
BR2_ENABLE_LOCALE=y
BR2_GENERATE_LOCALE="en_US.UTF-8 sw_KE.UTF-8"
BR2_SYSTEM_DEFAULT_PATH="/bin:/sbin:/usr/bin:/usr/sbin"
BR2_ROOTFS_SKELETON_DEFAULT=y
BR2_PACKAGE_BASH=y
BR2_PACKAGE_BASH_COMPLETION=y
BR2_PACKAGE_PYTHON3=y
BR2_PACKAGE_BUSYBOX=y
BR2_PACKAGE_UTIL_LINUX=y
BR2_PACKAGE_UTIL_LINUX_MOUNT=y
BR2_PACKAGE_E2FSPROGS=y
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_WGET=y
BR2_PACKAGE_CURL=y
BR2_PACKAGE_CA_CERTIFICATES=y
BR2_PACKAGE_DHCPCD=y
BR2_PACKAGE_IPROUTE2=y
BR2_PACKAGE_DOSFSTOOLS=y
BR2_PACKAGE_PARTED=y
BR2_PACKAGE_PCIUTILS=y
BR2_PACKAGE_USBUTILS=y
BR2_PACKAGE_PYTHON_NATS_PY=y
BR2_PACKAGE_SORABBYNGO_SENTINEL=y
BR2_PACKAGE_SORABBYNGO_CHAMEHA=y
BR2_PACKAGE_SORABBYNGO_NATS=y
BR2_ROOTFS_OVERLAY="board/sorabbyngo/overlay"
BR2_ROOTFS_POST_BUILD_SCRIPT="board/sorabbyngo/post-build.sh"
BR2_ROOTFS_POST_IMAGE_SCRIPT="board/sorabbyngo/post-image.sh"
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="4G"
BR2_TARGET_ROOTFS_ISO9660=y
BR2_TARGET_ROOTFS_ISO9660_HYBRID=y
BR2_TARGET_GRUB2=y
BR2_TARGET_GRUB2_X86_64_EFI=y
BR2_TARGET_GRUB2_BUILTIN_MODULES_EFI="boot linux ext2 fat serial part_msdos part_gpt normal iso9660 gzio"
BR2_TARGET_GRUB2_BUILTIN_MODULES_PC="boot linux ext2 fat serial part_msdos part_gpt normal iso9660 gzio"
BR2_TARGET_ROOTFS_ISO9660_BOOT_MENU="board/sorabbyngo/grub/grub.cfg"
EOF
echo "[OK] configs/sorabbyngo_x86_64_defconfig"

# ── 7. Build environment script ───────────────────────────────

mkdir -p "$REPO_DIR/scripts"
cat > "$REPO_DIR/scripts/build-env.sh" << 'EOF'
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
EOF
chmod +x "$REPO_DIR/scripts/build-env.sh"
echo "[OK] scripts/build-env.sh"

# ── Done ──────────────────────────────────────────────────────

echo ""
echo "════════════════════════════════════════════════"
echo "  Sorabbyngo build system installed."
echo ""
echo "  Next steps:"
echo "  1. cd ~/sorabbyngo-os"
echo "  2. source scripts/build-env.sh"
echo "  3. Clone buildroot if needed:"
echo "     git clone https://github.com/buildroot/buildroot ~/buildroot"
echo "  4. make -C ~/buildroot O=\$SORABBYNGO_OUTPUT \\"
echo "          BR2_EXTERNAL=\$BR2_EXTERNAL \\"
echo "          sorabbyngo_x86_64_defconfig"
echo "  5. make -C ~/buildroot O=\$SORABBYNGO_OUTPUT -j\$(nproc)"
echo "════════════════════════════════════════════════"

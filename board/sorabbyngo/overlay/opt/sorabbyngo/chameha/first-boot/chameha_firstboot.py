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

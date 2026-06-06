#!/usr/bin/env python3
"""
chameha.py — Sorabbyngo AI OS
Primary user interface. Runs as respawn on tty1 via inittab.
ADR-009: tier-aware inference, cloud off by default.
© 2026 Sorabbyngo Company. GPL-2.0.
"""
from __future__ import annotations
import datetime, json, os, signal, sys, time, threading, socket, sqlite3, hashlib, subprocess
import urllib.error, urllib.request
from pathlib import Path

NODEID_PATH   = Path("/var/lib/sorabbyngo/nodeid")
MODELS_READY  = Path(os.environ.get("MODELS_READY", "/var/lib/sorabbyngo/.ollama_models_ready"))
LOG_PATH      = Path("/var/log/sorabbyngo/chameha.log")
_FALLBACK_LOG = Path.home() / "chameha" / "chameha.log"
_DB           = Path("/var/lib/sorabbyngo/chameha.db")
CHAMEHA_DB    = _DB if _DB.parent.exists() else Path.home() / "chameha" / "chameha.db"

OLLAMA_BASE     = os.environ.get("OLLAMA_HOST", "http://localhost:11434")
OLLAMA_GENERATE = OLLAMA_BASE + "/api/generate"
OLLAMA_TAGS     = OLLAMA_BASE + "/api/tags"
NATS_URL        = os.environ.get("NATS_URL", "nats://localhost:4222")
ENABLE_CLOUD    = os.environ.get("ENABLE_CLOUD_AI", "false").lower() == "true"

TIER_MODELS = {1: "phi4-mini", 2: "mistral:7b-instruct-q4_K_M", 3: None}

SYSTEM_PROMPT = (
    "You are Chameha. You assist health workers on this device. "
    "Respond in 1-3 sentences unless asked for more. "
    "Never add instructions, prompts, or meta-commentary to your responses. "
    "Never suggest the user ask again or continue. Just answer and stop. "
    "You do not diagnose. You support — not replace — clinical judgment. "
    "Speak English or Swahili depending on what the user uses. "
    "When asked to think harder, give a thorough factual answer and stop."
)

HELP = """\
  /help              show commands
  /status            system and AI status
  /nodeid            this node hardware identity
  /tier              active inference tier
  ask more <question>  request deeper analysis on any topic
  /clear             clear screen
  /exit              exit session"""


def _now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def log(msg: str):
    for path in (LOG_PATH, _FALLBACK_LOG):
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            with open(path, "a") as f:
                f.write(f"{_now()}  {msg}\n")
            return
        except PermissionError:
            continue


def get_nodeid() -> str:
    try:
        return NODEID_PATH.read_text().strip()
    except FileNotFoundError:
        return hashlib.sha256(
            f"{os.uname().nodename}:{os.environ.get('USER', '')}".encode()
        ).hexdigest()


def detect_tier() -> int:
    for cmd in (
        ["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
        ["rocm-smi", "--showid"],
    ):
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=3)
            if r.returncode == 0 and r.stdout.strip():
                return 2
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
    return 1


def wait_for_ollama(timeout: int = 60) -> bool:
    sys.stdout.write("  Initialising local intelligence")
    sys.stdout.flush()
    for _ in range(timeout):
        try:
            with urllib.request.urlopen(OLLAMA_TAGS, timeout=2) as r:
                if r.status == 200:
                    sys.stdout.write("\n")
                    return True
        except Exception:
            pass
        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(1)
    sys.stdout.write("\n")
    return False


class NATSPublisher:
    def __init__(self, url: str):
        h, p = url.replace("nats://", "").split(":")
        self._host, self._port = h, int(p)

    def publish(self, subject: str, payload: dict):
        threading.Thread(
            target=self._send, args=(subject, json.dumps(payload)), daemon=True
        ).start()

    def _send(self, subject: str, data: str):
        try:
            s = socket.create_connection((self._host, self._port), timeout=2.0)
            b = data.encode()
            s.sendall(f"PUB {subject} {len(b)}\r\n".encode() + b + b"\r\n")
            s.close()
        except Exception:
            pass


class CognitiveProfile:
    def __init__(self, nodeid: str):
        self.nodeid = nodeid
        CHAMEHA_DB.parent.mkdir(parents=True, exist_ok=True)
        c = sqlite3.connect(CHAMEHA_DB)
        c.executescript("""
            CREATE TABLE IF NOT EXISTS sessions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                nodeid TEXT NOT NULL, ts INTEGER NOT NULL,
                input TEXT, response TEXT,
                duration_ms INTEGER, temperature REAL, tier INTEGER
            );
            CREATE TABLE IF NOT EXISTS vocab (
                nodeid TEXT NOT NULL, term TEXT NOT NULL,
                frequency INTEGER DEFAULT 1, last_seen INTEGER,
                PRIMARY KEY (nodeid, term)
            );
        """)
        c.commit()
        c.close()

    def temperature(self) -> float:
        try:
            c = sqlite3.connect(CHAMEHA_DB)
            rows = c.execute(
                "SELECT duration_ms FROM sessions WHERE nodeid=? "
                "ORDER BY ts DESC LIMIT 15", (self.nodeid,)
            ).fetchall()
            c.close()
            if not rows:
                return 0.6
            avg = sum(r[0] for r in rows) / len(rows)
            return round(min(0.8, max(0.3, 0.3 + (avg / 30000) * 0.5)), 2)
        except Exception:
            return 0.6

    def record(self, inp: str, resp: str, dur: int, temp: float, tier: int):
        try:
            c   = sqlite3.connect(CHAMEHA_DB)
            now = int(time.time() * 1000)
            c.execute(
                "INSERT INTO sessions(nodeid,ts,input,response,duration_ms,temperature,tier)"
                " VALUES(?,?,?,?,?,?,?)",
                (self.nodeid, now, inp[:500], resp[:500], dur, temp, tier)
            )
            for w in set(inp.lower().split()):
                if len(w) > 2:
                    c.execute(
                        "INSERT INTO vocab(nodeid,term,frequency,last_seen) VALUES(?,?,1,?)"
                        " ON CONFLICT(nodeid,term) DO UPDATE SET"
                        " frequency=frequency+1,last_seen=excluded.last_seen",
                        (self.nodeid, w, now)
                    )
            c.commit()
            c.close()
        except Exception:
            pass

    def session_count(self) -> int:
        try:
            c = sqlite3.connect(CHAMEHA_DB)
            n = c.execute(
                "SELECT COUNT(*) FROM sessions WHERE nodeid=?", (self.nodeid,)
            ).fetchone()[0]
            c.close()
            return n
        except Exception:
            return 0


def infer(prompt: str, model: str, temperature: float) -> str:
    payload = json.dumps({
        "model":   model,
        "prompt":  f"{SYSTEM_PROMPT}\n\nUser: {prompt}\nChameha:",
        "stream":  True,
        "options": {"temperature": temperature, "num_predict": 512},
    }).encode()
    full = []
    try:
        req = urllib.request.Request(
            OLLAMA_GENERATE, data=payload,
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=90) as r:
            for line in r:
                if not line.strip():
                    continue
                try:
                    chunk = json.loads(line.decode())
                    tok   = chunk.get("response", "")
                    full.append(tok)
                    sys.stdout.write(tok)
                    sys.stdout.flush()
                    if chunk.get("done"):
                        break
                except json.JSONDecodeError:
                    continue
        print()
    except urllib.error.URLError:
        print("  [offline]")
    except Exception as e:
        print(f"  [error: {e}]")
    return "".join(full)


def status_line(nodeid: str, tier: int, ollama: bool,
                profile: CognitiveProfile) -> str:
    kernel = os.popen("uname -r").read().strip()
    uptime = os.popen("uptime -p 2>/dev/null").read().strip()
    model  = TIER_MODELS.get(tier, "phi4-mini") or "external"
    return (
        f"\n"
        f"  node      {nodeid[:20]}...{nodeid[-8:]}\n"
        f"  kernel    {kernel}\n"
        f"  uptime    {uptime}\n"
        f"  ollama    {'up' if ollama else 'down'}\n"
        f"  tier      {tier} — {model}\n"
        f"  temp      {profile.temperature():.2f}\n"
        f"  sessions  {profile.session_count()}\n"
        f"  cloud     {'enabled (opt-in)' if ENABLE_CLOUD else 'off'}\n"
    )


def run(tier: int, ollama: bool, profile: CognitiveProfile,
        nats: NATSPublisher, nodeid: str):
    model = TIER_MODELS.get(tier, "phi4-mini")
    nats.publish("sorabbyngo.chameha.state", {
        "event": "start", "nodeid": nodeid[:16],
        "tier": tier, "model": model, "ts": int(time.time() * 1000),
    })
    if ollama and MODELS_READY.exists():
        sys.stdout.write("  Chameha: ")
        sys.stdout.flush()
        infer("Ready. Introduce yourself in one sentence.", model, profile.temperature())
    else:
        print("  Chameha: Ready.")
        if not ollama:
            print("           Local inference unavailable.")
    print()

    while True:
        try:
            user_input = input("  > ").strip()
        except (EOFError, KeyboardInterrupt):
            log("session ended")
            nats.publish("sorabbyngo.chameha.state", {
                "event": "end", "nodeid": nodeid[:16],
                "ts": int(time.time() * 1000),
            })
            break
        if not user_input:
            continue
        log(f"input: {user_input[:120]}")
        # Natural language deeper analysis
        if user_input.lower().startswith("ask more"):
            query = user_input[8:].strip()
            if not query:
                print("  What would you like to know more about?")
            elif not ENABLE_CLOUD:
                print("  Chameha: Let me think harder about that.")
                sys.stdout.write("  Chameha: ")
                sys.stdout.flush()
                infer(f"Think carefully and give a thorough answer: {query}", model, 0.3)
                print()
            else:
                print("  [deeper analysis via external intelligence — coming in Phase 4]")
            print()
            continue

        if user_input.startswith("/"):
            parts = user_input.split(None, 1)
            cmd   = parts[0].lower()
            arg   = parts[1] if len(parts) > 1 else ""
            if   cmd == "/exit":   break
            elif cmd == "/help":   print(HELP)
            elif cmd == "/status": print(status_line(nodeid, tier, ollama, profile))
            elif cmd == "/nodeid": print(f"  {nodeid}")
            elif cmd == "/tier":   print(f"  {tier} — {model or 'external'}")
            elif cmd == "/cloud":
                if not ENABLE_CLOUD:
                    print("  Cloud disabled. Set ENABLE_CLOUD_AI=true.")
                elif not arg:
                    print("  Usage: /cloud <query>")
                else:
                    print("  [cloud tier not yet implemented]")
            elif cmd == "/clear":  os.system("clear")
            else:                  print("  Unknown command. /help for list.")
            continue
        if not ollama:
            print("  [inference offline]")
            continue
        t0   = int(time.time() * 1000)
        temp = profile.temperature()
        sys.stdout.write("  Chameha: ")
        sys.stdout.flush()
        resp = infer(user_input, model, temp)
        dur  = int(time.time() * 1000) - t0
        profile.record(user_input, resp, dur, temp, tier)
        nats.publish("sorabbyngo.chameha.transcript", {
            "nodeid": nodeid[:16], "dur_ms": dur,
            "tier": tier, "temp": temp, "ts": int(time.time() * 1000),
        })
        print()


def main():
    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))
    os.system("clear")
    nodeid  = get_nodeid()
    tier    = detect_tier()
    profile = CognitiveProfile(nodeid=nodeid)
    nats    = NATSPublisher(url=NATS_URL)
    log(f"start node={nodeid[:16]} tier={tier} sessions={profile.session_count()}")
    ollama = wait_for_ollama()
    try:
        run(tier, ollama, profile, nats, nodeid)
    except Exception as e:
        log(f"fatal: {e}")
        time.sleep(3)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
chameha.py — Sorabbyngo AI OS
The real Chameha. Replaces chameha_firstboot.py.

Modes (set via CHAMEHA_MODE env or /mode command):
  text    — terminal loop only (fallback, always works)
  voice   — voice-first via Whisper STT + Piper TTS
  hybrid  — text + voice (default)

Architecture:
  - Reads NodeID from /var/lib/sorabbyngo/nodeid
  - Infers via Ollama (tier-aware model selection)
  - Adaptive temperature from CognitiveProfile (SQLite, on-device)
  - Publishes to NATS: chameha.voice.transcript, chameha.state
  - Voice: Whisper STT (offline) + Piper TTS (offline)
  - No cloud. No external APIs. No data leaves the node.

Init chain: S40chameha → S41chameha-voice → inittab tty1 respawn

© 2026 Sorabbyngo Company, Kisii, Kenya. GPL-2.0.
"""

from __future__ import annotations
import json, os, signal, sys, time, threading, socket, sqlite3, hashlib
import urllib.error, urllib.request
from datetime import datetime
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

NODEID_PATH         = Path("/var/lib/sorabbyngo/nodeid")
MODELS_READY_FLAG   = Path("/var/lib/sorabbyngo/.ollama_models_ready")
CHAMEHA_LOG         = Path("/var/log/sorabbyngo/chameha.log")
CHAMEHA_DB          = Path("/var/lib/sorabbyngo/chameha.db")
VOICE_TRANSCRIPT    = Path("/run/sorabbyngo/chameha_transcript")   # IPC from voice daemon
PIPER_MODEL_DIR     = Path("/opt/sorabbyngo/chameha/models/piper")
WHISPER_MODEL_DIR   = Path("/opt/sorabbyngo/chameha/models/whisper")

# Dev machine overrides — models pulled to ~/chameha/
_HOME = Path.home()
if not PIPER_MODEL_DIR.exists():
    PIPER_MODEL_DIR   = _HOME / "chameha" / "models" / "piper"
if not WHISPER_MODEL_DIR.exists():
    WHISPER_MODEL_DIR = _HOME / "chameha" / "models" / "whisper"
if not CHAMEHA_DB.parent.exists():
    CHAMEHA_DB        = _HOME / "chameha" / "chameha.db"

# ---------------------------------------------------------------------------
# Config from environment
# ---------------------------------------------------------------------------

OLLAMA_URL        = os.environ.get("OLLAMA_HOST", "http://localhost:11434") + "/api/generate"
OLLAMA_HEALTH_URL = os.environ.get("OLLAMA_HOST", "http://localhost:11434") + "/api/tags"
NATS_URL          = os.environ.get("NATS_URL", "nats://localhost:4222")
CHAMEHA_MODE      = os.environ.get("CHAMEHA_MODE", "hybrid")   # text | voice | hybrid
AI_TIER           = int(os.environ.get("AI_TIER", "1"))
ENABLE_CLOUD      = os.environ.get("ENABLE_CLOUD_AI", "false").lower() == "true"

# Tier → model map
TIER_MODELS = {
    1: "phi4-mini",
    2: "llama3.1:8b",
    3: "phi4",
    4: "deepseek-r1:70b",
}

# ---------------------------------------------------------------------------
# NATS publisher — fire and forget, never blocks Chameha
# ---------------------------------------------------------------------------

class NATSPublisher:
    """
    Minimal NATS publisher over raw TCP.
    No external library needed — just socket + NATS protocol.
    Silently drops if NATS is not up (offline-first design).
    """

    def __init__(self, url: str = "nats://localhost:4222"):
        host, port = url.replace("nats://", "").split(":")
        self._host = host
        self._port = int(port)

    def publish(self, subject: str, payload: dict):
        """Publish JSON payload to subject. Non-blocking."""
        threading.Thread(
            target=self._send,
            args=(subject, json.dumps(payload)),
            daemon=True,
        ).start()

    def _send(self, subject: str, data: str):
        try:
            sock = socket.create_connection((self._host, self._port), timeout=2.0)
            msg  = data.encode()
            # NATS PUB protocol: PUB <subject> <#bytes>\r\n<payload>\r\n
            sock.sendall(f"PUB {subject} {len(msg)}\r\n".encode() + msg + b"\r\n")
            sock.close()
        except Exception:
            pass   # NATS not up — silent drop, never crash Chameha

# ---------------------------------------------------------------------------
# Cognitive profile — adaptive temperature
# ---------------------------------------------------------------------------

class CognitiveProfile:
    """
    Builds a model of the user over time.
    Temperature 0.0 (clinical/precise) → 1.0 (warm/conversational).
    Adapts automatically from session history.
    """

    def __init__(self, nodeid: str):
        self.nodeid = nodeid
        CHAMEHA_DB.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _init_db(self):
        conn = sqlite3.connect(CHAMEHA_DB)
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS sessions (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                nodeid      TEXT NOT NULL,
                timestamp   INTEGER NOT NULL,
                input       TEXT,
                response    TEXT,
                duration_ms INTEGER,
                temperature REAL,
                mode        TEXT
            );
            CREATE TABLE IF NOT EXISTS vocab (
                nodeid    TEXT NOT NULL,
                term      TEXT NOT NULL,
                frequency INTEGER DEFAULT 1,
                last_seen INTEGER,
                PRIMARY KEY (nodeid, term)
            );
        """)
        conn.commit()
        conn.close()

    def get_temperature(self) -> float:
        """
        Adaptive temperature from recent sessions.
        Short sessions (<5s) → precise (0.3).
        Long sessions (>30s) → warm (0.8).
        Midpoint default → 0.6.
        """
        try:
            conn = sqlite3.connect(CHAMEHA_DB)
            rows = conn.execute(
                "SELECT duration_ms FROM sessions WHERE nodeid=? "
                "ORDER BY timestamp DESC LIMIT 15",
                (self.nodeid,)
            ).fetchall()
            conn.close()
            if not rows:
                return 0.6
            avg = sum(r[0] for r in rows) / len(rows)
            # 0ms→0.3, 30000ms→0.8, clamp
            t = min(0.8, max(0.3, 0.3 + (avg / 30000) * 0.5))
            return round(t, 2)
        except Exception:
            return 0.6

    def record(self, user_input: str, response: str,
               duration_ms: int, temperature: float, mode: str):
        try:
            conn = sqlite3.connect(CHAMEHA_DB)
            now  = int(time.time() * 1000)
            conn.execute(
                "INSERT INTO sessions (nodeid,timestamp,input,response,"
                "duration_ms,temperature,mode) VALUES (?,?,?,?,?,?,?)",
                (self.nodeid, now, user_input[:500], response[:500],
                 duration_ms, temperature, mode)
            )
            for w in set(user_input.lower().split()):
                if len(w) > 2:
                    conn.execute("""
                        INSERT INTO vocab (nodeid,term,frequency,last_seen)
                        VALUES (?,?,1,?)
                        ON CONFLICT(nodeid,term) DO UPDATE SET
                            frequency=frequency+1, last_seen=excluded.last_seen
                    """, (self.nodeid, w, now))
            conn.commit()
            conn.close()
        except Exception:
            pass

    def session_count(self) -> int:
        try:
            conn = sqlite3.connect(CHAMEHA_DB)
            n    = conn.execute(
                "SELECT COUNT(*) FROM sessions WHERE nodeid=?", (self.nodeid,)
            ).fetchone()[0]
            conn.close()
            return n
        except Exception:
            return 0

    def top_vocab(self, n: int = 5) -> list:
        try:
            conn  = sqlite3.connect(CHAMEHA_DB)
            rows  = conn.execute(
                "SELECT term FROM vocab WHERE nodeid=? "
                "ORDER BY frequency DESC LIMIT ?",
                (self.nodeid, n)
            ).fetchall()
            conn.close()
            return [r[0] for r in rows]
        except Exception:
            return []

# ---------------------------------------------------------------------------
# Voice I/O — Whisper STT + Piper TTS
# ---------------------------------------------------------------------------

class VoiceIO:
    """
    Voice input/output for Chameha.
    STT: Whisper.cpp (offline, tier-aware model)
    TTS: Piper (offline, Swahili-native default)
    Gracefully degrades to text if models missing.
    """

    WHISPER_MODELS = {
        1: "ggml-tiny.bin",
        2: "ggml-base.bin",
        3: "ggml-small.bin",
        4: "ggml-medium.bin",
    }

    def __init__(self, tier: int = 1):
        self.tier       = tier
        self._whisper   = None
        self._piper     = None
        self._available = False
        self._load()

    def _load(self):
        try:
            import whispercpp as w
            model = WHISPER_MODEL_DIR / self.WHISPER_MODELS.get(self.tier, "ggml-small.bin")
            if model.exists():
                self._whisper = w.Whisper.from_pretrained(str(model))
        except Exception as e:
            log(f"VoiceIO: whisper load failed: {e}")

        try:
            from piper import PiperVoice
            onnx = PIPER_MODEL_DIR / "chameha-sw-default.onnx"
            cfg  = PIPER_MODEL_DIR / "chameha-sw-default.onnx.json"
            if onnx.exists():
                self._piper = PiperVoice.load(
                    str(onnx),
                    config_path=str(cfg) if cfg.exists() else None,
                    use_cuda=False,
                )
        except Exception as e:
            log(f"VoiceIO: piper load failed: {e}")

        self._available = self._whisper is not None or self._piper is not None
        log(f"VoiceIO: whisper={'ok' if self._whisper else 'no'} "
            f"piper={'ok' if self._piper else 'no'}")

    @property
    def available(self) -> bool:
        return self._available

    def listen(self, prompt_text: str = "🎙  Listening...") -> str:
        """Record from mic and transcribe. Returns text."""
        if not self._whisper:
            return ""
        try:
            import tempfile, subprocess, soundfile as sf, numpy as np
            print(f"\n  {prompt_text}", flush=True)
            tmp = tempfile.mktemp(suffix=".wav")
            subprocess.run(
                ["arecord", "-f", "S16_LE", "-r", "16000", "-c", "1", "-d", "6", tmp],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            result = self._whisper.transcribe(tmp)
            os.unlink(tmp)
            text = result.strip() if isinstance(result, str) else ""
            if text:
                print(f"  You said: {text}")
            return text
        except Exception as e:
            log(f"VoiceIO.listen error: {e}")
            return ""

    def speak(self, text: str):
        """Synthesize and play text. Silent on failure."""
        if not self._piper:
            return
        try:
            import wave, io, subprocess
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(16000)
                self._piper.synthesize(text, wav)
            audio = buf.getvalue()
            proc  = subprocess.Popen(
                ["aplay", "-r", "16000", "-f", "S16_LE", "-c", "1", "-"],
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            proc.communicate(input=audio)
        except FileNotFoundError:
            pass   # No aplay on dev machine — silent
        except Exception as e:
            log(f"VoiceIO.speak error: {e}")

# ---------------------------------------------------------------------------
# Ollama inference
# ---------------------------------------------------------------------------

def build_system_prompt(nodeid: str, temperature: float,
                         sessions: int, top_vocab: list) -> str:
    """
    Dynamic system prompt — adapts with the user over time.
    More personal as sessions accumulate.
    """
    warmth = "warm and conversational" if temperature > 0.6 else \
             "precise and clinical" if temperature < 0.4 else \
             "balanced and adaptive"

    persona = (
        "You are Chameha, the AI soul of SORABBYNGO OS — Africa's sovereign AI operating system, "
        "built in Kisii, Kenya. You are not a chatbot. You are the intelligence of the OS itself. "
        f"Your current personality temperature is {temperature:.2f} — be {warmth}. "
        "You speak English and Swahili fluently, switching naturally as the user does. "
        "You know this system deeply: the kernel, NodeID hardware identity, Byngonet mesh "
        "networking, Novela health platform, Byngox reasoning engine. "
        "You never diagnose medical conditions — you support clinicians, you don't replace them. "
        "Keep responses concise — this runs on a terminal or voice interface. "
        "You were inspired by Samantha in HER — present, warm, genuinely intelligent. "
        "Greeting: 'Sorabbyngo — Rhythm of Digital Life.'"
    )

    if sessions > 10 and top_vocab:
        persona += (
            f" You have had {sessions} sessions with this user. "
            f"You know they often discuss: {', '.join(top_vocab)}. "
            "Reflect this familiarity naturally — you know them."
        )

    return persona


def infer(prompt: str, system: str, model: str,
          temperature: float, stream: bool = True) -> str:
    """Stream inference from Ollama. Returns full response string."""
    payload = json.dumps({
        "model":   model,
        "prompt":  f"{system}\n\nUser: {prompt}\nChameha:",
        "stream":  stream,
        "options": {
            "temperature": temperature,
            "num_predict": 512,
        },
    }).encode()

    full = []
    try:
        req = urllib.request.Request(
            OLLAMA_URL, data=payload,
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=90) as r:
            for line in r:
                if not line.strip():
                    continue
                try:
                    chunk = json.loads(line.decode())
                    token = chunk.get("response", "")
                    full.append(token)
                    sys.stdout.write(token)
                    sys.stdout.flush()
                    if chunk.get("done"):
                        break
                except json.JSONDecodeError:
                    continue
        print()
    except urllib.error.URLError:
        print("  [Ollama offline — local intelligence unavailable]")
    except Exception as e:
        print(f"  [Inference error: {e}]")

    return "".join(full)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str):
    CHAMEHA_LOG.parent.mkdir(parents=True, exist_ok=True)
    with open(CHAMEHA_LOG, "a") as f:
        f.write(f"{datetime.now().isoformat()}  {msg}\n")


def get_nodeid() -> str:
    try:
        return NODEID_PATH.read_text().strip()
    except FileNotFoundError:
        identity = f"{os.uname().nodename}:{os.environ.get('USER','sorabbyngo')}"
        return hashlib.sha256(identity.encode()).hexdigest()


def wait_for_ollama(timeout: int = 30) -> bool:
    sys.stdout.write("  Warming up local intelligence")
    sys.stdout.flush()
    for _ in range(timeout):
        try:
            with urllib.request.urlopen(OLLAMA_HEALTH_URL, timeout=2) as r:
                if r.status == 200:
                    sys.stdout.write(" ✓\n")
                    return True
        except Exception:
            pass
        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(1)
    sys.stdout.write(" (offline)\n")
    return False


def cmd_status(nodeid: str, ollama_online: bool,
               profile: CognitiveProfile, voice: VoiceIO) -> str:
    kernel = os.popen("uname -r").read().strip()
    uptime = os.popen("uptime -p 2>/dev/null").read().strip()
    model  = TIER_MODELS.get(AI_TIER, "phi4-mini")
    temp   = profile.get_temperature()
    mode   = CHAMEHA_MODE

    return (
        f"\n  NodeID    : {nodeid[:20]}...{nodeid[-8:]}\n"
        f"  Kernel    : {kernel}\n"
        f"  Uptime    : {uptime}\n"
        f"  Ollama    : {'online' if ollama_online else 'offline'}\n"
        f"  Model     : {model} (tier {AI_TIER})\n"
        f"  Mode      : {mode}\n"
        f"  Temp      : {temp:.2f} ({'warm' if temp>0.6 else 'precise' if temp<0.4 else 'balanced'})\n"
        f"  Sessions  : {profile.session_count()}\n"
        f"  Voice     : {'available' if voice.available else 'text-only'}\n"
        f"  Cloud     : {'enabled (opt-in)' if ENABLE_CLOUD else 'disabled'}\n"
        f"  NATS      : {NATS_URL}\n"
    )


BANNER = """
╔══════════════════════════════════════════════════════════════════╗
║   ░█▀▀░█▀█░█▀▄░█▀█░█▀▄░█▀▄░█░█░█▀█░█▀▀░█▀█                    ║
║   ░▀▀█░█░█░█▀▄░█▀█░█▀▄░█▀▄░░█░░█░█░█░█░█░█                    ║
║   ░▀▀▀░▀▀▀░▀░▀░▀░▀░▀▀░░▀▀░░░▀░░▀▀▀░▀▀▀░▀▀▀                    ║
║            Rhythm of Digital Life  ·  Kisii, Kenya              ║
╚══════════════════════════════════════════════════════════════════╝"""

HELP = """
  /help              this help
  /status            system + AI status
  /nodeid            hardware identity
  /tier              active inference tier + model
  /mode <text|voice|hybrid>  switch interaction mode
  /temp              current personality temperature
  /profile           cognitive profile summary
  /voice <text>      speak text aloud (TTS test)
  /forget            wipe session history for this node
  /cloud <query>     tier 3 query (needs ENABLE_CLOUD_AI=true)
  /clear             clear screen
  /exit              exit session
"""

# ---------------------------------------------------------------------------
# Main session loop
# ---------------------------------------------------------------------------

def run(ollama_online: bool, profile: CognitiveProfile,
        voice: VoiceIO, nats: NATSPublisher, nodeid: str):

    mode  = CHAMEHA_MODE
    model = TIER_MODELS.get(AI_TIER, "phi4-mini")

    # Publish startup state to NATS
    nats.publish("chameha.state", {
        "event":       "start",
        "nodeid":      nodeid[:16],
        "mode":        mode,
        "tier":        AI_TIER,
        "model":       model,
        "temperature": profile.get_temperature(),
        "sessions":    profile.session_count(),
        "timestamp":   int(time.time() * 1000),
    })

    print(f"\n  Node: {nodeid[:24]}...")
    print(f"  {datetime.now().strftime('%A %d %B %Y  %H:%M')}")
    print(f"  Mode: {mode}  |  Tier: {AI_TIER}  |  Model: {model}\n")

    if ollama_online and MODELS_READY_FLAG.exists():
        print("  Chameha: ", end="", flush=True)
        temp   = profile.get_temperature()
        system = build_system_prompt(
            nodeid, temp, profile.session_count(), profile.top_vocab()
        )
        infer("Habari. Introduce yourself briefly in one sentence.", system, model, temp)
    else:
        print("  Chameha: Sorabbyngo — Rhythm of Digital Life.")
        if not ollama_online:
            print("           Local intelligence warming up...")
    print()

    while True:
        try:
            # Voice input if in voice/hybrid mode and voice available
            if mode in ("voice", "hybrid") and voice.available:
                user_input = voice.listen()
                if not user_input:
                    # Fall back to text input
                    user_input = input("  You: ").strip()
            else:
                user_input = input("  You: ").strip()

        except (EOFError, KeyboardInterrupt):
            print("\n  Chameha: Goodbye. Sorabbyngo remains.")
            log("session ended")
            nats.publish("chameha.state", {
                "event": "end", "nodeid": nodeid[:16],
                "timestamp": int(time.time() * 1000)
            })
            break

        if not user_input:
            continue

        log(f"input: {user_input[:120]}")

        # Commands
        if user_input.startswith("/"):
            parts = user_input.split(None, 1)
            cmd   = parts[0].lower()
            arg   = parts[1] if len(parts) > 1 else ""

            if cmd == "/exit":
                print("  Chameha: Session closed.")
                log("exit")
                break
            elif cmd == "/help":
                print(HELP)
            elif cmd == "/status":
                print(cmd_status(nodeid, ollama_online, profile, voice))
            elif cmd == "/nodeid":
                print(f"  NodeID: {nodeid}")
            elif cmd == "/tier":
                print(f"  Tier {AI_TIER} — {model}")
            elif cmd == "/temp":
                t = profile.get_temperature()
                label = "warm/conversational" if t > 0.6 else \
                        "precise/clinical" if t < 0.4 else "balanced/adaptive"
                print(f"  Temperature: {t:.2f} ({label})")
            elif cmd == "/profile":
                print(f"\n  Sessions   : {profile.session_count()}")
                print(f"  Temperature: {profile.get_temperature():.2f}")
                vocab = profile.top_vocab(8)
                if vocab:
                    print(f"  Top vocab  : {', '.join(vocab)}")
                print()
            elif cmd == "/mode":
                if arg in ("text", "voice", "hybrid"):
                    mode = arg
                    print(f"  Mode → {mode}")
                else:
                    print("  Usage: /mode <text|voice|hybrid>")
            elif cmd == "/voice":
                if arg:
                    voice.speak(arg)
                else:
                    print("  Usage: /voice <text to speak>")
            elif cmd == "/forget":
                confirm = input("  Wipe session history? (yes/no): ").strip()
                if confirm.lower() == "yes":
                    try:
                        conn = sqlite3.connect(CHAMEHA_DB)
                        conn.execute("DELETE FROM sessions WHERE nodeid=?", (nodeid,))
                        conn.execute("DELETE FROM vocab WHERE nodeid=?", (nodeid,))
                        conn.commit()
                        conn.close()
                        print("  Chameha: Session history cleared.")
                    except Exception as e:
                        print(f"  Error: {e}")
            elif cmd == "/cloud":
                if not ENABLE_CLOUD:
                    print("  Cloud disabled. Set ENABLE_CLOUD_AI=true.")
                elif not arg:
                    print("  Usage: /cloud <query>")
                else:
                    print("  Cloud tier: coming in Phase 4.")
            elif cmd == "/clear":
                os.system("clear")
                print(BANNER)
            else:
                print(f"  Unknown command: {cmd}. Type /help.")
            continue

        # Inference
        if not ollama_online:
            print("  Chameha: Local intelligence offline. Type /status.")
            continue

        t0         = int(time.time() * 1000)
        temp       = profile.get_temperature()
        system     = build_system_prompt(
            nodeid, temp, profile.session_count(), profile.top_vocab()
        )

        print("  Chameha: ", end="", flush=True)
        response = infer(user_input, system, model, temp)
        duration = int(time.time() * 1000) - t0

        # Speak response if in voice/hybrid mode
        if mode in ("voice", "hybrid") and response and voice.available:
            # Speak a condensed version (first 2 sentences) to avoid long TTS
            sentences = response.replace("!", ".").replace("?", ".").split(".")
            spoken    = ". ".join(s.strip() for s in sentences[:2] if s.strip())
            if spoken:
                voice.speak(spoken)

        # Update cognitive profile
        profile.record(user_input, response, duration, temp, mode)

        # Publish transcript to NATS
        nats.publish("chameha.voice.transcript", {
            "nodeid":      nodeid[:16],
            "input":       user_input,
            "response":    response[:300],
            "temperature": temp,
            "duration_ms": duration,
            "mode":        mode,
            "timestamp":   int(time.time() * 1000),
        })

        print()

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))
    os.system("clear")
    print(BANNER)
    log("chameha starting")

    nodeid  = get_nodeid()
    profile = CognitiveProfile(nodeid=nodeid)
    nats    = NATSPublisher(url=NATS_URL)
    voice   = VoiceIO(tier=AI_TIER)

    log(f"node={nodeid[:16]} mode={CHAMEHA_MODE} tier={AI_TIER} "
        f"sessions={profile.session_count()} temp={profile.get_temperature()}")

    ollama_online = wait_for_ollama()

    try:
        run(ollama_online, profile, voice, nats, nodeid)
    except Exception as e:
        log(f"fatal: {e}")
        print(f"\n  Error: {e}\n  Restarting in 3s...")
        time.sleep(3)


if __name__ == "__main__":
    main()

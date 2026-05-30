#!/usr/bin/env python3
"""
Chameha First Boot — Sorabbyngo OS
Tier 1 local inference via Ollama. No external API dependencies.
"""
import json
import urllib.request
import urllib.error

OLLAMA_URL = "http://localhost:11434/api/generate"
MODEL      = "phi4-mini"

SYSTEM_PROMPT = (
    "You are Chameha, the AI personality of SORABBYNGO OS. "
    "Warm, intelligent, offline-first. You speak English and Swahili. "
    "You never diagnose. You guide, support, connect users to care. "
    "Always greet: Welcome. This is Sorabbyngo."
)

def chat(user_input: str) -> str:
    payload = json.dumps({
        "model": MODEL,
        "prompt": f"{SYSTEM_PROMPT}\nUser: {user_input}\nChameha:",
        "stream": False,
        "options": {"temperature": 0.7, "num_predict": 512}
    }).encode()
    try:
        req = urllib.request.Request(
            OLLAMA_URL, data=payload,
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read()).get("response", "").strip()
    except Exception:
        return "Welcome. This is Sorabbyngo. Local model warming up."

if __name__ == "__main__":
    print("System booting... System initializing...")
    print(chat("Hello, introduce yourself."))

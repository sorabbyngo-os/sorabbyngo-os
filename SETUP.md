# Sorabbyngo OS — New Machine Setup

## Prerequisites
- Kali Linux or Ubuntu 22.04+
- 4GB RAM minimum
- 20GB free disk

## Step 1 — Clone all repos
```bash
mkdir -p ~/sorabbyngo-labs && cd ~/sorabbyngo-labs
gh auth login
git clone https://github.com/sorabbyngo-os/sorabbyngo-os
git clone https://github.com/sorabbyngo-os/chameha
git clone https://github.com/sorabbyngo-os/byngonet
git clone https://github.com/sorabbyngo-os/byngox
git clone https://github.com/sorabbyngo-os/cml
git clone https://github.com/sorabbyngo-os/novela
```

## Step 2 — Install dependencies
```bash
sudo apt-get install -y python3 python3-pip nats-server git curl
pip install pyyaml cryptography fastapi uvicorn --break-system-packages
```

## Step 3 — Install NATS
```bash
cd ~/Downloads
wget https://github.com/nats-io/nats-server/releases/download/v2.10.14/nats-server-v2.10.14-linux-amd64.zip
unzip nats-server-v2.10.14-linux-amd64.zip
sudo mv nats-server-v2.10.14-linux-amd64/nats-server /usr/local/bin/
```

## Step 4 — Install Ollama
```bash
cmake .. && make -j4
```

## Step 5 — Pull Phi-4 Mini model
```bash



ollama pull phi4-mini
ollama pull meditron:7b
ollama pull aya23:8b
ollama pull deepseek-r1:70b
```

## Step 6 — Generate policy signing keys
```bash
python3 ~/sorabbyngo-os/policy/policy_keygen.py
python3 ~/sorabbyngo-os/policy/policy_sign.py ~/sorabbyngo-os/policy/base.yaml
```

## Step 7 — Start full stack
```bash
# Terminal 1 — NATS
nats-server -c ~/sorabbyngo-os/nats/sorabbyngo.conf


ollama serve

# Terminal 3 — Sentinel
python3 ~/sorabbyngo-os/sentinel/sentinel_daemon.py

# Terminal 4 — KCP (requires root)
sudo python3 ~/sorabbyngo-os/sentinel/sentinel_watcher.py

# Terminal 5 — Chameha
cd ~/chameha && python3 main.py
```

## Current Status
- Kernel: 6.19.10-sorabbyngo (built, in GitHub patches repo)
- SCL Phase 1: Complete
- Next: Byngonet ↔ HAL registration

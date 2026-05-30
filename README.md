<p align="center">
  <img src="assets/banner.svg" alt="Sorabbyngo OS" width="800"/>
</p>

<p align="center">
  <strong>SORABBYNGO OS — Intelligence from the kernel up</strong><br/>
  Linux 6.19.10 · Offline-first · Africa-native · No external API keys
</p>

<p align="center">
  <img src="https://img.shields.io/badge/kernel-Linux%206.19.10-blue?style=flat-square&logo=linux&logoColor=white"/>
  <img src="https://img.shields.io/badge/arch-x86__64%20%7C%20ARM64%20%7C%20RISC--V-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/AI-offline--first-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/origin-Kisii%2C%20Kenya-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/status-in%20development-blue?style=flat-square"/>
</p>

---

SORABBYNGO OS is a full AI operating system built from the ground up. Intelligence is not a feature added on top — it is built into every layer, from the hardware identity module to the voice interface. This is not a Linux distro with an AI assistant bolted on. The AI **is** the OS.

---

## System architecture

```mermaid
graph TD
    UI["User Environment\nVoice · Text · Biometric · Flutter UI\nEnglish · Swahili · 13 African languages"]
    NOV["Novela\nDjango · FHIR R4 · Counterfeit detection"]
    AI["AI Core\nByngox — 8-agent swarm reasoning\nChameha — Voice · Emotion · Personality\nCML — Safety filter"]
    AGT["AI Agent Layer\nhealth · drug · context · network\nlearning · population · finance · companion"]
    SVC["System Services\nNATS JetStream · FastAPI · PostgreSQL\nNeo4j · LanceDB · ONNX runtime"]
    NET["Byngonet Mesh\nNodeID · AISP · BSP post-quantum · offline mesh"]
    KRN["Linux 6.19.10 + AI Supervisor\nsorabbyngo_nodeid.ko · byngonet modules · SELinux"]
    HAL["Hardware Abstraction\nARM · x86 · RISC-V · sensors · radios"]

    UI --> NOV --> AI --> AGT --> SVC --> NET --> KRN --> HAL

    style UI  fill:#00001a,stroke:#4488ff,color:#88aaff
    style NOV fill:#00001a,stroke:#3377ee,color:#88aaff
    style AI  fill:#00001a,stroke:#2266dd,color:#88aaff
    style AGT fill:#00001a,stroke:#2255cc,color:#88aaff
    style SVC fill:#00001a,stroke:#1144bb,color:#7799ff
    style NET fill:#00001a,stroke:#1133aa,color:#7799ff
    style KRN fill:#00001a,stroke:#002288,color:#6688ff
    style HAL fill:#00001a,stroke:#001166,color:#5577ff
```

---

## OS surfaces

All surfaces share the Sorabbyngo AI Kernel as their root base.

| Surface | Repo | Target | Key requirement | Status |
|---|---|---|---|---|
| Sorabbyngo OS | `sorabbyngo-os` | Base / development | Current build target | 🔧 In development |
| Novela OS | `novela-os` | Medical / clinical | PREEMPT_RT real-time | 🔜 Planned |
| Byngo OS | `byngo-os` | Consumer devices | Android compatibility | 🔜 Planned |
| Edge OS | `edge-os` | IoT / drones / vehicles | Minimal footprint | 🔜 Planned |
| Sentinel OS | `sentinel-os` | Cybersecurity ops | Hardened kernel | 🔜 Planned |
| Server OS | `server-os` | Infrastructure | High throughput | 🔜 Planned |

---

## Model stack

| Tier | Models | Runtime | Condition |
|---|---|---|---|
| Tier 1 | Phi-4 Mini · SmolLM2 360M · meditron:7b · Aya 23 8B | Ollama | Always-on, no internet, 4–8GB RAM |
| Tier 2 | Phi-4 14B · Gemma 3 4B · Qwen 2.5 7B · Aya 23 35B | Ollama | GPU devices, 24GB+ VRAM |
| Tier 3 | Qwen 2.5 72B · Mistral Small 3.1 | Ollama | Performance devices |
| Tier 4 | DeepSeek-R1 70B · Mixtral 8x22B · LLaMA 3.1 70B | Byngox Prime vLLM | Self-hosted H100/A100 cluster |
| Medical | GatorTron · Meditron-70B · BioMistral · ClinicalT5 | ONNX + HuggingFace | Clinical intelligence |
| Embeddings | nomic-embed-text · mxbai-embed-large | Ollama | Vector memory |

> No external API keys. No data leaves Sorabbyngo infrastructure.

---

## Custom kernel modules

```mermaid
graph LR
    KRN["Linux 6.19.10"] --> NID["sorabbyngo_nodeid.ko\nHardware cryptographic identity\n/proc/sorabbyngo/nodeid"]
    KRN --> BNM["byngonet.ko\nIdentity-aware mesh networking\n/proc/byngonet_stats"]
    NID --> BSP["BSP post-quantum\nPeer verification before\nany traffic is accepted"]
    BNM --> BSP

    style KRN fill:#00001a,stroke:#4488ff,color:#88aaff
    style NID fill:#00001a,stroke:#2266dd,color:#88aaff
    style BNM fill:#00001a,stroke:#2266dd,color:#88aaff
    style BSP fill:#00001a,stroke:#1144bb,color:#7799ff
```

---

## Build phases

| Phase | Description | Status |
|---|---|---|
| 1 | Custom kernel — Linux 6.19.10 | ✅ Complete |
| 2 | Root filesystem — Buildroot | 🔜 Next |
| 3 | Init system — systemd | 🔜 Planned |
| 4 | AI runtime layer | 🔜 Planned |
| 5 | ISO / image builder | 🔜 Planned |
| 6 | Installer | 🔜 Planned |

---

## Build instructions

```bash
cp /boot/config-$(uname -r) .config
make olddefconfig LOCALVERSION=""
./scripts/kconfig/merge_config.sh .config sorabbyngo.config
make -j$(nproc) LOCALVERSION=""
sudo make modules_install LOCALVERSION=""
sudo cp arch/x86/boot/bzImage /boot/vmlinuz-6.19.10-sorabbyngo
sudo update-initramfs -c -k 6.19.10-sorabbyngo
sudo update-grub
```

---

## Repository structure
sorabbyngo-os/
├── board/              # Hardware board configs
│   └── sorabbyngo/     # Target board definitions
├── configs/            # Per-surface kernel defconfigs
│   ├── sorabbyngo_defconfig
│   ├── sorabbyngo_medical_defconfig
│   ├── sorabbyngo_sentinel_defconfig
│   ├── sorabbyngo_server_defconfig
│   ├── sorabbyngo_edge_arm_defconfig
│   └── sorabbyngo_consumer_defconfig
├── drivers/            # Custom kernel modules
│   ├── sorabbyngo_nodeid.ko
│   └── byngonet/
├── patches/            # Kernel patch set
└── sorabbyngo.config   # AI supervisor config fragment
---

## Related repositories

| Repo | Role |
|---|---|
| `sorabbyngo-patches` | Kernel patch set |
| `sorabbyngo-build` | Build system, ADRs, roadmap |
| `byngox` | AI reasoning engine |
| `chameha` | Voice and personality interface |
| `byngonet` | Mesh network protocol |
| `novela` | Health platform |
| `security` | 7-ring defence architecture |

---

<p align="center">
  <strong>Sorabbyngo Company</strong> · Kisii, Kenya · 2026<br/>
  <em>Rhythm of Digital Life</em>
</p>

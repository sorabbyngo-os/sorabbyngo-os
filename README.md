# SORABBYNGO OS
> **Africa-First AI Operating System** — Sorabbyngo Labs

A complete AI operating system built from the kernel up.

## Layers
| Layer | Component | Status |
|---|---|---|
| 0 | Linux 6.19.10 kernel + NodeID + Byngonet | Building |
| 1 | HAL — gRPC hardware abstraction daemon | Planned |
| 2 | Core Services — NATS + SurrealDB + Vault | Done |
| 3 | AI Runtime — ONNX + LanceDB | Planned |
| 4 | Byngox — 8-agent swarm engine | In development |
| 5 | CML — Safety and alignment | In development |
| 6 | Novela — AI health platform | In development |
| 7 | Chameha — Voice AI shell | In development |

## Quick Start
```bash
git clone https://github.com/sorabbyngo-Labs/sorabbyngo-os.git
cd sorabbyngo-os
docker-compose -f os/core-services/docker-compose.dev.yml up -d
python3 os/core-services/nats/setup_streams.py
```

**Sorabbyngo Company** — Kisii, Kenya | 2026

# ADR-009: Chameha Intelligence Tiers + Cloud Policy

## Status
Accepted

## Decision
Chameha uses a 3-tier intelligence model. Cloud is never automatic.

## Tiers

| Tier | Model | Trigger | Hardware |
|------|-------|---------|----------|
| 1 | Phi-4 Mini Q4 via Ollama | Always-on | 2GB RAM x86_64 |
| 2 | Mistral 7B Q4 | Auto when GPU detected | GPU required |
| 3 | External API (opt-in) | `/cloud <query>` only | Network + ENABLE_CLOUD_AI=true required |

## Cloud Policy
- Cloud is OFF by default, every session, no exceptions
- No automatic cloud calls under any condition
- When Tier 1/2 confidence is low, Chameha signals:
  "Deeper analysis available via external intelligence."
- User must explicitly type `/cloud <query>` to trigger Tier 3
- Data sent to Tier 3 is always sanitized — no raw telemetry,
  no NodeID, no behavioral logs, no user identity
- Cloud is disabled automatically in secure mode

## Rationale
- Africa-first offline operation — clinics in Kisii, Kakamega,
  Lodwar must function without internet
- Sentinel handles sensitive behavioral data — external leakage
  is an unacceptable attack surface
- Option C consent model prevents fatigue and session-level leakage
- Hybrid signal ("deeper analysis available") preserves system
  intelligence without compromising intentionality
- /cloud command establishes Chameha command interface precedent —
  foundation for full OS control plane

## Hardware Target
- Minimum: 4GB RAM, x86_64, 4-core CPU
- ARM support: Phase 2
- Phi-4 Mini Q4 runs in ~1.5GB RAM, inference under 1 second on target

## Consequences
- Ollama must be installed in the OS image
- Phi-4 Mini served via Ollama — pulled on first boot via ollama pull phi4-mini
- Tier 2 detection: check for CUDA/ROCm/Metal at boot
- No API keys stored in OS — user provides per session if Tier 3 is ever enabled
# ADR-009: Chameha Intelligence Tiers + Cloud Policy

## Status
Accepted

## Decision
Chameha uses a 3-tier intelligence model. Cloud is never automatic.

## Tiers

| Tier | Model | Trigger | Hardware |
|------|-------|---------|----------|
| 1 | Phi-4 Mini Q4 via Ollama | Always-on | 2GB RAM x86_64 |
| 2 | Mistral 7B Q4 | Auto when GPU detected | GPU required |
| 3 | External API (opt-in) | `/cloud <query>` only | Network + ENABLE_CLOUD_AI=true required |

## Cloud Policy
- Cloud is OFF by default, every session, no exceptions
- No automatic cloud calls under any condition
- When Tier 1/2 confidence is low, Chameha signals:
  "Deeper analysis available via external intelligence."
- User must explicitly type `/cloud <query>` to trigger Tier 3
- Data sent to Tier 3 is always sanitized — no raw telemetry,
  no NodeID, no behavioral logs, no user identity
- Cloud is disabled automatically in secure mode

## Rationale
- Africa-first offline operation — clinics in Kisii, Kakamega,
  Lodwar must function without internet
- Sentinel handles sensitive behavioral data — external leakage
  is an unacceptable attack surface
- Option C consent model prevents fatigue and session-level leakage
- Hybrid signal ("deeper analysis available") preserves system
  intelligence without compromising intentionality
- /cloud command establishes Chameha command interface precedent —
  foundation for full OS control plane

## Hardware Target
- Minimum: 4GB RAM, x86_64, 4-core CPU
- ARM support: Phase 2
- Phi-4 Mini Q4 runs in ~1.5GB RAM, inference under 1 second on target

## Consequences
- Ollama must be installed in the OS image
- Phi-4 Mini served via Ollama — pulled on first boot via ollama pull phi4-mini
- Tier 2 detection: check for CUDA/ROCm/Metal at boot
- No API keys stored in OS — user provides per session if Tier 3 is ever enabled

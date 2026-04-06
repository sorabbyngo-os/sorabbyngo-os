# SORABBYNGO NATS Subject Hierarchy

## Naming Convention
{service}.{domain}.{action}

## Health
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| health.query.triage | Chameha | Byngox | Triage request |
| health.result.triage | Byngox | CML | Result before safety check |
| health.result.safe | CML | Chameha | Cleared result to user |
| health.alert.emergency | Byngox | Chameha, Novela | Emergency alert |

## Drug Verification
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| drug.verify.request | Chameha | Byngox.drug_agent | Verify medication |
| drug.verify.result | Byngox | CML | Result before safety check |
| drug.verify.safe | CML | Chameha | Cleared result |
| drug.report.counterfeit | Novela | Byngox.population_agent | Counterfeit confirmed |

## Population Health
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| population.signal | Novela | Byngox.population_agent | Health signal |
| population.alert.outbreak | Byngox | Chameha, Novela | Outbreak detected |
| population.heatmap.update | Byngox | Novela | Heatmap data |

## System
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| system.nodeid.ready | HAL | All | NodeID ready at boot |
| system.health | All | Monitor | Heartbeats |
| system.shutdown | OS | All | Graceful shutdown |

## Learning
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| learn.feedback | Chameha | Byngox.learning_agent | User feedback |
| learn.model.update | PRIME | Byngox | New weights available |

## Byngonet Mesh
| Subject | Publisher | Subscriber | Purpose |
|---|---|---|---|
| byngonet.peer.discovered | Byngonet | Byngox.network_agent | New peer found |
| byngonet.task.dispatch | Byngox | Byngonet | Dispatch AI task |
| byngonet.task.result | Byngonet | Byngox | Result from mesh |

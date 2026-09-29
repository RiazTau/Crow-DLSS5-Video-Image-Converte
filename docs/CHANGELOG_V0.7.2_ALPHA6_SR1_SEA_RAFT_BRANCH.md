# V0.7.2-alpha6-sr1 — SEA-RAFT Neural Motion Backend Branch

## Scope
Experimental branch from alpha6. Mainline NVOF remains intact and default-compatible.

## Added
- `TemporalMode::SeaRaft` and GUI option `SEA-RAFT - Neural HQ (CUDA)`.
- Persistent `SeaRaftFlowSession` C++ <-> Python binary pipe.
- Official SEA-RAFT checkout/runtime setup bootstrap under `sea_raft/`.
- Current->previous flow convention without vector inversion.
- Learned SEA-RAFT uncertainty mapping into Crow confidence / motionUncertainty.
- Optional Crow FG 3F/5F Temporal Motion Consensus after SEA-RAFT.
- SEA-RAFT runtime log: `video/logs/sea-raft-video.log`.

## Deliberately not applied to SEA-RAFT
- NVOF Full + 1/2 + 1/4 spatial consensus.
- NVOF medoid/dominant-mode repair.
- NVOF robust affine fallback.
- NVOF Cost / Output Grid / Temporal Hints controls.

This keeps the branch useful for a fair NVOF-alpha6 vs SEA-RAFT backend A/B instead of passing neural flow through heuristics designed to rescue NVOF.

# SEA-RAFT Runtime (sr2)

Build Full/Portable now installs this runtime automatically. The GUI Setup button remains available for repair/reinstall.

- CUDA PyTorch is installed separately from ordinary requirements so a CPU wheel cannot silently overwrite it.
- Mainland-China builds use GitCode + hf-mirror/Alibaba PyPI where applicable and fall back to official sources.
- Spring-S and Spring-M are supported. Default: Spring-M.
- UI exposes model, inference scale (quarter/half/full), refinement iterations (1-12), neural-flow trust and uncertainty sensitivity.
- Official SEA-RAFT source/models remain external BSD-3-Clause dependencies and are fetched at setup time.

Crow motion convention remains Current -> Previous.

Default Hugging Face models:
- MemorySlices/Tartan-C-T-TSKH-spring540x960-S
- MemorySlices/Tartan-C-T-TSKH-spring540x960-M

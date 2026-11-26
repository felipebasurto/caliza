# Transitional Orbis platform layer

This directory is not a PS5 product target. It is the inherited Orbis platform layer currently used by the Caliza build. The name is historical and intentionally documented in `../REPO-MAP.md` because moving it while interface integration is in progress would break active work.

- `coreorbis/` contains boot, threading, memory, controller, audio, storage, NFS, logging and instrumentation adaptations required by the PS4 package.
- `frontend/` is transitional. Retained services will move behind explicit Caliza contracts; inherited presentation must not ship.
- `third_party/` contains console-port dependencies.

Planned cleanup names are `orbis/core/`, `orbis/services/` and `orbis/third_party/` after the new interface is integrated and validated. Technical lineage notes that remain useful live in `../ps4/docs/lineage/`.

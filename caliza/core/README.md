# Caliza core backend, storage and migration slice

This directory slice implements the portable storage-facing half of the `contract.h` boundary. It is
intentionally not wired into the PS4 Makefile or boot flow; the orchestrator owns that integration.

## Components

- `fake_backend.h/.cpp` — thread-safe in-memory `Backend` implementation for host previews and contract
  tests. It implements every command currently declared in `contract.h`, ordered command execution,
  monotonic revisions, scan progress/cancellation, volume disconnect/reconnect simulation, damaged images,
  absent BIOS, folder browsing/filtering/pagination, memory cards, settings, controls, accessibility state,
  VM state transitions, service operations and achievement state. `FailNextCommand` injects deterministic
  command failures.
- `backend.h/.cpp` — POSIX storage/catalog backend. It discovers storage, persists saved scan sources,
  browses folders read-only and scans images incrementally on worker threads. Emulator, network, texture-pack,
  Smart Tune, companion-panel and achievement commands currently fail explicitly with
  `console platform adapter is not bound`; they are not faked as successful here.
- `storage.h/.cpp` — dependency-free POSIX storage service. It probes `/data/caliza`, `/mnt/usb0`-`/mnt/usb7`
  and `/mnt/ext0`-`/mnt/ext1` without writing to volumes, uses `opendir`/`open` readability probes, records
  USB versus extended-storage evidence, accepts durable volume identity only from an explicit platform
  callback, and otherwise keeps fingerprints non-authoritative. Ambiguous fingerprint evidence never causes
  silent reassociation to another slot. On `ORBIS_PS4`, `statvfs` is deliberately not called because the
  existing frontend documented console crashes in that libc path; capacity probing needs proper testing through
  a libkernel adapter.
- `data_root.h/.cpp` — canonical data-root layout plus the resumable legacy-to-Caliza migration library.
  The source root is never deleted, moved or content-modified. INI text is translated into the canonical
  Caliza key/root form in the destination copy only.

## Saved scan sources

`StorageBackend` persists user-added folder links in `<data-root>/library/sources.ini` using atomic
rewrite. The store records the absolute path, volume root, relative path, durable identity when one exists,
and the non-authoritative fingerprint. It handles duplicate paths, relocation, missing volumes and scan
metadata across process restarts. Removing a record only unlinks the saved configuration; it never deletes
or moves game images. A malformed existing store is not overwritten implicitly.

## Scanning

Scanning is incremental, cooperative and off the caller thread. Partially discovered games can appear before
completion; cancellation and root disappearance produce explicit terminal states. A completed scan removes
stale catalog entries for that source, while cancelled/failed scans preserve partial entries.

The default `PosixImageInspector` recognizes `.iso`, `.chd`, `.cso`, `.zso`, `.bin`, `.img` and `.elf`. It
validates ELF/CHD/CSO/ZSO magic and ISO9660 descriptors, including the raw CD sector layouts used by the
existing frontend. Plain ISO/BIN/IMG images get dependency-free `SYSTEM.CNF` serial extraction. CHD/CSO/ZSO
serial extraction requires libchdr/lz4/zlib and is deliberately left to the orchestrator through the
`ImageInspector` adapter boundary. Scanning never copies, moves, formats or writes images.

## Migration safety contract

Preserved user data categories are `bios/`, `memcards/`, `savestates/`, `settings/`, `patches/`, `cheats/`,
`covers/`, `lang/`, `flags/`, `inputprofiles/`, top-level `*.ini`, `playtime.dat` and `lastgame.txt`.
Dotfiles inside preserved directories are user data and are included.

`games/` and `textures/` are explicit large-data exceptions:

- `ReferenceOnly` leaves them in place and inventories them in the manifest;
- `HardlinkVerified` links them only when same-device/same-inode verification succeeds, with no byte-copy
  fallback and therefore no silent duplication.

Regular files are copied through a temporary file, flushed, and published by atomic no-overwrite hardlink
publication. If the destination filesystem cannot hardlink, migration fails closed rather than using an
overwrite-capable rename race. Existing differing destination files are collisions and are never overwritten;
identical files make retries idempotent. Source and destination symlinks are rejected/not followed. Appended
journal records and the manifest are enough to inspect and retry after interruption or power loss. Retry is
by design idempotent even when a crash happened between publication and journaling.

INI translation has a conservative 64 MiB per-file limit; larger INI files are reported as read errors rather
than silently copied untranslated. Component-level symlink checks are race-limited by POSIX itself: an
unrelated writer concurrently mutating the destination tree is outside the migration lock's protection.
OpenOrbis hardlink, `fcntl` locking and directory-fsync behavior needs proper testing on physical console
storage before release integration.

## Host test commands

From the repository root:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  tests/caliza/test_contract.cpp caliza/core/fake_backend.cpp caliza/core/storage.cpp caliza/core/data_root.cpp \
  -o /tmp/caliza-test-contract
/tmp/caliza-test-contract

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  tests/caliza/test_storage.cpp caliza/core/backend.cpp caliza/core/storage.cpp caliza/core/data_root.cpp \
  -o /tmp/caliza-test-storage
/tmp/caliza-test-storage

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  tests/caliza/test_migration.cpp caliza/core/data_root.cpp caliza/core/storage.cpp \
  -o /tmp/caliza-test-migration
/tmp/caliza-test-migration
```

The tests are standalone because `ps4/tools/run-host-tests.sh` is outside this slice's ownership. They cover
ordered commands, scan cancellation, disconnect during scan, unreadable/empty/duplicate/long-name folders,
folder browsing pagination/filtering, saved-source restart persistence, migration fresh copy, interruption
and retry, collision preservation, source/destination symlink rejection, verified hardlinks, missing source,
source preservation, dotfile preservation and INI compatibility translation.

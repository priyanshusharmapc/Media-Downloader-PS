# Archive Mode local offload snapshot

Date: 2026-09-14
Baseline commit: `bad82c16508cdf7ec5daffb9bc92c90271a008f7`
Target branch: `archive-mode-v1`
Status: WIP source snapshot, not release-qualified

This directory exists only to preserve an unpublished local implementation before the development conversation branches. It is not canonical project documentation. Canonical semantic/project documentation remains in Google Drive.

## Payload

- `2026-09-14-archive-mode-local.patch.gz`
  - gzip-compressed unified diff from the exact baseline source tree at `bad82c16508cdf7ec5daffb9bc92c90271a008f7`
  - uncompressed patch SHA-256: `8d0ee6a8219f96a459e431b9bf3fe8ed2bbad11cb127635ae66940f35e4e10a7`
  - compressed payload SHA-256: `6bf7f5becaca8dfeabd4deb861ba2e4a4d4a53eb02942076a955cf74b9cd20ce`

## Changed/new source represented by the patch

- `CMakeLists.txt`
- `src/tabmanager.h`
- `src/tabmanager.cpp`
- `src/archive/archivecore.h`
- `src/archive/archivecore.cpp`
- `src/archive/archivetab.h`
- `src/archive/archivetab.cpp`
- `src/archive/archivecli.cpp`
- `src/archive/archive.qrc`
- `tests/archive-core-tests.cpp`
- `resources/archive/ARCHIVE_AGENT.md`
- `resources/archive/recovery-package.schema.json`
- `resources/archive/recovery-package.example.json`

## Implemented WIP areas

The snapshot contains an additive Archive tab and Archive core scaffolding covering archive-root-relative paths, source registry and canonical item persistence, complete/partial snapshot reconciliation, removed/reappeared membership history, unavailable recovery status, projections (`catalog.csv`, `missing.csv`, M3U8), Recovery Package validation/import plumbing, Activity/Diagnostic logging, media verification/execution plumbing, a small `archive-cli validate` command, and the packaged `ARCHIVE_AGENT.md`/schema/example resources.

## Local evidence before offload

The Linux Qt6 source tree built successfully after the latest fix:

- `archive-core-tests`: PASS
- `archive-cli`: build PASS
- `media-downloader`: build PASS

Latest deterministic test run: `100% tests passed, 0 tests failed out of 1`.

The test currently checks at least:

- archive layout creation;
- relative path acceptance and Windows absolute/path-traversal rejection;
- source registry round-trip;
- interrupted representation recovery;
- partial/HTTP-429 snapshot cannot infer removals;
- complete-snapshot absence becomes removed membership;
- removed item can reappear with `membership_reappeared` history;
- deleted missing item becomes a recovery candidate;
- same source ID across playlists reuses canonical identity;
- `catalog.csv`, `missing.csv`, and M3U8 projection generation;
- valid Recovery Package validation and absolute package path rejection;
- Activity log retention at no more than seven activity-date directories;
- Diagnostic log rolling window remaining near the 10 MB cap.

## Latest bug fixed locally

A reconciliation fixture originally failed because a reappearing playlist item without an existing canonical row was incorrectly emitted as `first_seen`. The WIP patch now bases membership-history classification on prior playlist membership, not canonical-row existence, and ensures a removed historical playlist item is promoted to canonical state when necessary. The post-fix deterministic suite passes.

## Not yet qualified

Do not treat this snapshot as release-ready. Remaining work includes Windows CI against the applied feature source, deeper deterministic/adversarial fixtures, real yt-dlp/FFmpeg/ffprobe end-to-end tests, portable package verification, GUI regression/smoke testing, local Windows/Kilo qualification, and final Drive state reconciliation.

## Resume procedure

1. Check out `archive-mode-v1` at the commit containing this offload artifact.
2. Extract `wip/offload/2026-09-14-archive-mode-local.patch.gz`.
3. Verify the gzip SHA-256 above.
4. Apply it from repository root against baseline `bad82c16508cdf7ec5daffb9bc92c90271a008f7`, for example with `patch -p1` after inspecting paths.
5. Build with Qt6 and run `ctest --output-on-failure`.
6. Once confirmed, commit the actual source files and delete this temporary offload artifact in the same or a subsequent cleanup commit.
7. Continue from the Google Drive Development Record and Current State documents, not from assumptions in this README.

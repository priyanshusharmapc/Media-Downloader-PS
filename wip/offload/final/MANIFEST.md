# Media Downloader PS Archive Mode local implementation offload

Date: 2026-09-14
Purpose: durable handoff of all non-reproducible local Archive Mode source changes and verification evidence before conversation rollover.

## Authoritative implementation baseline

Apply the reconstructed patch to repository `priyanshusharmapc/Media-Downloader-PS` at commit:

`bad82c16508cdf7ec5daffb9bc92c90271a008f7`

The working implementation branch is `archive-mode-v1`. The preservation branch is `archive-mode-v1-offload`.

## Reliable source payload

The earlier file `wip/offload/2026-09-14-archive-mode-local.patch.gz` is known to be truncated and MUST NOT be used for reconstruction.

The reliable source payload is base64-split across:

- `wip/offload/final/patch10-00.b64`
- `wip/offload/final/patch10-01.b64`
- `wip/offload/final/patch10-02.b64`
- `wip/offload/final/patch10-03.b64`

Reconstruct it with:

```bash
cat wip/offload/final/patch10-*.b64 | tr -d '\r\n' | base64 -d > archive-mode-local-complete.patch.gz
sha256sum archive-mode-local-complete.patch.gz
gzip -dc archive-mode-local-complete.patch.gz > archive-mode-local-complete.patch
sha256sum archive-mode-local-complete.patch
```

Expected SHA-256:

- compressed patch: `9738ede6f91d029fee4530a0103d2680e1766132542192fca83e38d36680b876`
- uncompressed patch: `213f63fdff948af9f675c23d6f7f5fd18d1be1e8a43240f24487f76b21a455f2`

Expected sizes:

- compressed patch: 29,214 bytes
- uncompressed patch: 122,195 bytes

The patch contains exactly the local implementation delta for:

- `CMakeLists.txt`
- `resources/archive/ARCHIVE_AGENT.md`
- `resources/archive/recovery-package.example.json`
- `resources/archive/recovery-package.schema.json`
- `src/archive/archive.qrc`
- `src/archive/archivecli.cpp`
- `src/archive/archivecore.cpp`
- `src/archive/archivecore.h`
- `src/archive/archivetab.cpp`
- `src/archive/archivetab.h`
- `src/tabmanager.cpp`
- `src/tabmanager.h`
- `tests/archive-core-tests.cpp`

## Verification evidence

Raw local evidence is preserved directly under `wip/offload/final/evidence/`:

- `LOCAL-STATE-MANIFEST.txt`
- `configure.log`
- `build.log`
- `LastTest.log`
- `gui-smoke.err`
- `local-build-binary-sha256.txt`

`gui-smoke.out` was empty locally and therefore carries no additional information.

A previous attempt to preserve this evidence as a single base64 tar payload was discarded after validation failed. The raw evidence files above are the authoritative evidence copy.

## Known local qualification state at offload

Local Linux Qt6 work built these targets successfully:

- `media-downloader`
- `archive-cli`
- `archive-core-tests`

CTest result:

- `archive-core`: PASS
- 100% tests passed
- 0 failed out of 1

The deterministic core test covers archive layout, relative-path safety, Windows absolute-path rejection, traversal rejection, source-registry round trip, interrupted-state recovery, partial/429 removal protection, complete-snapshot removal, reappearance, deleted-item recovery candidacy, same-ID reuse across playlists, generated catalog/M3U8 projections, Recovery Package validation including absolute-path rejection, seven-activity-day retention, and diagnostic-size rotation.

The local work is NOT a final release qualification. Windows feature CI, portable feature packaging, real yt-dlp/FFmpeg/FFprobe execution, GUI regression/smoke qualification, real playlist behavior, and final Windows harness iteration remain.

## Next safe action

1. Verify all final chunks using the `Archive Mode final offload verify` workflow or the commands above.
2. Restore a fresh checkout at `bad82c16508cdf7ec5daffb9bc92c90271a008f7`.
3. Reconstruct and apply `archive-mode-local-complete.patch`.
4. Build `media-downloader`, `archive-cli`, and `archive-core-tests` and rerun `ctest --output-on-failure`.
5. Commit the actual reconstructed source files normally onto `archive-mode-v1`.
6. Run fork-specific Linux and Windows Qt6 CI on that implementation branch.
7. Produce and inspect the Windows portable candidate, including `ARCHIVE_AGENT.md`, Recovery Package schema/example, and `archive-cli.exe`.
8. Hand the candidate to the local Windows/Kilo harness for run-observe-repair qualification.
9. Reconcile Google Drive project documentation after each accepted engineering milestone.

Do not modify production archive media during development qualification.

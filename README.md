# Media Downloader PS

Media Downloader PS is a Qt/C++ fork of Media Downloader with a loss-resistant Archive Mode for preserving playlist history, canonical media, metadata, recovery evidence, and operational state over time.

The upstream project remains a general-purpose graphical frontend for tools such as yt-dlp, gallery-dl, you-get, svtplay-dl, aria2c, wget, and related extensions. This fork adds a separate Archive Mode designed for long-lived collections where deleted, private, removed, unavailable, or later-recovered items must remain historically traceable.

## Archive Mode readiness

Do not use hardcoded commit IDs, workflow run IDs, artifact IDs, or package digests from documentation as acceptance evidence.

For any candidate package:

1. read the package's `build-identity.json`;
2. confirm the exact package commit has a successful Archive Qt6 qualification run on Linux and Windows;
3. verify the sealed package with its own `SHA256SUMS.txt`;
4. run the target Windows local harness with `-ExpectedCommit` set from that package's identity;
5. preserve the resulting local-harness evidence receipt.

The package itself and the CI run for its exact commit are the current sources of qualification truth.

## Archive Mode documentation

Start here:

- [Archive Mode documentation index](docs/README.md)
- [Archive Mode architecture and invariants](docs/ARCHIVE_MODE.md)
- [Operations and troubleshooting](docs/ARCHIVE_OPERATIONS.md)
- [Recovery package workflow](docs/ARCHIVE_RECOVERY.md)
- [Testing, qualification, and acceptance](docs/ARCHIVE_TESTING.md)
- [Archive Agent interoperability contract](resources/archive/ARCHIVE_AGENT.md)

## Archive Mode quick start

For a CI-qualified Windows portable candidate, extract the ZIP into a fresh directory and keep the Archive Root outside the portable package. Use an empty Archive Root for the first target-host qualification.

Read the package identity:

```powershell
$identity = Get-Content .\build-identity.json -Raw | ConvertFrom-Json
$expectedCommit = $identity.commit
$identity
```

Confirm that `qualification` is `windows-ci-qualified-for-local-harness`, then run:

```powershell
.\archive-local-harness.ps1 `
  -ArchiveRoot 'C:\ArchiveHarness' `
  -PlaylistUrl '<REAL PLAYLIST URL>' `
  -VideoUrl '<REAL VIDEO URL>' `
  -ExpectedCommit $expectedCommit
```

The harness verifies package identity and sealed files before execution, runs Archive preflight, performs a complete playlist scan, syncs and verifies the exact requested item, repeats the operation, confirms canonical media hashes are unchanged, and writes a dated evidence receipt into the Archive Root.

Do not use `-AllowExistingArchive` for the first acceptance run unless you intentionally want to test against existing state.

## Archive Mode command-line interface

The portable candidate includes `archive-cli.exe`.

```text
archive-cli preflight <archive-root>
archive-cli validate <archive-root> <package-dir>
archive-cli ingest-pending <archive-root>
archive-cli scan <archive-root> <youtube-playlist-url> [display-name]
archive-cli sync-item <archive-root> <youtube-video-url>
archive-cli verify-item <archive-root> <youtube-video-url>
```

`scan` returns exit code 0 only for a complete discovery snapshot. An incomplete discovery does not infer historical removal. Invalid YouTube identity input is rejected before archive mutation.

## Archive design principles

Archive Mode follows a few strict rules:

1. Historical playlist membership is durable. Removed or unavailable items are not erased from history.
2. Canonical archive state is authoritative. CSV, M3U8, and JSONL reports are generated projections, not mutation inputs.
3. Canonical media is not overwritten casually. Normalization and recovery stage and verify output before promotion.
4. Recovery is additive. External tools and agents submit Recovery Packages through `State/ArchiveMode/Imports/Pending/`; they do not edit canonical state directly.
5. State mutation is serialized and journaled. Interrupted multi-file commits can roll forward safely, while conflicting preimages are preserved for diagnosis.
6. Media marked complete is verified against real files and FFprobe, not trusted solely because state says `complete`.
7. Portable acceptance is commit-bound and hash-bound. A stale, mixed, or tampered package must not produce a successful local-harness result.

See [docs/ARCHIVE_MODE.md](docs/ARCHIVE_MODE.md) for the full model.

## Building the Archive candidate

A representative Qt6 development build is:

```sh
cmake -S . -B build -G Ninja \
  -DBUILD_WITH_QT6=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The application-level Archive integration suite requires Python 3.11 or newer and real FFmpeg/FFprobe. `ARCHIVE_TEST_FFMPEG` and `ARCHIVE_TEST_FFPROBE` can override the media tool locations.

## Upstream Media Downloader

This repository is derived from [mhogomchungu/media-downloader](https://github.com/mhogomchungu/media-downloader). Upstream Media Downloader provides the general GUI downloader functionality, extension model, translations, and platform packaging foundation used by this fork.

For upstream usage information, extension documentation, FAQs, and official upstream releases, consult the upstream repository and wiki. Fork-specific Archive Mode behavior is documented in this repository because it has additional state, safety, recovery, qualification, and acceptance semantics that do not belong to upstream documentation.

## Legal and operational responsibility

Use the software only for media and sources you are authorized to access and archive, and in compliance with applicable law and service terms. Archive Mode is designed to preserve evidence and historical state; it does not grant access to content you are not authorized to obtain.

## License

This project retains the licensing terms of the upstream project. See `LICENSE`, `LICENSE.txt`, `GPLv2`, and `GPLv3` in the repository.
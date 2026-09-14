# Archive Mode architecture and invariants

Archive Mode is a loss-resistant archival subsystem layered into Media Downloader PS. Its purpose is not simply to download the current contents of a playlist. It preserves durable historical identity, playlist membership, canonical media state, provenance, recovery evidence, and operational history even when upstream content later disappears or changes.

## 1. Goals

Archive Mode is designed to:

- preserve the fact that an item was observed in a playlist even after it is removed;
- retain canonical identity when an original upload becomes deleted, private, login-restricted, geo-blocked, copyright-blocked, or otherwise unavailable;
- keep video and audio representation state independently visible;
- generate human-readable projections without making those projections authoritative;
- support safe external recovery without allowing external tools to mutate canonical state directly;
- survive interrupted multi-file state publication without silently producing a mixed archive;
- reject ambiguous, malformed, linked, escaping, or conflicting filesystem inputs;
- verify canonical media using FFprobe rather than trusting filenames or state labels alone;
- produce deterministic evidence for CI and target-host acceptance.

## 2. Non-goals

Archive Mode does not guarantee that every unavailable historical item can be recovered. It does not prove that a recovered mirror is semantically identical to the original upload. It does not bypass authentication, rights controls, service restrictions, or applicable law. It also does not claim exhaustive qualification of every inherited downloader extension in the upstream application.

## 3. Canonical identity

For a known YouTube video, the canonical key is:

```text
youtube:<11-character-video-id>
```

The original YouTube identity is retained even if media is later recovered from a different source. A mirror or reupload is evidence supporting the historical object, not a replacement identity.

Playlist occurrences are distinct from canonical media identity. Duplicate appearances of the same canonical video can have separate occurrence identities and positions while still pointing to one canonical item.

## 4. Archive Root layout

A new Archive Root is initialized beneath an operator-chosen directory. Important paths include:

```text
<ArchiveRoot>/
├── ARCHIVE_AGENT.md
├── Video/
├── Audio/
├── Metadata/
├── Temp/
├── Playlists/
│   └── <source-key>/
│       ├── playlist.json
│       ├── items.json
│       ├── history.jsonl
│       ├── catalog.csv
│       ├── missing.csv
│       ├── video.m3u8
│       ├── audio.m3u8
│       ├── unavailable.jsonl
│       └── removed.jsonl
└── State/
    ├── video-archive.txt
    ├── audio-archive.txt
    └── ArchiveMode/
        ├── sources.json
        ├── items.json
        ├── sync.lock
        ├── Schemas/
        │   ├── recovery-package.schema.json
        │   └── recovery-package.example.json
        ├── Imports/
        │   ├── Pending/
        │   ├── Accepted/
        │   └── Rejected/
        └── Logs/
            ├── Activity/
            └── Diagnostic/
```

The runtime can create additional transaction or recovery-control files inside Archive state. These are implementation-owned and must not be deleted merely to force a retry.

## 5. Canonical state versus projections

Canonical truth lives in state owned by the application, including:

- `State/ArchiveMode/sources.json`
- `State/ArchiveMode/items.json`
- per-playlist `items.json`
- per-playlist durable `history.jsonl`
- representation state and paths
- accepted recovery receipts and evidence

Generated projections include:

- `catalog.csv`
- `missing.csv`
- `video.m3u8`
- `audio.m3u8`
- `unavailable.jsonl`
- `removed.jsonl`

Projections are for reading, reporting, playback, and external research. Editing them never changes canonical truth. Formula-like CSV values are neutralized and M3U text is sanitized so report content cannot inject spreadsheet formulas or extra playlist records.

## 6. Representation state

Video and audio are tracked separately. A canonical item can therefore have one representation complete while the other remains missing, failed, interrupted, or blocked by source availability.

The implementation recognizes representation states such as:

- `missing`
- `running`
- `complete`
- `failed`
- `interrupted`
- `blocked_unavailable`

An item's broader recovery need is derived from both representations and source availability. A video-only recovery does not hide missing audio, and an audio-only recovery does not claim the full item is complete.

## 7. Discovery semantics

Playlist discovery is deliberately conservative. Historical removal is inferred only from a complete, internally consistent discovery snapshot.

A snapshot is not considered complete merely because yt-dlp produced some JSON. The parser and reconciler require a valid source identity, a usable entries array, consistent declared counts, successful process execution, and no evidence that the response is partial or erroneous.

If discovery is incomplete:

- already-known historical membership is preserved;
- missing observations are not treated as removals;
- the scan reports incomplete status;
- the CLI uses a non-zero exit status for the incomplete result.

This prevents temporary network, extractor, rate-limit, login, or parse failures from erasing historical truth.

## 8. Media compatibility contract

Canonical recovered or normalized video is accepted only when it satisfies the Archive compatibility contract. The current contract requires an MP4-family video representation with H.264 video, yuv420p pixel format, bounded dimensions, positive duration, and AAC audio when an audio stream is present.

Canonical audio is an M4A/MP4-family audio representation with AAC audio and positive duration. Audio-only output must not contain moving video.

Media verification uses FFprobe and evaluates the relevant streams. A state value of `complete` is insufficient if the referenced file is missing, empty, corrupt, incompatible, or no longer probes successfully.

## 9. Safe normalization

Normalization is staged. The original candidate is not deleted before a replacement is successfully created and verified. Canonical destinations are not blindly overwritten.

This applies to both automatic download repair and external recovery. A failed normalization should leave the source evidence available for diagnosis or retry rather than destroying the only usable copy.

## 10. State transactions

Archive state often requires several files to move together. Publication therefore uses a durable transaction model with hashed preimages and after-images.

Important properties are:

- all expected preimages are validated before publication;
- an interrupted transaction can roll forward idempotently on the next initialization;
- a changed preimage causes conflict refusal instead of silent overwrite;
- media promotion and state updates for recovery are bound into one transaction boundary;
- projections can be rebuilt from canonical state after recovery.

The transaction journal is application-owned evidence. Do not manually edit or delete it when diagnosing an interrupted operation.

## 11. Writer serialization

Archive mutation is serialized by a root-level synchronization lock. GUI and CLI operations share the same lock semantics.

The implementation does not expire a lock merely because it is old. A live long-running writer must not be displaced by a second writer. Process-death recovery is supported so a stale lock from a terminated process does not permanently disable the archive.

If a command reports that another writer owns the archive, stop the second writer and diagnose the first process rather than deleting lock files while a process may still be active.

## 12. Filesystem safety

Archive Mode rejects unsafe relative paths and linked path boundaries. Recovery package paths are package-relative and must not contain traversal, absolute paths, drive letters, alternate data streams, reserved Windows names, symbolic links, junctions, or path escapes.

Canonical Archive paths are stored as Archive Root relative paths where appropriate. Machine-specific absolute paths are runtime details and should not become portable archive identity.

Path checks are intentionally conservative. They reduce accidental and malicious package escapes, but they are not intended to defeat a hostile local administrator who can modify the filesystem concurrently with the process.

## 13. Recovery boundary

External agents, scripts, researchers, and harnesses may read archive state and perform open-ended research, but they do not own canonical mutation.

The supported boundary is a Recovery Package under:

```text
State/ArchiveMode/Imports/Pending/<package-id>/
```

The application validates identity, paths, schema, media, destination safety, and conflicts before promotion. Successful packages move to `Accepted`; schema or identity failures move to `Rejected`; retryable pre-promotion operational failures can remain `Pending`.

See [ARCHIVE_RECOVERY.md](ARCHIVE_RECOVERY.md) and [../resources/archive/ARCHIVE_AGENT.md](../resources/archive/ARCHIVE_AGENT.md).

## 14. Logging and diagnostics

Archive Mode maintains structured activity logs and bounded diagnostic logs under `State/ArchiveMode/Logs/`.

Diagnostics are redacted recursively for credential-like values. Large diagnostic entries are bounded, and diagnostic files rotate. Logs remain evidence, but operators should still avoid intentionally placing authentication secrets in recovery manifests, notes, URLs, or command-line arguments.

## 15. Command-line contract

The supported Archive CLI commands are:

```text
archive-cli preflight <archive-root>
archive-cli validate <archive-root> <package-dir>
archive-cli ingest-pending <archive-root>
archive-cli scan <archive-root> <youtube-playlist-url> [display-name]
archive-cli sync-item <archive-root> <youtube-video-url>
archive-cli verify-item <archive-root> <youtube-video-url>
```

### `preflight`

Resolves and starts yt-dlp, FFmpeg, FFprobe, and Deno. A successful preflight confirms the runtime tools can launch. It does not prove real YouTube access.

### `validate`

Validates one Recovery Package without canonical promotion.

### `ingest-pending`

Processes Recovery Packages already present under `Imports/Pending`.

### `scan`

Discovers a YouTube playlist and reconciles it against historical state. A complete snapshot returns success. An incomplete snapshot is preserved as incomplete and must not imply removals.

### `sync-item`

Creates the canonical item if needed, obtains the requested video and audio representations, normalizes where necessary, updates state, and then verifies both canonical representations.

### `verify-item`

Re-reads canonical state and verifies the exact requested canonical video's current video and audio files.

## 16. Portable package identity

The Windows qualification package includes:

- `build-identity.json`
- `SHA256SUMS.txt`
- `PORTABLE_MANIFEST.txt`
- `archive-local-harness.ps1`
- `archive-cli.exe`
- the GUI executable
- packaged yt-dlp, Deno, FFmpeg, and FFprobe runtime components

The local harness verifies the expected commit and all sealed package bytes before executing Archive operations. Extra unsealed files in the extracted package directory cause refusal. This is intentional so a mixed or modified package cannot be mistaken for the CI-qualified candidate.

Runtime versions are package-specific. Inspect `RUNTIME_VERSIONS.txt` rather than relying on a version copied into documentation.

## 17. Acceptance boundary

Hosted CI establishes reproducible build and product-test evidence. The final pre-live gate is the local Windows harness using real YouTube inputs on the target host.

A passing local harness proves, for the chosen playlist and item on that host, that:

- the package identity matched the expected commit;
- all sealed package files matched their hashes;
- runtime preflight passed;
- playlist discovery completed and observed at least one item;
- the exact requested item synced;
- its video and audio representations verified;
- an immediate rerun was idempotent;
- canonical media hashes did not change on the rerun;
- a local evidence receipt was written.

It does not prove every playlist, authentication mode, geographic environment, network failure, or future extractor version.

## 18. Qualification source of truth

Archive Mode deliberately avoids a hardcoded "current qualified commit" in living documentation.

For any candidate, qualification is established by the combination of:

- the exact source/package commit in `build-identity.json`;
- a successful Archive Qt6 qualification run for that exact commit;
- the package's own `SHA256SUMS.txt` and `RUNTIME_VERSIONS.txt`;
- the target-host local-harness evidence receipt for the selected package.

See [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md) for the required test layers and acceptance gates.
# Media Downloader PS Archive Agent Contract

Contract version: 1.1 (package schema remains version 1)

## Purpose

This file is the local interoperability contract for any coding harness, LLM agent, recovery script, or human-assisted automation that contributes evidence or recovered media to a Media Downloader PS Archive Root.

The archive is loss-resistant. A playlist item is a durable historical object even when its original YouTube upload becomes deleted, private, removed from a playlist, inaccessible, or otherwise unavailable.

External agents may perform open-ended recovery research. Media Downloader PS alone owns canonical archive mutation.

## Authority boundary

You MAY:

- read Archive Mode state and generated reports;
- read `catalog.csv` and `missing.csv`;
- inspect historical metadata and playlist history;
- search the web, old disks, alternate uploads, mirrors, backups, or other evidence sources;
- download candidate media into an isolated workspace;
- analyze candidate media and metadata;
- create a Recovery Package under `State/ArchiveMode/Imports/Pending/<package-id>/`;
- run the packaged validator when available;
- add evidence files inside the Recovery Package.

You MUST NOT:

- overwrite or delete anything in `Video/`;
- overwrite or delete anything in `Audio/`;
- rewrite existing automatic metadata trees;
- edit `State/video-archive.txt` or `State/audio-archive.txt`;
- fabricate entries in the automatic R9 video/audio catalogs;
- directly edit canonical `State/ArchiveMode/items.json`;
- directly edit canonical playlist `items.json` or durable `history.jsonl`;
- change a missing/unavailable/deleted/private record to recovered merely by editing a CSV projection;
- use an absolute machine-specific path in a Recovery Package;
- overwrite an already-complete canonical representation;
- delete historical evidence because a source disappears.

All external contribution is additive at the submission boundary.

## Recovery Package location

Create one directory per proposal:

```text
State/ArchiveMode/Imports/Pending/<package-id>/
```

Minimum layout:

```text
<package-id>/
├── manifest.json
├── files/
│   ├── recovered-video.ext       # optional
│   └── recovered-audio.ext       # optional
└── evidence/                     # optional
    └── notes.md
```

`manifest.json` MUST conform to `State/ArchiveMode/Schemas/recovery-package.schema.json`.

All paths inside the manifest are relative to the package directory. Never place drive letters, home-directory paths, or Archive Root absolute paths in the manifest.

## Identity rules

Prefer an existing canonical item identity:

```text
youtube:<11-character-video-id>
```

If the original YouTube ID is known, preserve it even when the recovered file came from a different location.

Example:

```json
"target": {
  "item_key": "youtube:abc123DEF45",
  "youtube_id": "abc123DEF45"
}
```

A recovered mirror/reupload does not replace historical identity. It is evidence supporting the representation of the original historical object.

If identity is uncertain, do not silently guess. Record the evidence and confidence in the manifest. Prefer leaving an item unrecovered over attaching media to the wrong historical object.

## Provenance

Record how the candidate was obtained. Recommended `method` values include:

- `old_local_backup`
- `alternate_web_source`
- `mirror`
- `user_supplied`
- `manual_research`
- `other`

Record source URLs when known, but do not place credentials, cookies, access tokens, or private authentication material in the package.

## Representations

A package may contribute:

- video only;
- audio only;
- both video and audio;
- metadata/evidence only.

Media Downloader PS will normalize and verify contributed media before canonical promotion. Do not pre-edit canonical archive files to make the package appear accepted.

## Acceptance semantics

`Pending` means proposed external evidence only.

Media Downloader PS validates:

1. schema version;
2. package-relative paths;
3. target identity;
4. conflicts with existing canonical state;
5. media readability;
6. normalization to the archive compatibility contract;
7. ffprobe verification;
8. canonical destination safety;
9. provenance/history writes.

An accepted package is moved to `Imports/Accepted/` after successful canonical promotion.

A rejected package is moved to `Imports/Rejected/` with a rejection receipt. Fix the cause and submit a new package rather than rewriting canonical state.

## Canonical formats

Recovered video is normalized to the Archive compatibility contract:

- MP4 container;
- H.264 video;
- yuv420p pixel format;
- AAC audio when audio is present.

Recovered audio is normalized to:

- M4A/MP4-family container;
- AAC audio.

The application, not the external agent, performs final normalization and verification.

## Generated reports are read-only projections

The following are generated views and are not mutation inputs:

- `catalog.csv`
- `missing.csv`
- `video.m3u8`
- `audio.m3u8`
- `unavailable.jsonl`
- `removed.jsonl`

Do not edit these files to change archive truth. Submit a Recovery Package instead.

## Portability invariant

New canonical Archive Mode state stores archive-internal paths relative to Archive Root. Recovery Package file references are package-relative. Absolute filesystem paths are runtime-only and must not be persisted in package manifests.

## Safe harness workflow

```text
read ARCHIVE_AGENT.md
→ inspect missing.csv / canonical history
→ research dynamically
→ collect candidate evidence in isolated workspace
→ identify the target historical item
→ create schema-valid Recovery Package
→ validate package
→ place package under Imports/Pending
→ let Media Downloader PS process it
→ inspect Accepted/Rejected receipt and regenerated reports
```

If any operation would require changing existing canonical files directly, stop. The correct integration path is the Recovery Package boundary.

## Transactional submission and recovery safeguards

Use a package ID of 1 to 160 ASCII letters, digits, underscores or hyphens. The direct Pending directory name must equal that ID. An Accepted ID is immutable and cannot be reused. Package paths must not contain traversal, drive letters, alternate data streams, reserved Windows names, symbolic links or junctions. When both target IDs are supplied they must agree. A youtube_id-only target is supported, but must already exist in canonical state.

Prepare the complete package outside Pending, then move the directory into Pending only after all files are closed. Do not edit submitted packages while the application is running. Every contributed representation is staged and verified before any canonical promotion. The durable transaction journal supports restart roll-forward, not manual edits. If the application reports a transaction conflict, preserve the journal and all referenced evidence for diagnosis.

Schema or identity rejection creates a unique Rejected receipt. Media-tool or storage failure before promotion leaves the package Pending for retry. Interrupted promotion retains its journal for recovery on next initialization. Acceptance never overwrites existing canonical media or older Accepted evidence. A video-only recovery still leaves missing audio visible, and vice versa. Acceptance proves the stated technical checks, not that the content matches the historical upload; human identity review remains necessary.

The root contract and packaged schema are regenerated from the installed build. A differing existing contract is preserved in a content-addressed previous-version file before replacement. Keep custom recovery notes separately rather than editing generated contracts.

## Target Windows acceptance command

Run the sealed portable candidate from a fresh extraction and use a separate empty Archive Root:

```powershell
.\archive-local-harness.ps1 -ArchiveRoot 'C:\ArchiveHarness' -PlaylistUrl '<playlist URL>' -VideoUrl '<video URL>' -ExpectedCommit '<40-character candidate commit>'
```

The harness checks the package identity and hashes, verifies the requested canonical video and audio, repeats the operation, checks unchanged media hashes, and writes a dated evidence receipt. A source-code build, hosted CI PASS, or this document alone is not a substitute for that target-host result.

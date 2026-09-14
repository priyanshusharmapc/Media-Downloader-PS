# Archive Mode recovery workflow

This guide defines the supported method for attaching externally recovered media or evidence to an existing Archive Mode historical item.

The central rule is simple: external tools may research and propose evidence, but Media Downloader PS alone owns canonical archive mutation.

## 1. Why Recovery Packages exist

A historical item may remain important after the original upload becomes deleted, private, removed, login-restricted, geo-blocked, copyright-blocked, or otherwise unavailable. The archive must preserve that historical identity while still allowing authorized recovery from backups, mirrors, alternate sources, user-supplied files, or manual research.

Directly copying a candidate file into `Video/` or `Audio/` would bypass identity checks, provenance, normalization, verification, conflict protection, transaction safety, and durable receipts. Recovery Packages provide a controlled submission boundary instead.

## 2. Authority boundary

External agents, scripts, or humans MAY:

- read generated reports and canonical history;
- inspect `catalog.csv`, `missing.csv`, playlist history, and canonical item identity;
- search authorized local and external sources;
- download candidate media into an isolated workspace;
- analyze candidate media and metadata;
- prepare a schema-valid Recovery Package;
- add notes and supporting evidence inside that package;
- run `archive-cli validate` before submission.

They MUST NOT:

- edit `State/ArchiveMode/items.json` directly;
- edit per-playlist canonical `items.json` or `history.jsonl`;
- edit `State/video-archive.txt` or `State/audio-archive.txt`;
- overwrite or delete canonical files under `Video/` or `Audio/`;
- change archive truth by editing CSV, M3U8, or JSONL projections;
- rewrite an existing Accepted package;
- invent a canonical identity when evidence is uncertain.

The runtime copy of `ARCHIVE_AGENT.md` in each Archive Root is the authoritative interoperability contract for external recovery work.

## 3. Package lifecycle

Recovery Packages move through three states:

```text
prepared outside Pending
        |
        v
Imports/Pending/<package-id>/
        |
        +--> Accepted/<package-id>/   successful canonical promotion
        |
        +--> Rejected/...            schema or identity rejection with receipt
        |
        +--> remains Pending          retryable pre-promotion operational failure
```

An Accepted package identity is immutable and cannot be reused.

A package that fails before canonical promotion because of a media-tool, storage, or other retryable operational problem can remain Pending. Fix the external cause and retry without editing canonical state.

An interrupted promotion retains its durable transaction journal so initialization can recover safely on the next run.

## 4. Prepare packages outside Pending

Build the entire package in a staging directory that is not inside `Imports/Pending`.

Only after the manifest, media, notes, and evidence files are completely written and closed should the complete package directory be moved into:

```text
State/ArchiveMode/Imports/Pending/<package-id>/
```

Do not modify a submitted package while Media Downloader PS may be reading it.

## 5. Package ID rules

`package_id` must contain 1 to 160 ASCII letters, digits, underscores, or hyphens:

```text
^[A-Za-z0-9_-]{1,160}$
```

The direct directory name under `Pending` must match `package_id` exactly.

Good examples:

```text
20260915-abc123DEF45-001
oldbackup_abc123DEF45_v2
manual-recovery-0007
```

Do not use spaces, slashes, drive letters, colons, or filesystem-reserved names.

## 6. Minimum layout

A package can contain media, evidence, metadata, or a combination of them.

```text
<package-id>/
├── manifest.json
├── files/
│   ├── recovered-video.ext       # optional
│   └── recovered-audio.ext       # optional
└── evidence/                     # optional
    ├── notes.md
    └── source-details.txt
```

The package may be metadata/evidence-only. `representations` is optional in schema version 1.

## 7. Identity rules

Prefer the existing canonical key:

```text
youtube:<11-character-video-id>
```

A target can identify the item using `item_key`, `youtube_id`, or both. If both are supplied they must agree.

Example:

```json
"target": {
  "item_key": "youtube:abc123DEF45",
  "youtube_id": "abc123DEF45"
}
```

A `youtube_id`-only target is supported only when that identity already exists in canonical state.

A mirror or reupload does not become a new canonical identity. It is provenance for a representation of the original historical object.

If identity is uncertain, do not guess. Preserve the candidate as external evidence, document uncertainty, and leave the item unrecovered until a human can establish a defensible match.

## 8. Manifest example

Schema version 1 uses a manifest like this:

```json
{
  "schema_version": 1,
  "package_id": "20260915-abc123DEF45-001",
  "target": {
    "item_key": "youtube:abc123DEF45",
    "youtube_id": "abc123DEF45"
  },
  "provenance": {
    "method": "old_local_backup",
    "recovered_by": "manual-research",
    "recovered_at": "2026-09-15T00:00:00+05:30",
    "confidence": "verified",
    "notes": "Recovered from an old local archive after matching historical metadata."
  },
  "representations": {
    "video": { "file": "files/recovered-video.mkv" },
    "audio": { "file": "files/recovered-audio.flac" }
  },
  "metadata": {
    "canonical_title": "Recovered unique edit",
    "source_title": "Recovered unique edit",
    "original_url": "https://www.youtube.com/watch?v=abc123DEF45",
    "user_tags": ["recovered", "edit"]
  }
}
```

The exact machine-readable contract is `resources/archive/recovery-package.schema.json` in source and `State/ArchiveMode/Schemas/recovery-package.schema.json` inside an initialized Archive Root.

## 9. Provenance

`provenance.method` is required. Useful values include:

- `old_local_backup`
- `alternate_web_source`
- `mirror`
- `user_supplied`
- `manual_research`
- `other`

Optional provenance can record:

- who or what performed the recovery;
- recovery timestamp;
- source URL;
- confidence;
- notes.

Allowed confidence values in schema version 1 are:

```text
low
medium
high
verified
```

`verified` should mean the researcher has affirmative identity evidence, not merely that the file decodes successfully.

Do not put cookies, authorization headers, bearer tokens, passwords, private keys, or access tokens in a manifest, note, filename, or source URL.

## 10. Package path safety

Every media path in `manifest.json` is relative to the package directory.

The importer rejects unsafe path constructions, including path traversal, absolute paths, drive-letter paths, alternate data streams, reserved Windows names, symbolic links, junctions, and other linked boundaries.

Good:

```json
"file": "files/recovered-video.mkv"
```

Bad:

```json
"file": "C:\\Users\\someone\\video.mkv"
```

Bad:

```json
"file": "../../Video/canonical.mp4"
```

Keep all package evidence physically inside the package directory or reference it only in prose as external provenance.

## 11. Representations

A package may contribute:

- video only;
- audio only;
- both video and audio;
- no media, only metadata/evidence.

The application performs final normalization and verification. Contributors should not overwrite canonical media to make a package appear accepted.

A video-only acceptance does not clear a missing-audio condition. An audio-only acceptance does not claim video is complete.

## 12. Metadata

Schema version 1 permits metadata fields such as:

- `canonical_title`
- `source_title`
- `original_url`
- `source_tags`
- `user_tags`

Use metadata to preserve useful historical context, not to override identity evidence.

The canonical YouTube ID remains the primary identity when known.

## 13. Validate before submission

Validate a prepared package against the target Archive Root:

```powershell
.\archive-cli.exe validate 'C:\ArchiveRoot' 'C:\RecoveryStaging\20260915-abc123DEF45-001'
```

A valid package reports:

```text
VALID
package_id=<package-id>
item_key=youtube:<video-id>
```

Validation does not promote media. It is safe to run before moving the package into Pending.

If validation fails, correct the staged package before submission. Do not modify canonical state to make the validation pass.

## 14. Submit and ingest

After successful validation, move the complete package directory into:

```text
<ArchiveRoot>\State\ArchiveMode\Imports\Pending\<package-id>\
```

Then run:

```powershell
.\archive-cli.exe ingest-pending 'C:\ArchiveRoot'
```

The command reports how many packages were accepted and returns failure if one or more Pending packages could not be processed successfully.

## 15. What the importer validates

Before canonical promotion, the importer checks the relevant contract, including:

1. package schema version;
2. package ID and directory identity;
3. safe package-relative paths;
4. target identity and agreement between supplied IDs;
5. existence of the canonical historical target where required;
6. conflicts with existing canonical representations;
7. candidate media readability;
8. normalization capability;
9. FFprobe compatibility of staged canonical output;
10. destination safety;
11. provenance and receipt data required for durable history.

Every contributed media representation is staged and verified before canonical promotion begins.

## 16. Canonical media contract

Recovered video is normalized and verified against the current Archive video contract:

- MP4-family container;
- H.264 video;
- yuv420p pixel format;
- positive duration;
- bounded video dimensions;
- AAC audio when audio is present.

Recovered audio is normalized and verified as:

- M4A/MP4-family container;
- AAC audio;
- positive duration;
- no moving video stream.

The source candidate is preserved until successful staged output exists. Existing canonical media is not overwritten merely because a package was submitted.

## 17. Transactional promotion

When media and state are ready, the importer binds the promotion into the Archive transaction model.

The promotion can include:

- canonical media placement;
- canonical item representation updates;
- metadata and history changes;
- receipt creation;
- package movement to Accepted;
- projection rebuild state.

Preimages and after-images are hashed. If a preimage no longer matches what the transaction expected, promotion refuses the conflict rather than overwriting unexpected bytes.

If the process dies during promotion, preserve all files. The next Archive initialization can roll the durable journal forward when the expected preimages still match.

## 18. Failure and retry matrix

| Failure type | Expected package state | Operator action |
| --- | --- | --- |
| malformed schema | Rejected | prepare a new corrected package |
| invalid or conflicting identity | Rejected | re-establish identity and submit a new package |
| unsafe path or linked path | Rejected | rebuild package with safe regular files |
| canonical representation already conflicts | rejected/refused | inspect canonical state; do not overwrite it manually |
| FFmpeg/FFprobe unavailable | Pending or operation failure before promotion | repair runtime, retry ingestion |
| candidate media cannot normalize or verify | Pending or failed validation depending on stage | replace candidate media, then submit a new or corrected pre-submission package as appropriate |
| storage/file-lock failure before promotion | Pending | clear storage/lock issue and retry |
| process killed during promotion | transaction journal retained | restart Archive Mode and allow journal recovery |
| transaction preimage conflict | journal preserved, mutation refused | stop and diagnose; preserve evidence |
| accepted package ID submitted again | refused | use a new package ID; Accepted evidence is immutable |

Never move an Accepted package back to Pending to force a second promotion.

## 19. Accepted evidence

Accepted recovery evidence is durable. Do not rewrite it when a later, better source appears.

If new evidence is discovered later, create a new package with a new ID. Historical evidence should remain additive so reviewers can reconstruct what was known and accepted at each point in time.

## 20. Rejected packages

A rejection is useful evidence. Keep the rejection receipt long enough to understand the cause.

For schema or identity problems, submit a new corrected package rather than manually modifying canonical state or attempting to reuse an immutable accepted identity.

## 21. Human semantic review

Technical acceptance proves that:

- the package was structurally valid;
- its target identity was allowed by canonical state;
- paths were safe;
- media normalized;
- media passed the configured technical compatibility checks;
- the promotion was transactionally safe.

It cannot prove that an alternate upload is semantically identical to the historical original. Human review of title, duration, frames, audio, descriptions, timestamps, checksums, backups, or other provenance may still be required.

For ambiguous media, prefer preserving evidence without canonical promotion over attaching the wrong content to a historical identity.

## 22. External agent workflow

The intended agent workflow is:

```text
read ARCHIVE_AGENT.md
→ inspect missing.csv and canonical history
→ research authorized sources dynamically
→ collect candidates outside the Archive Root
→ establish the target historical identity
→ prepare a complete Recovery Package
→ validate it
→ move the closed package directory into Pending
→ let Media Downloader PS ingest it
→ inspect Accepted/Rejected status and receipt
→ inspect regenerated reports
```

If an agent concludes that it must edit an existing canonical file directly, that conclusion is wrong. It should stop and use the Recovery Package boundary.

## 23. Package checklist

Before submission, confirm all of the following:

- package ID uses only allowed characters and matches its directory name;
- target item is the intended historical object;
- item key and YouTube ID agree when both are present;
- all manifest paths are package-relative;
- no files or parent directories are links or junctions;
- no secrets are present in provenance or notes;
- candidate media opens locally;
- supporting identity evidence is included or described;
- confidence reflects identity evidence, not just codec validity;
- the package validates with `archive-cli validate`;
- the complete package is closed before moving into Pending;
- no canonical file has been edited manually.

## 24. Troubleshooting evidence

For a recovery incident, preserve:

- the Pending/Accepted/Rejected package directory;
- receipt files;
- relevant Activity and Diagnostic logs;
- transaction journal if one exists;
- exact package manifest;
- exact application package commit;
- FFmpeg/FFprobe errors;
- any supporting human research notes.

Do not sanitize an incident by deleting the failing evidence before diagnosis. Copy the affected material to a separate diagnostic location if additional experimentation is necessary.
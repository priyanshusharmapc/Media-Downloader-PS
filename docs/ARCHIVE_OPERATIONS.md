# Archive Mode operations and troubleshooting

This guide is for operators, testers, and developers running Media Downloader PS Archive Mode on a real machine.

## 1. Choose the correct package

For target Windows acceptance, use a portable artifact produced by the Archive Qt6 qualification workflow, not an arbitrary local build and not an older portable ZIP relabeled as current.

For the currently qualified code candidate:

- commit: `33cf3150fb7ccc6aa26f0f2d9454db7701baad05`
- workflow run: `34881209447`
- qualification: Linux PASS, Windows PASS
- portable artifact digest: `sha256:186798f595707f3a8d2ecd257ca207e55c6c374f529e532172010e91b10478a1`

After extraction, inspect `build-identity.json`. The `commit` field in that file is the expected commit for that exact package.

Do not mix files from two candidate ZIPs. The local harness rejects extra unsealed files and hash mismatches.

## 2. Archive Root rules

Use an Archive Root outside the portable application directory.

For first acceptance, use a new empty directory such as:

```text
C:\ArchiveHarness
```

Do not put the Archive Root inside the extracted portable package. Do not make the root, its parent chain, or recovery package paths symbolic links or junctions.

For routine long-term use, choose a stable local filesystem location with adequate free space and reliable backup. Avoid removable or network storage for the first qualification run because it adds failure modes unrelated to Archive Mode itself.

## 3. First preflight

From the portable directory:

```powershell
.\archive-cli.exe preflight 'C:\ArchiveHarness'
```

A successful result should report usable yt-dlp, FFmpeg, FFprobe, and Deno runtimes and end with:

```text
preflight=PASS
```

Preflight proves the tools launch. It does not prove YouTube access, cookies, account permissions, network stability, or playlist discovery.

If preflight fails, do not start live acceptance. Fix the missing or blocked runtime first.

## 4. Scan a playlist

```powershell
.\archive-cli.exe scan 'C:\ArchiveHarness' '<PLAYLIST URL>' 'My archive playlist'
```

A complete scan reports fields such as:

```text
complete=true
observed=<count>
active=<count>
removed=<count>
unavailable=<count>
```

If `complete=false` or the command returns a non-zero exit code, treat the run as incomplete. Do not infer that items missing from that partial observation were removed from the playlist.

Common reasons for incomplete discovery include network failures, rate limiting, extractor changes, login requirements, and malformed or truncated responses.

## 5. Sync one exact item

```powershell
.\archive-cli.exe sync-item 'C:\ArchiveHarness' 'https://www.youtube.com/watch?v=<VIDEO_ID>'
```

A successful sync ends with:

```text
sync=PASS
item_key=youtube:<VIDEO_ID>
video_state=complete
video_path=...
audio_state=complete
audio_path=...
```

`sync-item` is exact-item oriented. It does not treat an arbitrary file elsewhere in the archive as proof that the requested item succeeded.

## 6. Verify one exact item

```powershell
.\archive-cli.exe verify-item 'C:\ArchiveHarness' 'https://www.youtube.com/watch?v=<VIDEO_ID>'
```

`verify-item` re-reads canonical state, locates the requested canonical item, verifies its video and audio files, and reports the same canonical paths.

Use it after manual filesystem incidents, antivirus quarantines, restores, or suspected corruption.

## 7. Run the target Windows local harness

For the first live acceptance run, use a fresh package extraction and empty root:

```powershell
.\archive-local-harness.ps1 `
  -ArchiveRoot 'C:\ArchiveHarness' `
  -PlaylistUrl '<REAL PLAYLIST URL>' `
  -VideoUrl '<REAL VIDEO URL>' `
  -ExpectedCommit '33cf3150fb7ccc6aa26f0f2d9454db7701baad05'
```

Use the commit from `build-identity.json` if you are running a different qualified package.

The harness refuses to proceed if:

- required package files are missing;
- `build-identity.json` does not match `-ExpectedCommit`;
- the package is not marked `windows-ci-qualified-for-local-harness`;
- any file listed in `SHA256SUMS.txt` fails verification;
- a required runtime file is not sealed;
- the extracted directory contains unsealed extra files;
- the Archive Root is inside the portable package;
- the Archive Root is non-empty unless `-AllowExistingArchive` is explicitly supplied.

The harness then performs:

1. runtime preflight;
2. complete playlist discovery;
3. exact-item sync;
4. exact-item verification;
5. SHA-256 capture of canonical video and audio;
6. a second sync and verification;
7. equality checks proving the rerun did not change canonical media;
8. creation of a dated `local-harness-evidence-*.json` receipt in the Archive Root.

A PASS from this script is the target-host acceptance gate for the tested playlist and item.

## 8. Routine operating cycle

A conservative operating cycle is:

```text
preflight when environment changed
→ scan playlist
→ inspect catalog.csv and missing.csv
→ sync required accessible items
→ verify suspicious or restored items
→ prepare Recovery Packages for unavailable historical media
→ validate package
→ ingest Pending packages
→ inspect Accepted/Rejected receipts
→ back up the Archive Root
```

Do not turn `catalog.csv`, `missing.csv`, M3U8 files, or JSONL reports into mutation workflows. They are projections only.

## 9. Recovery package validation and ingestion

Validate a prepared package before submission:

```powershell
.\archive-cli.exe validate 'C:\ArchiveRoot' 'C:\staging\my-package'
```

After moving a complete package under `State\ArchiveMode\Imports\Pending\<package-id>\`, process Pending packages with:

```powershell
.\archive-cli.exe ingest-pending 'C:\ArchiveRoot'
```

Accepted packages move to `Accepted`. Schema or identity rejections move to `Rejected` with a receipt. Some operational failures before canonical promotion intentionally leave the package Pending for retry.

Never reuse an Accepted package ID.

See [ARCHIVE_RECOVERY.md](ARCHIVE_RECOVERY.md).

## 10. What to do after an interrupted process

Archive state publication uses a durable transaction journal. If a process is killed during a multi-file commit, the next initialization attempts safe recovery.

Recommended response:

1. stop all Archive Mode GUI and CLI processes;
2. preserve the Archive Root exactly as it is;
3. restart with `archive-cli preflight <root>` or another read/initializing operation;
4. allow initialization to recover the journal;
5. inspect the reported error if recovery refuses a conflict;
6. copy the journal and relevant state files before any manual intervention.

Do not delete transaction files merely because an operation appears stuck. A journal conflict means the preimage changed and the software is intentionally refusing to overwrite unexpected bytes.

## 11. Concurrent writer error

Archive mutation is serialized. If a command reports that another process owns the archive:

- check for a running GUI instance;
- check for another `archive-cli.exe` process;
- allow a legitimate long-running writer to finish;
- terminate only the process you know is stale;
- retry after process exit.

Do not delete `sync.lock` while a writer may still be active. The lock is not expired solely because of age.

## 12. Missing or corrupt canonical media

If state says `complete` but a media file was deleted, truncated, quarantined, or replaced, `verify-item` should fail.

For an accessible original source, rerun `sync-item`. The executor can repair a missing representation and re-verify the canonical result.

For an unavailable original source, create a Recovery Package from authorized evidence rather than copying a file directly into `Video/` or `Audio/`.

## 13. Antivirus and file-lock incidents

Windows antivirus, indexing, backup, or media software can temporarily lock files.

If a promotion or normalization fails because a file is locked:

- preserve the Pending package and transaction evidence;
- close software holding the file;
- confirm the antivirus did not quarantine a runtime or canonical media file;
- rerun validation or ingestion after the lock is gone;
- use `verify-item` on any representation that may have been affected.

Do not disable security software globally as a first response. Prefer narrow diagnosis and explicit allowlisting when justified.

## 14. Logs

Archive logs live under:

```text
State/ArchiveMode/Logs/Activity/
State/ArchiveMode/Logs/Diagnostic/
```

Activity logs are structured operational evidence. Diagnostic logs are bounded and rotated. Credential-like content is redacted, but operators should still avoid putting secrets into manifests or notes.

When reporting a bug, capture:

- exact application/package commit;
- exact command or GUI action;
- command exit code;
- relevant activity and diagnostic logs;
- `build-identity.json` for portable incidents;
- the local harness evidence receipt if acceptance was involved;
- affected package receipt or transaction journal when relevant.

Do not publish private cookies, authorization headers, tokens, or user data with bug reports.

## 15. Backup strategy

Back up the Archive Root as a unit. The important principle is consistency across media, canonical state, playlist history, accepted recovery evidence, and transaction state.

A good backup includes at least:

- `Video/`
- `Audio/`
- `Metadata/`
- `Playlists/`
- `State/`
- root `ARCHIVE_AGENT.md`

If possible, take backups when no Archive writer is active.

Do not back up only the CSV reports and assume the archive can be reconstructed from them. They are projections, not canonical state.

## 16. Restore strategy

Restore the Archive Root into a clean directory, then run:

```powershell
.\archive-cli.exe preflight '<RESTORED ROOT>'
```

Follow with `verify-item` for representative known items. For a large archive, sample across older and newer media and any recovered items.

If restoration includes an interrupted transaction journal, allow initialization to process it rather than deleting it.

## 17. Moving an Archive Root

Archive Mode stores new canonical internal paths relative to the Archive Root where appropriate, so moving a complete Archive Root is supported more safely than persisting machine-specific paths.

Move the full root, not selected subdirectories. After moving:

1. run preflight;
2. scan one source;
3. verify representative items;
4. confirm reports regenerate correctly;
5. confirm recovery schemas and the root `ARCHIVE_AGENT.md` are present.

## 18. Large archive considerations

Before a large run:

- verify free disk space for both video and audio representations;
- keep `Temp/` on storage with enough headroom for staged normalization;
- avoid unnecessary concurrent external processes against the same root;
- back up state before major migrations;
- use complete playlist scans as authoritative reconciliation points.

The current qualification includes high-volume state and integration coverage, but it is not a formal performance benchmark for every archive size and storage medium.

## 19. Stop conditions

Stop the operation and preserve evidence if you see any of the following:

- transaction conflict;
- canonical state parse/schema failure;
- missing paired registry files;
- path escape, linked path, or junction refusal;
- package identity disagreement;
- an Accepted package ID collision;
- unexpected canonical overwrite attempt;
- package hash mismatch;
- local harness commit mismatch;
- incomplete playlist discovery during an acceptance test.

These are safety refusals, not conditions to bypass manually.

## 20. Kilo live-harness handoff

The recommended Kilo phase starts only after the sealed local harness passes once manually or under controlled automation on the target Windows machine.

After that, test the broader real-world surface:

- GUI responsiveness during long syncs;
- cancellation during download and normalization;
- restart after abrupt GUI termination;
- antivirus and file-lock behavior;
- Unicode and long Archive Root paths;
- private, deleted, removed, unavailable, or login-required playlist entries using authorized access;
- repeated scans across actual playlist changes;
- recovery packages prepared by the external-agent workflow;
- backup and restore on a copy of the archive.

Treat every failure as evidence. Do not repair canonical files manually unless the code path explicitly instructs you to do so.
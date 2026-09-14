# Archive Mode pre-harness audit and hardening

## Baseline and scope

Recovered from GitHub, not a prior conversation workspace: `priyanshusharmapc/Media-Downloader-PS`, `archive-mode-v1`, commit `3b0d705818b00dfd69d5c21b9cadec17f5f1abeb`, source tree `fe9052dd75fa9f16451132893c0a938bee11f939`.

The downloaded official source artifact had SHA256 `d0e9fd88ed47f4760cc4838e50f632a9c142ed78ac0c6e0af5aabc14f2357e99`. Reconstructing its Git tree reproduced the exact baseline tree. Qualification run `34861991248` passed Linux and Windows, including packaged runtime smoke tests. That historical result applies to the baseline, not automatically to this revision.

This revision preserves the existing Archive implementation and repairs specific defects. It does not rebuild or replace the product architecture. Scope: Archive state, discovery, media verification/execution, external recovery, logging, CLI/GUI boundaries, portable acceptance, and repeatable qualification. It does not claim exhaustive review of every inherited downloader engine.

## Critique and completed remedies

| Finding | Risk | Remedy and verification |
|---|---|---|
| Malformed or incomplete discovery could look complete | False removal of historical membership | Require entries array, valid entries, matching source identity, declared-count consistency, successful exit, and no warning/error indications before inferring removal. Four independently failing baseline regressions now pass. Partial scans preserve prior membership. |
| Invalid JSON state became an empty collection | Silent canonical/source overwrite | Strict identity/schema/representation checks, checked read/write errors, refusal of missing paired registries and malformed history. Corruption tests verify original bytes remain unchanged. |
| Multi-file state writes had no recovery protocol | Canonical state, history and projections could diverge after interruption | Durable preimage/after-image hashed transaction, validate all preimages before publication, idempotent restart roll-forward, conflict refusal, and a projection-rebuild marker. Restart and mid-commit simulations pass. |
| Independent GUI/CLI mutations and age-expiring lock | Concurrent writers or long-running work could overlap | One root lock around read-modify-write and import operations, same-thread reentrancy, no age-only lock expiry, process-death recovery. Two concurrent processes and killed-writer tests pass. |
| Normalization removed source before successful replacement | Irrecoverable media loss or wrong extension | Stage and verify a correctly suffixed replacement, never delete the original, refuse overwrite, preserve incompatible source bytes. Real FFmpeg normalization test compares original hashes. |
| Complete state bypassed actual-file checks | Missing/corrupt media reported as success | Re-read canonical state, verify complete paths, repair missing output, adopt valid existing media, and expose exact-item `verify-item`. Integration covers deletion, failure, repair and idempotence. |
| Weak media compatibility checks | Incompatible or misleading output accepted | Validate container family, extension, positive duration, every relevant stream, H.264/yuv420p/bounded video dimensions, AAC audio, and no moving video in audio-only output. Real FFmpeg/FFprobe and full fixture decoding pass. |
| Recovery ID/path/schema ambiguity | Wrong historical item, path escape, overwrites | Validate canonical ID agreement and existence, support ID-only targeting, safe package IDs, no linked paths/junctions, portable filenames, manifest field types and confidence, and immutable Accepted identities. Six import/path regressions plus actual media imports pass. |
| Recovery published video before audio succeeded | Partial acceptance, lost evidence, ambiguous retry | Stage and verify every representation, bind input/evidence hashes, jointly journal media/state/history/receipt/package promotion, keep failed pre-promotion packages Pending and old Accepted packages immutable. Invalid second representation leaves no canonical mutation or published media. |
| Recovery and reporting collapsed distinct states | Missing audio hidden by video-only recovery; lost known titles | Only both-complete clears unmet recovery needs. CSV retains canonical title, formula-like values are neutralized, M3U text cannot inject records. Duplicate playlist occurrences receive occurrence IDs while retaining one canonical media item. |
| Nested secrets and oversized diagnostics | Credential exposure or uncontrolled diagnostic file growth | Recursive redaction, bounded per-entry diagnostics and rotation, unlinked/dated retention boundaries. Redaction regression passes. |
| Resource contract never refreshed | Stale external-agent instructions after upgrade | Install current embedded contracts and preserve differing prior bytes under a content-addressed previous-version name. Upgrade preservation integration test passes. |
| Local harness selected the first media file | Unrelated/stale files could produce false PASS | Check expected commit, sealed package bytes and required runtime files, use isolated root by default, verify exact requested item, repeat operation and compare canonical media hashes, write a dated evidence receipt. A Windows-only positive/tampered-package test is part of CI. |
| Qualification omitted application-level real-media integration | Hosted green status did not prove archive execution | Add reproducible offline application integration through a deterministic yt-dlp subprocess fixture with real FFmpeg/FFprobe. Run normal and ASan/UBSan Linux tests, and Windows tests against the pinned packaged media tools. YouTube remains a separate target-host gate. |

## Executed local evidence

Environment: fresh Debian 13 container, GCC 14.2, recovered Qt6 dependencies, real local FFmpeg/FFprobe. The original source-tree identity was verified before changes.

* Baseline experiment: original core test passed; all 16 newly introduced regression cases failed against the unmodified baseline. See `qualification/pre-harness/baseline-tests.log` and XML.
* Current full GUI, CLI, original core tests, hardening tests and fake tool compile successfully.
* Current normal suite: 18/18 CTest entries pass. The integration entry contains 22 unittest methods: 21 execute and pass on Linux, with the Windows PowerShell wrapper explicitly skipped on Linux.
* Current AddressSanitizer plus UndefinedBehaviorSanitizer suite: 18/18 CTest entries pass with leak detection and halt-on-error enabled. No sanitizer finding was reported. The same Windows-only method is skipped.
* Integration includes real transcoding/probing/fixture decoding, normalized-original preservation, failed second representation, accepted evidence immutability, metadata-only imports, download-archive retry ordering, invalid CLI calls without filesystem side effects, concurrent writers, killed-writer recovery, mid-commit replay/conflicts, duplicate occurrences, resource preservation, projection injection, unavailable-source adoption, malformed journal payload, corrupt history and linked-state refusal.
* `git diff --check` passes. JUnit and detailed logs are preserved alongside this report.

These are test cases, not a statistical estimate of defect freedom. Subprocess fixture tests do not exercise real YouTube authentication, throttling or extractor changes.

## Build and rerun

```sh
cmake -S . -B build -G Ninja -DBUILD_WITH_QT6=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Python 3.11 or newer and real FFmpeg/FFprobe are required for integration. Set `ARCHIVE_TEST_FFMPEG` and `ARCHIVE_TEST_FFPROBE` to override the media tools. The fake yt-dlp executable is test-only and is never copied into the product package.

## Remaining acceptance gates and limitations

At the time this source report was frozen, updated Windows CI had not yet run. Consult the candidate commit's Actions results rather than inheriting the baseline's status. Windows CI must compile, run regression/integration, execute the PowerShell wrapper test, launch the portable GUI, and seal the new portable artifact. The linked-file unit case is explicitly host-dependent on Windows; the separate Windows junction integration provides directory-boundary coverage.

After Windows CI passes, run the new portable candidate on the actual Windows/Kilo host, from a fresh extraction and against a separate empty archive root:

```powershell
.\archive-local-harness.ps1 -ArchiveRoot 'C:\ArchiveHarness' -PlaylistUrl '<real playlist URL>' -VideoUrl '<real video URL>' -ExpectedCommit '<exact candidate commit from build-identity.json, independently checked against Actions>'
```

Then review GUI responsiveness, cancellation during a long download, restart after abrupt GUI termination, antivirus/file locks, Unicode/long root paths, and representative private/deleted/unavailable playlist cases with authorized access. No target-host or live-YouTube result is claimed by this report.

The journal is tested for process interruption and controlled preimage conflicts, not physical power loss on every filesystem. Path checks are conservative and do not claim to defeat a malicious concurrent local administrator. Technical media acceptance cannot prove semantic identity of an externally recovered upload; provenance and human review remain necessary. Large-archive performance, all historical extractor variants, and inherited non-Archive engine behavior are not exhaustively qualified.

## Delivery and preservation

Publish the tested source as a new candidate, retaining the prior qualified commit for rollback. Recovery journal files must not be deleted to force a retry. Do not copy the old executable into the new candidate or relabel the old portable artifact as this revision.

Repository-native source, tests, workflow, harness, agent contract and this audit are the durable handoff. Source ZIP, patch and full evidence bundle are additional portable exports. The code repository remains the authority; no Google Drive, ontology graph or parallel knowledge system was introduced.

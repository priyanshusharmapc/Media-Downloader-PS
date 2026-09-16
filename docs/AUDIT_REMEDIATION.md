# Audit Remediation

The audit remediation branch resolves the release-relevant defects identified by the independent engineering review.

## Media Integrity

Canonical video and audio verification first validates the required container/profile metadata and then consumes every relevant FFmpeg video/audio stream with `-xerror` and a bounded timeout. Fast-start MP4/M4A truncation fixtures remain metadata-readable but fail integrity verification. The integration suite verifies that a damaged canonical representation is rejected and repaired by a subsequent sync.

## Archive Root And Reparse Safety

The requested absolute Archive Root is preserved and validated before any canonical path resolution. The C++ safety layer rejects symbolic links, junctions, unknown unsafe Windows reparse tags and name-surrogate boundaries, while allowing the documented Cloud Files `0x9000n01A` family. Windows CI executes real root, parent, State, Media and Recovery Package link tests with Developer Mode enabled.

## GUI Settings

Archive Root settings are stored in the per-user Qt application-configuration location. The application directory remains sealed. The GUI settings regression exercises the shared persistence helper and verifies that no package-local settings file or unsealed package file is created.

## Identity And Names

Unresolved playlist placeholder base identity no longer includes mutable position. Reconciliation matches prior unresolved occurrences by stable source/title/URL fingerprint and preserves separate duplicate occurrence keys. Windows reserved-device variants, including superscript `COM`/`LPT` forms, are rejected.

## Workflows And Endurance

Inherited upstream publisher workflows were removed. The maintained Archive Qt6 workflow builds the triggering commit, runs Windows/Linux tests and sanitizers, enables the Windows symbolic-link qualification, seals the package and uploads evidence. The endurance qualification runs for at least 60 minutes and records package identity, iterations, complete media hashes, temporary residue and transaction-journal state.

## Verification Boundary

The native file-dialog interaction itself is not automated in the offscreen CI environment. The shared GUI persistence path is exercised by the packaged settings smoke and the full GUI startup remains covered. Provider availability and the exact historical OneDrive ancestor remain external/environment-specific risks; the Cloud Files tag policy and reparse boundaries are tested in the application core and Windows target suite.

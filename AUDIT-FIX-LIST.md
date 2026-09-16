# Audit Remediation Status

This list distinguishes implementation, focused regression and final qualification. A local PASS does not close a release gate until the exact final source commit has passed authoritative CI and target-host acceptance.

| ID | Implementation | Focused regression | Final qualification |
|---|---|---|---|
| MDPS-AUDIT-001 | PASS: metadata plus full FFmpeg stream decode | PASS locally; real fast-start truncation is rejected and repaired | CI PASS; final package/live pending |
| MDPS-AUDIT-002 | PASS: requested root is validated before canonical use | PASS logic/tests; Windows link cases require Developer Mode | CI PASS; final package pending |
| MDPS-AUDIT-003 | PASS: C++ reparse tag policy added and shared | PASS policy/tag cases; Windows path cases require Developer Mode | CI PASS; final package pending |
| MDPS-AUDIT-004 | PASS: GUI settings moved to user config location | PASS shared settings-location regression | GUI helper CI PASS; interactive selection behavior documented as environment-limited |
| MDPS-AUDIT-005 | PASS: inherited upstream publisher workflows removed | PASS workflow inventory locally | CI PASS on remediation branch |
| MDPS-AUDIT-006 | OPEN: final identity must be regenerated after repairs | N/A | PENDING final package/CI identity |
| MDPS-AUDIT-007 | PASS: 60-minute endurance script added | NOT RUN | OPEN until measured endurance completes |
| MDPS-AUDIT-008 | PASS: unresolved placeholder base identity excludes position; occurrence matching preserves prior keys | PASS reorder/duplicate regression locally | CI PASS; final history/live package pending |
| MDPS-AUDIT-009 | PASS: superscript reserved-device variants rejected | PASS locally | CI PASS |
| MDPS-AUDIT-010 | PASS: documentation corrected; verifier semantics are explicit | PASS documentation review locally | Final docs regeneration pending |
| MDPS-AUDIT-011 | PASS: executable source modes restored where tracked | PASS `git diff --summary` review | PENDING publication tree verification |

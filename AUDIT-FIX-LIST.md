# Audit Remediation Status

This list distinguishes implementation, focused regression and final qualification. A local PASS does not close a release gate until the exact final source commit has passed authoritative CI and target-host acceptance.

| ID | Implementation | Focused regression | Final qualification |
|---|---|---|---|
| MDPS-AUDIT-001 | PASS: metadata plus full FFmpeg stream decode | PASS locally; real fast-start truncation is rejected and repaired | PENDING CI/package/live rerun |
| MDPS-AUDIT-002 | PASS: requested root is validated before canonical use | PASS logic/tests; Windows link cases require Developer Mode | PENDING authoritative Windows execution |
| MDPS-AUDIT-003 | PASS: C++ reparse tag policy added and shared | PASS policy/tag cases; Windows path cases require Developer Mode | PENDING authoritative Windows execution |
| MDPS-AUDIT-004 | PASS: GUI settings moved to user config location | PASS settings-location and package-path regression | PENDING packaged GUI selection/persistence run |
| MDPS-AUDIT-005 | PASS: inherited upstream publisher workflows removed | PASS workflow inventory locally | PENDING new branch CI audit |
| MDPS-AUDIT-006 | OPEN: final identity must be regenerated after repairs | N/A | PENDING final package/CI identity |
| MDPS-AUDIT-007 | PASS: 60-minute endurance script added | NOT RUN | OPEN until measured endurance completes |
| MDPS-AUDIT-008 | PASS: unresolved placeholder base identity excludes position; occurrence matching preserves prior keys | PASS reorder/duplicate regression locally | PENDING full history/projection qualification |
| MDPS-AUDIT-009 | PASS: superscript reserved-device variants rejected | PASS locally | PENDING full CI |
| MDPS-AUDIT-010 | PASS: documentation corrected; verifier semantics are explicit | PASS documentation review locally | PENDING final docs QA |
| MDPS-AUDIT-011 | PASS: executable source modes restored where tracked | PASS `git diff --summary` review | PENDING publication tree verification |

# Archive Mode documentation quality checklist

Use this checklist when reviewing a change that affects Archive Mode behavior, packaging, recovery, or qualification.

## Contract coverage

A documentation update is required when a change affects any of these surfaces:

- Archive Root directory layout
- canonical state fields or schema expectations
- playlist reconciliation or removal semantics
- representation states or derived recovery status
- video/audio compatibility rules
- FFmpeg/FFprobe normalization or verification behavior
- lock ownership or stale-process recovery
- transaction journal behavior
- recovery package schema, identity, path, or lifecycle rules
- CLI command syntax, output fields, or exit semantics
- portable runtime contents
- build identity or package sealing
- local harness parameters or PASS conditions
- CI test layers, artifacts, or evidence
- target-host acceptance criteria
- known limitations or operational stop conditions

## Consistency rules

Before merging documentation changes:

1. Verify commands against `archive-cli` and `archive-local-harness.ps1` rather than memory.
2. Verify recovery fields against `recovery-package.schema.json`.
3. Separate canonical state from generated projections explicitly.
4. Do not describe an incomplete discovery as authoritative removal evidence.
5. Do not describe technical media acceptance as proof of semantic identity.
6. Do not tell operators to delete locks, journals, Accepted packages, or canonical state to force progress.
7. Distinguish CI qualification from real target-host acceptance.
8. Treat `build-identity.json` as the package-specific commit authority.
9. Treat `RUNTIME_VERSIONS.txt` and `SHA256SUMS.txt` as package-specific runtime and sealing authority.
10. Avoid claiming defect freedom. State the required gates and point to the exact run evidence for the candidate.
11. Do not hardcode a "latest" commit, run ID, artifact ID, digest, test count, or runtime version in living docs.
12. Keep superseded qualification history in Git history and preserved evidence, not in current operational guidance.

## Link review

The maintained Archive Mode docs should remain reachable from the repository root README and from `docs/README.md`:

- `docs/ARCHIVE_MODE.md`
- `docs/ARCHIVE_OPERATIONS.md`
- `docs/ARCHIVE_RECOVERY.md`
- `docs/ARCHIVE_TESTING.md`
- `docs/DOCUMENTATION_CHECKLIST.md`
- `resources/archive/ARCHIVE_AGENT.md`

## Release review

Before a candidate is handed to Kilo, documentation should explain how to determine and verify:

- the exact package commit from `build-identity.json`;
- the matching CI run and Linux/Windows qualification state;
- the portable package sealing and runtime identity;
- the local harness invocation;
- what local PASS proves and does not prove;
- troubleshooting and stop conditions;
- recovery workflow and authority boundary;
- backup and incident-evidence guidance.

Candidate-specific commit IDs, run IDs, artifact IDs, digests, and runtime versions should live in that candidate's evidence record, not in these maintained docs.

If any contract above is missing or contradicts the implementation, the documentation handoff is incomplete.
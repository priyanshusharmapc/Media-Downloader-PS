# Archive Mode documentation

This directory contains the maintained documentation for Media Downloader PS Archive Mode.

## Start by audience

### Operator or tester

Read [ARCHIVE_OPERATIONS.md](ARCHIVE_OPERATIONS.md), then [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md). These explain how to run the portable candidate, preflight the runtime, scan playlists, sync and verify items, interpret failures, and execute the target Windows local harness.

### Developer or reviewer

Read [ARCHIVE_MODE.md](ARCHIVE_MODE.md) first. It documents the architecture, state model, path layout, safety invariants, transaction behavior, media contract, generated projections, and command-line surface.

Then read [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md) for the qualification strategy and evidence chain, and [DOCUMENTATION_CHECKLIST.md](DOCUMENTATION_CHECKLIST.md) before changing an Archive contract.

### Recovery agent, script, or human researcher

Read [ARCHIVE_RECOVERY.md](ARCHIVE_RECOVERY.md) and the embedded [Archive Agent contract](../resources/archive/ARCHIVE_AGENT.md). The contract is the authoritative submission boundary for external recovery work.

## Current-status source of truth

Maintained documentation intentionally does not hardcode a "current" commit, workflow run, artifact ID, digest, or runtime version. Those values become stale as soon as another qualified package is produced.

For any candidate package, determine current status from:

1. the exact package's `build-identity.json`;
2. the Archive Qt6 qualification result for that exact commit;
3. the package's own `SHA256SUMS.txt` and `RUNTIME_VERSIONS.txt`;
4. the target-host local-harness evidence receipt.

If those sources disagree, stop and resolve the identity mismatch before using the package.

## Document set

| Document | Purpose |
| --- | --- |
| [ARCHIVE_MODE.md](ARCHIVE_MODE.md) | Architecture, invariants, state, layout, media contract, CLI behavior |
| [ARCHIVE_OPERATIONS.md](ARCHIVE_OPERATIONS.md) | Routine operation, first run, troubleshooting, backup, incident handling, Kilo handoff |
| [ARCHIVE_RECOVERY.md](ARCHIVE_RECOVERY.md) | Recovery Package creation, validation, provenance, retry, acceptance, rejection |
| [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md) | Regression, integration, sanitizers, Windows qualification, package sealing, local acceptance |
| [DOCUMENTATION_CHECKLIST.md](DOCUMENTATION_CHECKLIST.md) | Contract-change checklist to keep documentation synchronized with implementation |
| [../resources/archive/ARCHIVE_AGENT.md](../resources/archive/ARCHIVE_AGENT.md) | Runtime materialized contract for external agents and recovery automation |

## Authority hierarchy

When documents disagree, use this order:

1. the source code and schema in the exact commit being executed;
2. `resources/archive/ARCHIVE_AGENT.md` and `resources/archive/recovery-package.schema.json` for recovery submissions;
3. the maintained docs in this directory;
4. chat transcripts, temporary notes, exported bundles, or other secondary material.

Qualification history belongs in Git history, workflow artifacts, and preserved evidence, not in living operational documentation.

## Documentation maintenance rules

Documentation should be updated whenever any of the following changes:

- Archive Root paths or canonical state schema
- recovery package schema or acceptance rules
- media compatibility requirements
- CLI command syntax or exit semantics
- transaction or locking behavior
- portable package sealing and identity checks
- local harness parameters or acceptance criteria
- CI test layers or artifact structure
- known limitations or target-host acceptance requirements

A code change that alters one of these contracts is incomplete until the corresponding documentation is updated. Use [DOCUMENTATION_CHECKLIST.md](DOCUMENTATION_CHECKLIST.md) as the release review checklist.

Do not add hardcoded "latest" build identities to maintained docs. Point readers to package identity and CI evidence instead.
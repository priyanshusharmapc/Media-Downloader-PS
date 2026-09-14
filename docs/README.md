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

## Current status

At the time these documents were authored, the latest code-qualified Archive Mode candidate was:

- Branch: `archive-mode-v1`
- Commit: `33cf3150fb7ccc6aa26f0f2d9454db7701baad05`
- Source tree: `b8b0c299518a8b81d5731e5da8638e07ed361e40`
- GitHub Actions run: `34881209447`
- Linux job: PASS
- Windows job: PASS
- Target-host real YouTube/Kilo acceptance: still required

The Windows portable artifact from that qualification run was named `Media-Downloader-PS-Windows-Qt6-33cf3150fb7ccc6aa26f0f2d9454db7701baad05`. Its GitHub artifact digest was `sha256:186798f595707f3a8d2ecd257ca207e55c6c374f529e532172010e91b10478a1`.

Documentation commits can be newer than a previously built portable package. Treat the `build-identity.json` inside a package as the authority for that package's expected commit.

## Document set

| Document | Purpose |
| --- | --- |
| [ARCHIVE_MODE.md](ARCHIVE_MODE.md) | Architecture, invariants, state, layout, media contract, CLI behavior |
| [ARCHIVE_OPERATIONS.md](ARCHIVE_OPERATIONS.md) | Routine operation, first run, troubleshooting, backup, incident handling, Kilo handoff |
| [ARCHIVE_RECOVERY.md](ARCHIVE_RECOVERY.md) | Recovery Package creation, validation, provenance, retry, acceptance, rejection |
| [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md) | Regression, integration, sanitizers, Windows qualification, package sealing, local acceptance |
| [DOCUMENTATION_CHECKLIST.md](DOCUMENTATION_CHECKLIST.md) | Contract-change checklist to keep documentation synchronized with implementation |
| [archive-pre-harness-audit-2026-09-14.md](archive-pre-harness-audit-2026-09-14.md) | Frozen forensic audit of the baseline and hardening work |
| [../resources/archive/ARCHIVE_AGENT.md](../resources/archive/ARCHIVE_AGENT.md) | Runtime materialized contract for external agents and recovery automation |

## Authority hierarchy

When documents disagree, use this order:

1. The source code and schema in the exact commit being executed.
2. `resources/archive/ARCHIVE_AGENT.md` and `resources/archive/recovery-package.schema.json` for recovery submissions.
3. The maintained docs in this directory.
4. The dated audit report, which is intentionally historical.
5. Chat transcripts, temporary notes, or exported bundles.

Do not infer current behavior from an older portable package, an older CI run, or the frozen audit without checking the package identity and current source.

## Historical audit note

`archive-pre-harness-audit-2026-09-14.md` was frozen before the final strengthened Windows qualification finished. Its statement that updated Windows CI had not yet run is historically accurate for the moment the audit was frozen, but it is no longer the current project status. The final run `34881209447` later passed both Linux and Windows. Current qualification information belongs in [ARCHIVE_TESTING.md](ARCHIVE_TESTING.md).

## Documentation maintenance rules

Documentation should be updated whenever any of the following changes:

- Archive Root paths or canonical state schema
- recovery package schema or acceptance rules
- media compatibility requirements
- CLI command syntax or exit semantics
- transaction or locking behavior
- portable package sealing and identity checks
- local harness parameters or acceptance criteria
- CI test layers or artifact names
- known limitations or target-host acceptance requirements

A code change that alters one of these contracts is incomplete until the corresponding documentation is updated. Use [DOCUMENTATION_CHECKLIST.md](DOCUMENTATION_CHECKLIST.md) as the release review checklist.
# Archive Mode testing, qualification, and acceptance

This document defines the evidence chain required before Media Downloader PS Archive Mode moves from source development into live Kilo harness testing.

## 1. Qualification identities

The hardening work began from the previously qualified baseline:

- branch: `archive-mode-v1`
- baseline commit: `3b0d705818b00dfd69d5c21b9cadec17f5f1abeb`
- baseline source tree: `fe9052dd75fa9f16451132893c0a938bee11f939`
- baseline GitHub Actions run: `34861991248`

The hardened code baseline documented here is:

- commit: `33cf3150fb7ccc6aa26f0f2d9454db7701baad05`
- source tree: `b8b0c299518a8b81d5731e5da8638e07ed361e40`
- GitHub Actions run: `34881209447`
- Linux job: PASS
- Windows job: PASS

Documentation-only commits can be newer than the hardened code baseline. For any portable package, `build-identity.json` remains the authority for the exact package commit.

## 2. Qualification philosophy

A green compile is not sufficient for Archive Mode because the important risks are state integrity, historical loss, recovery ambiguity, filesystem safety, crash recovery, media validity, and false-positive acceptance.

The test strategy therefore has several independent layers:

1. focused C++ core and hardening regressions;
2. application-level integration tests using the real Archive CLI boundary;
3. real FFmpeg/FFprobe media generation, normalization, probing, and decoding;
4. multi-process concurrency and killed-writer scenarios;
5. ASan and UBSan execution on Linux;
6. native Windows build and full integration execution;
7. self-contained portable package construction and sealing;
8. packaged runtime smoke using pinned yt-dlp, FFmpeg, FFprobe, and Deno;
9. positive and deliberately tampered Windows local-harness tests;
10. final target-host local harness using real YouTube inputs.

Each layer catches a different class of defect. None should be treated as a substitute for the layers after it.

## 3. Baseline regression experiment

Before accepting the hardening changes, the newly introduced regressions were exercised against the unmodified baseline.

The original core test passed, while the 16 newly introduced hardening regression cases failed against the baseline as expected. This established that the tests were detecting real pre-existing gaps rather than merely mirroring the repaired implementation.

The preserved baseline evidence lives under:

```text
qualification/pre-harness/baseline-tests.log
qualification/pre-harness/baseline-tests.xml
```

## 4. Normal local suite

The hardened development tree was configured and built with Qt6 testing enabled.

Representative commands:

```sh
cmake -S . -B build -G Ninja \
  -DBUILD_WITH_QT6=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Result for the hardened pre-harness tree:

```text
18/18 CTest entries PASS
```

The application-level integration CTest contains 22 Python unittest methods. On Linux, 21 execute and pass and the Windows-only PowerShell harness method is intentionally skipped.

## 5. Sanitizer suite

Linux qualification also configures an instrumented Archive build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-sanitized -G Ninja \
  -DBUILD_WITH_QT6=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

The sanitizer run uses leak detection and halt-on-error behavior.

Result:

```text
18/18 CTest entries PASS
no reported ASan or UBSan finding
```

The same Windows-only PowerShell harness method is skipped on Linux.

## 6. Application-level integration coverage

The integration suite uses the built `archive-cli` and a deterministic test boundary for yt-dlp while using real FFmpeg and FFprobe for media behavior.

Coverage includes, among other cases:

- `scan -> sync-item -> verify-item -> idempotent rerun`;
- real media generation and canonical decoding;
- safe normalization with source preservation;
- repair after canonical media deletion;
- corrupt canonical registry refusal without overwrite;
- incomplete discovery preserving historical membership;
- malformed discovery not becoming a false removal event;
- recovery package validation and actual media import;
- failed second representation producing no partial canonical publication;
- retry ordering and download-archive behavior;
- immutable Accepted recovery evidence;
- metadata-only recovery packages;
- invalid CLI input with no filesystem side effect;
- concurrent writer rejection;
- killed-writer lock recovery;
- interrupted multi-file transaction roll-forward;
- transaction preimage conflict preservation;
- malformed transaction journal refusal;
- duplicate playlist occurrences retaining one canonical media identity;
- linked state and junction/path-boundary rejection;
- resource contract upgrade preservation;
- CSV formula neutralization;
- M3U record-injection prevention;
- unavailable-source adoption of valid existing canonical media;
- corrupt playlist history refusal;
- exact-item verification rather than first-file discovery;
- Windows local-harness positive execution;
- deliberate portable-package tampering rejection.

This test surface is intentionally broader than a happy-path download test because Archive Mode's primary failure modes are integrity failures.

## 7. Linux CI qualification

The `linux` job in `.github/workflows/archive-qt6.yml` runs in a Debian trixie container on an Ubuntu 24.04 runner.

It installs a matching Qt6 development toolchain plus Python and FFmpeg, captures source identity, configures and builds the Qt6 project, runs the normal suite with JUnit output, configures a sanitizer build, builds the instrumented Archive targets, runs the sanitizer suite, and uploads the resulting test evidence.

For run `34881209447`, every Linux qualification step completed successfully.

## 8. Windows CI qualification

The `windows` job uses Windows Server 2022, Qt 6.8.1 with matching MinGW 13.1, and Python 3.12.

The job performs:

1. source checkout;
2. Qt and compiler setup;
3. Release configuration and build;
4. assembly of a self-contained portable candidate;
5. installation of pinned, hash-verified yt-dlp, Deno, and FFmpeg/FFprobe runtimes;
6. full normal regression and real-media integration execution;
7. PowerShell syntax validation of the packaged local harness;
8. packaged Archive preflight;
9. portable GUI launch smoke;
10. packaged yt-dlp + FFmpeg + FFprobe end-to-end runtime smoke against a local generated media fixture;
11. advisory hosted YouTube discovery evidence;
12. final build-identity sealing and SHA-256 manifest generation;
13. upload of the portable package and Windows test evidence.

For run `34881209447`, every Windows qualification step completed successfully.

The Windows integration entry executed all 22 integration methods successfully, including the local-harness positive case and deliberate package-tamper rejection.

## 9. Packaged runtime smoke

The portable runtime smoke intentionally avoids relying on hosted YouTube availability as a hard CI gate.

The workflow generates a short audiovisual MP4 using the packaged FFmpeg, serves it from a local HTTP server, downloads it using the packaged yt-dlp configured with packaged FFmpeg, probes the resulting video using packaged FFprobe, extracts AAC audio using packaged FFmpeg, and probes the audio output again.

This establishes that the shipped runtime pieces can work together in the assembled Windows package even if GitHub-hosted access to a real YouTube endpoint is unreliable.

Real YouTube acceptance remains a target-host test.

## 10. Runtime pinning

The qualified Windows package uses pinned runtime downloads whose archives are SHA-256 verified during assembly.

For the documented qualification run, the workflow pinned:

- yt-dlp `2026.08.19`;
- Deno `2.9.6`;
- FFmpeg build `n9.0.1-29-gad500d59cb`.

The portable package also contains `RUNTIME_VERSIONS.txt` so an operator can inspect the exact runtime identities distributed with the candidate.

Do not silently replace one of these executables inside a sealed candidate and continue calling it the qualified package. The local harness is expected to reject changed sealed bytes.

## 11. Portable package sealing

After the Windows tests and runtime smokes pass, the workflow writes:

```text
build-identity.json
PORTABLE_MANIFEST.txt
SHA256SUMS.txt
```

`build-identity.json` binds the portable candidate to repository, commit, workflow run, and qualification state.

A qualified package uses:

```text
qualification = windows-ci-qualified-for-local-harness
```

`SHA256SUMS.txt` seals every packaged file other than itself. The local harness verifies every declared file and rejects undeclared extra files in the extraction directory.

This prevents a stale binary, mixed extraction, edited harness, replaced runtime, or other package mutation from being mistaken for the exact candidate qualified by CI.

## 12. Qualified run artifacts

GitHub Actions run `34881209447` produced these durable workflow artifacts:

| Artifact | Artifact ID | GitHub digest |
| --- | ---: | --- |
| source-33cf3150fb7ccc6aa26f0f2d9454db7701baad05 | `10363186973` | `sha256:665e42c0bfdc7041606e2f89f49a66f4b7722442470a11ca1bbf197def6b86de` |
| linux-test-evidence-33cf3150fb7ccc6aa26f0f2d9454db7701baad05 | `10362533391` | `sha256:1f8ef3c8f403d0cfd597cdfd2f4f87babddaa40648e1a4c41f2826e4e697b617` |
| windows-test-evidence-33cf3150fb7ccc6aa26f0f2d9454db7701baad05 | `10363177553` | `sha256:663125e94ce441305f7c6a3a21ca3a7609efae6afd566f71c82132a12643005f` |
| Media-Downloader-PS-Windows-Qt6-33cf3150fb7ccc6aa26f0f2d9454db7701baad05 | `10362997743` | `sha256:186798f595707f3a8d2ecd257ca207e55c6c374f529e532172010e91b10478a1` |

These GitHub artifact digests identify the uploaded ZIP artifacts. The package's own `SHA256SUMS.txt` separately seals the files inside the portable candidate.

## 13. Preserved repository evidence

The repository includes pre-harness evidence under:

```text
qualification/pre-harness/
```

That directory includes baseline logs, normal test logs and XML, sanitizer configuration/build logs, sanitizer test logs and XML, CTest LastTest logs, and an evidence manifest.

The dated forensic review is:

```text
docs/archive-pre-harness-audit-2026-09-14.md
```

That audit was frozen before the final strengthened Windows run completed. Its historical statement that updated Windows CI had not yet run should not be interpreted as current status. Run `34881209447` later completed successfully on both Linux and Windows.

## 14. Target-host local acceptance

Hosted CI is necessary but not sufficient. The target machine has real environmental variables such as network routing, antivirus, filesystem behavior, Windows path handling, local permissions, yt-dlp extractor behavior, and real YouTube responses.

Run the sealed portable package from a fresh extraction against an empty root:

```powershell
.\archive-local-harness.ps1 `
  -ArchiveRoot 'C:\ArchiveHarness' `
  -PlaylistUrl '<REAL PLAYLIST URL>' `
  -VideoUrl '<REAL VIDEO URL>' `
  -ExpectedCommit '33cf3150fb7ccc6aa26f0f2d9454db7701baad05'
```

If a later package is used, take the expected commit from its `build-identity.json`.

Do not add `-AllowExistingArchive` to the first acceptance run unless the purpose of the test specifically requires pre-existing state.

## 15. What a local-harness PASS proves

For the chosen package, host, playlist, and item, PASS establishes that:

- the package claimed the expected qualified commit;
- the package's declared files matched their SHA-256 hashes;
- required runtime files were sealed;
- no extra unsealed files were present;
- runtime preflight succeeded;
- the playlist produced a complete non-empty discovery snapshot;
- the exact requested item synced successfully;
- the exact requested canonical item verified successfully;
- both video and audio canonical media were present and non-empty;
- the operation could be repeated successfully;
- canonical media SHA-256 values did not change on the immediate rerun;
- a dated evidence receipt was written into the Archive Root.

## 16. What a local-harness PASS does not prove

It does not prove:

- all YouTube playlists behave identically;
- all private or authenticated content is accessible;
- future yt-dlp or website changes will remain compatible;
- every geographic or network environment works;
- every long-running cancellation path is defect-free;
- every storage medium handles abrupt power loss identically;
- every external recovery package is semantically correct;
- all inherited non-Archive downloader engines are qualified.

That broader uncertainty belongs to live Kilo exploratory testing and later production experience.

## 17. Gate before live Kilo testing

Do not start broad live-harness exploration until all of the following are true:

- the exact source commit is known;
- Linux CI is green;
- Windows CI is green;
- normal regression/integration tests pass;
- Linux sanitizer tests pass;
- the Windows portable package is sealed;
- packaged preflight passes;
- packaged GUI smoke passes;
- packaged local runtime media smoke passes;
- target Windows local harness passes using a fresh extraction and real inputs;
- the resulting local evidence receipt is preserved.

After those conditions are met, the candidate is ready for broader Kilo-driven environmental and user-flow testing.

## 18. Live Kilo test priorities

The next phase should emphasize behavior that deterministic CI cannot fully reproduce:

- long real downloads;
- GUI cancellation and restart;
- abrupt GUI/process termination during state and media work;
- repeated playlist scans across real membership changes;
- deleted, private, removed, unavailable, login-required, and authenticated cases using authorized access;
- real-world throttling and transient YouTube errors;
- antivirus and file-lock interactions;
- Unicode and long Windows paths;
- large Archive Roots;
- external Recovery Packages created by the Kilo/research workflow;
- backup, restore, and root relocation using a copy of the archive.

Every live failure should become a reproducible automated regression whenever feasible.

## 19. Regression policy

A defect found during Kilo or production testing should normally result in:

1. a minimized reproduction;
2. a regression test that fails against the affected revision;
3. a repair that preserves existing Archive invariants;
4. normal test rerun;
5. sanitizer rerun where applicable;
6. Windows CI rerun;
7. new portable candidate if runtime behavior changed;
8. local-harness rerun if the package or acceptance path changed;
9. documentation update if any operator, recovery, state, or qualification contract changed.

Do not remove a safety test merely because its failure becomes inconvenient. Change the implementation or explicitly revise the documented contract with evidence.

## 20. Adding tests

New Archive tests should target externally observable invariants rather than private implementation details where possible.

Prefer tests that prove properties such as:

- historical state is not lost after malformed discovery;
- canonical bytes are not overwritten on failed recovery;
- a killed process leaves a recoverable state;
- a second writer cannot mutate the same root concurrently;
- an accepted package cannot be silently replaced;
- a missing canonical file is detected even if state says `complete`;
- an unsafe path is refused before mutation;
- a rerun is idempotent;
- reports cannot inject active spreadsheet formulas or extra M3U records;
- the exact requested item, not an arbitrary file, is used for acceptance.

Tests should leave enough diagnostic context to understand a failure without requiring a live debugger.

## 21. Evidence retention

For each candidate that reaches local acceptance, preserve at minimum:

- exact Git commit;
- CI run ID;
- CI conclusion for Linux and Windows;
- portable artifact identity or digest;
- Windows and Linux test evidence artifacts;
- `build-identity.json`;
- `SHA256SUMS.txt`;
- local-harness evidence receipt;
- any incident logs produced during acceptance.

This makes qualification reproducible and prevents an older or modified binary from being confused with the accepted candidate.

## 22. Current conclusion

Commit `33cf3150fb7ccc6aa26f0f2d9454db7701baad05` completed the strengthened pre-harness CI qualification successfully on both Linux and Windows. The deterministic hardening, integration, sanitizer, portable packaging, GUI smoke, and packaged runtime gates are green.

The remaining pre-live requirement is the real target Windows local harness using the sealed candidate and real authorized YouTube inputs. Once that passes and its receipt is preserved, broad Kilo live-harness testing can begin.
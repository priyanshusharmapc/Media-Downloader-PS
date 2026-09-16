# Archive Mode testing, qualification, and acceptance

This document defines the evidence chain required before Media Downloader PS Archive Mode moves from source development into live Kilo harness testing.

Living documentation intentionally does not embed historical commit IDs, workflow run IDs, artifact IDs, digests, or pinned runtime version numbers. Those values are package-specific and must be read from the candidate package and its exact CI run.

## 1. Qualification identity

For every candidate, establish identity from the exact package and source revision being tested.

The required identity chain is:

```text
source commit
→ Archive Qt6 qualification run for that commit
→ sealed Windows portable artifact from that run
→ build-identity.json inside that artifact
→ SHA256SUMS.txt and RUNTIME_VERSIONS.txt inside that artifact
→ target-host local-harness evidence receipt
```

Do not inherit qualification from an older package merely because the code appears similar. Do not copy an older run ID, digest, or runtime version from documentation.

## 2. Qualification philosophy

A green compile is not sufficient for Archive Mode because the important risks are state integrity, historical loss, recovery ambiguity, filesystem safety, crash recovery, media validity, and false-positive acceptance.

The test strategy therefore has several independent layers:

1. focused C++ core and hardening regressions;
2. application-level integration tests using the real Archive CLI boundary;
3. real FFmpeg/FFprobe media generation, normalization, probing, full stream-integrity decoding, and truncation rejection;
4. multi-process concurrency and killed-writer scenarios;
5. ASan and UBSan execution on Linux;
6. native Windows build and full integration execution;
7. self-contained portable package construction and sealing;
8. packaged runtime smoke using the packaged yt-dlp, FFmpeg, FFprobe, and Deno;
9. positive and deliberately tampered Windows local-harness tests;
10. final target-host local harness using real authorized YouTube inputs.

Each layer catches a different class of defect. None should be treated as a substitute for the layers after it.

## 3. Local regression suite

A representative Qt6 test build is:

```sh
cmake -S . -B build -G Ninja \
  -DBUILD_WITH_QT6=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The repository's current CTest configuration is the authority for the exact number and names of tests. Do not encode a fixed test count into release documentation.

The suite includes focused Archive core/hardening tests and an application-level Python integration entry. Python 3.11 or newer is required by the current CMake contract.

## 4. Sanitizer suite

Linux qualification configures an instrumented Archive build with AddressSanitizer and UndefinedBehaviorSanitizer.

Representative configuration:

```sh
cmake -S . -B build-sanitized -G Ninja \
  -DBUILD_WITH_QT6=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

The qualification workflow defines the exact sanitizer environment and failure policy. A candidate is not sanitizer-qualified if the instrumented test job reports an ASan or UBSan failure.

## 5. Application-level integration coverage

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

The media-integrity regression deliberately creates fast-start MP4/M4A fixtures whose metadata remains readable after truncation. Metadata-only probing must pass the fixture while the canonical verifier must reject it through full FFmpeg stream consumption. A subsequent sync must promote a repaired, decodable representation.

The test source is authoritative if this list ever differs from implementation.

## 6. Linux CI qualification

The `linux` job in `.github/workflows/archive-qt6.yml` is responsible for reproducible Linux qualification.

It must, at minimum:

- prepare the required Qt6, Python, compiler, and media-tool environment;
- capture source identity;
- configure and build the Archive candidate;
- run the normal regression/integration suite with inspectable evidence;
- configure and build the sanitizer targets;
- run the sanitizer suite;
- upload the resulting evidence.

The exact runner image, toolchain versions, and test counts are defined by the workflow at the candidate commit. Check that workflow rather than relying on copied values in docs.

## 7. Windows CI qualification

The `windows` job in `.github/workflows/archive-qt6.yml` is responsible for native Windows qualification and assembly of the target portable candidate.

It must, at minimum:

1. check out the exact source commit;
2. prepare the configured Qt/MinGW/Python environment;
3. configure and build the Release candidate;
4. assemble a self-contained portable package;
5. install the workflow-defined, hash-verified runtime tools;
6. run the normal regression and real-media integration suite;
7. validate the packaged PowerShell harness syntax;
8. run packaged Archive preflight;
9. run a portable GUI launch smoke;
10. run packaged yt-dlp + FFmpeg + FFprobe media smoke;
11. run the GUI settings/package-immutability regression using the shared ArchiveTab settings path;
12. collect advisory hosted YouTube evidence without treating hosted-network availability as the sole hard acceptance gate;
13. seal final build identity and package hashes;
14. upload the portable package and Windows test evidence.

A Windows package is not ready for target-host acceptance unless the job for its exact commit completed successfully and its final `build-identity.json` marks it `windows-ci-qualified-for-local-harness`.

## 8. Packaged runtime smoke

The portable runtime smoke intentionally avoids relying on hosted YouTube availability as a hard CI gate.

The workflow generates a local media fixture, exercises the packaged downloader/media tools together, probes the resulting outputs, and verifies that the packaged runtime pieces interoperate in the assembled Windows candidate.

Real YouTube acceptance remains a target-host test.

## 9. Runtime identity

Runtime versions are candidate-specific.

Inspect:

```text
RUNTIME_VERSIONS.txt
```

inside the extracted portable package for the exact yt-dlp, Deno, FFmpeg, FFprobe, and related runtime identities shipped with that candidate.

Do not silently replace one of these executables inside a sealed candidate and continue calling it the qualified package. The package hashes and local harness are intended to reject changed sealed bytes.

## 10. Portable package sealing

After the Windows tests and runtime smokes pass, the workflow writes package evidence including:

```text
build-identity.json
PORTABLE_MANIFEST.txt
SHA256SUMS.txt
RUNTIME_VERSIONS.txt
```

`build-identity.json` binds the portable candidate to repository, commit, workflow run, and qualification state.

A package ready for target-host acceptance uses:

```text
qualification = windows-ci-qualified-for-local-harness
```

`SHA256SUMS.txt` seals packaged files according to the workflow contract. The local harness verifies declared files and rejects undeclared extra files in the extraction directory.

This prevents a stale binary, mixed extraction, edited harness, replaced runtime, or other package mutation from being mistaken for the exact candidate qualified by CI.

## 11. CI evidence and artifacts

For the candidate being accepted, preserve the artifacts from that exact workflow run. Typical evidence includes:

- source identity artifact;
- Linux test evidence;
- Windows test evidence;
- sealed Windows portable package.

Artifact IDs and GitHub digests belong to the workflow run, not to living documentation. Record them in the acceptance record or release evidence for the specific candidate if long-term traceability is required.

The package's own `SHA256SUMS.txt` separately seals the files inside the portable candidate.

## 12. Repository evidence

Repository-native tests, workflow definitions, harness code, schemas, and source are the durable specification for current behavior.

Qualification logs and exported evidence may be retained under dedicated qualification/evidence paths, but historical results must not override the behavior or identity of a newer candidate.

Git history is the historical record for superseded audits, old baseline identities, and past qualification narratives. Living docs should describe the current contract, not repeat obsolete build metadata.

## 13. Target-host local acceptance

Hosted CI is necessary but not sufficient. The target machine has real environmental variables such as network routing, antivirus, filesystem behavior, Windows path handling, local permissions, yt-dlp extractor behavior, and real YouTube responses.

Run the sealed portable package from a fresh extraction against an empty root. First load the package's own identity:

```powershell
$identity = Get-Content .\build-identity.json -Raw | ConvertFrom-Json
$expectedCommit = $identity.commit
$identity
```

Confirm that `qualification` is `windows-ci-qualified-for-local-harness`, then run:

```powershell
.\archive-local-harness.ps1 `
  -ArchiveRoot 'C:\ArchiveHarness' `
  -PlaylistUrl '<REAL PLAYLIST URL>' `
  -VideoUrl '<REAL VIDEO URL>' `
  -ExpectedCommit $expectedCommit
```

Do not add `-AllowExistingArchive` to the first acceptance run unless the purpose of the test specifically requires pre-existing state.

## 14. What a local-harness PASS proves

For the chosen package, host, playlist, and item, PASS establishes that:

- the package claimed the expected qualified commit;
- the package's declared files matched their hashes;
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

## 15. What a local-harness PASS does not prove

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

## 16. Gate before live Kilo testing

Do not start broad live-harness exploration until all of the following are true for the exact candidate being used:

- the package commit is known from `build-identity.json`;
- Linux CI is green for that commit;
- Windows CI is green for that commit;
- normal regression/integration tests pass;
- Linux sanitizer tests pass;
- the Windows portable package is sealed;
- packaged preflight passes;
- packaged GUI smoke passes;
- packaged local runtime media smoke passes;
- target Windows local harness passes using a fresh extraction and real inputs;
- the resulting local evidence receipt is preserved.

After those conditions are met, the candidate is ready for broader Kilo-driven environmental and user-flow testing.

## 17. Live Kilo test priorities

The live phase should emphasize behavior that deterministic CI cannot fully reproduce:

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

## 18. Regression policy

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

## 19. Adding tests

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

## 20. Evidence retention

For each candidate that reaches local acceptance, preserve at minimum:

- exact Git/package commit;
- CI run identity and conclusions for Linux and Windows;
- portable artifact identity and digest for that run;
- Windows and Linux test evidence artifacts;
- `build-identity.json`;
- `SHA256SUMS.txt`;
- `RUNTIME_VERSIONS.txt`;
- local-harness evidence receipt;
- any incident logs produced during acceptance.

This makes qualification reproducible and prevents an older or modified binary from being confused with the accepted candidate.

## 21. Acceptance conclusion

There is no permanently hardcoded "current qualified candidate" in this document.

The candidate selected for deployment is ready for broad live Kilo testing only when its own identity, CI run, package sealing, packaged smokes, and target-host local-harness evidence all satisfy the gates above.

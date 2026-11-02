# Packet memory internals

> **Status:** Implementation committed at 2007989; verified review edits await commit on task/2-fix-must-have-findings-from-the-sol-6-1.
> **Now:** Callback stress restored to 80,000 packets; pooled: 30 passed; no-pool: 20 passed, 10 skipped; zero failures.
> **Next:** Dispatcher commits the review changes, restamps the sources, and obtains re-review and CI evidence.
> **Watch out:** The original test-first history requirement is unmet; preserve the existing branch/history per the follow-up dispatch. Git metadata is read-only.
> **Last verified against:** 2007989 (2026-11-02)

## Current truth

Library.Initialize(Callbacks) calls native with all delegates held in its local callback structure,
then updates three separate process-lifetime roots according to native acceptance. Incomplete
malloc/free pairs leave every root untouched; valid pairs replace both allocator roots; null
fields retain the corresponding live roots. Native assigns callbacks before platform initialization,
so a platform initialization failure does not undo accepted callback fields. Deinitialize does not
clear roots: outstanding packets can still invoke the allocator. Public signatures are unchanged.

enet_packet_create_offset applies dataOffset to non-null NoAllocate pointers, while preserving
the original null-data behavior and length subtraction. Destruction frees only the packet block.
Invariant: external memory belongs to the caller and must survive the final queued send; the
caller frees the original allocation, never the offset pointer.

ENetPoolCache has a Windows-only retired bit. DllMain sets it for every DLL_THREAD_DETACH,
then retires an active cache once. enet_pool_cache_get refuses retired caches. Invariant: neither
acquisition nor release may reactivate a retired thread cache. An outstanding pooled block is
freed and counted in Freed; new allocations are unpooled and do not change Hits or Misses.
POSIX code and the no-pool allocator path are unchanged. Ordinary pool draining is not retirement.

Packet's existing concurrent free-callback registry remains unchanged. Invariant: the per-packet
delegate remains reachable until destruction on any thread, then runs once and is removed.

## Regression techniques

- The allocator test installs instance delegates through NoInlining helpers and retains only
  WeakReference objects, plus counters that do not point back to the delegates. It runs two
  GC.Collect/WaitForPendingFinalizers cycles per phase. Cleanup replaces any collected native
  pointers before deinitializing and balances successful initialization calls, including the
  fixture's original initialization, then restores that fixture state. NativeMemory requires
  AllowUnsafeBlocks in the test project; production compilation settings are unchanged.
- The packet callback test has four producer threads enqueue 80,000 packets with unique callback
  closures created in a NoInlining helper. Producers exit before GC; four new consumer threads
  dispose the packets, with every ID required to be counted exactly once.
- The offset test verifies direct external storage, the copying path, loopback transmission and
  ownership. Hosts are disposed before freeing external storage, even when delivery fails.
- The fake-detach test uses GetModuleHandleW for the loaded enet.dll and reads AddressOfEntryPoint
  from its PE header (PE signature + 24 + 16). It calls that entry point with reason 3 on a
  dedicated disposable worker, never an xUnit worker. P1 stays live across detach; the test then
  disposes P1 and creates/disposes P2. Statistics recorded immediately after detach establish
  the baseline: Caches is unchanged, ThreadRetained is zero, Hits/Misses are unchanged, and
  Freed rises by exactly one. Cleanup drains any reactivated cache on an unfixed build. The
  real OS detach still follows on thread exit. No test-only native export or production hook
  was introduced. WindowsPoolFact skips this technique on POSIX and no-pool builds.

## Verification evidence

The implementation and its original tests/docs are committed together at 2007989 on
task/2-fix-must-have-findings-from-the-sol-6-1, directly after 49297d5. The review-session drift
check found seven source files changed since the old 49297d5 stamp, with no initial uncommitted
source changes. The contract matches the implementation; the old delivery notes and stamp were
stale. The stamp was computed against committed sources at 2007989 before the callback test
edit. That edit requires another computed stamp after the Dispatcher commits it.

origin/master is 49297d5; local master is stale at 463d57b (2.6.1).
The requested protocol/version search against local master therefore includes
the pre-existing 2.7.0 changes. The same search against 49297d5 and origin/master produces no
output. No version, public callback signature, protocol layout or command table was changed.

The previous implementation session recorded an unfixed four-test run against unchanged
production sources with
3 failed, 1 passed, 0 skipped. Exact failures for the PR description:

- ThreadSafetyTests.Initialize_RejectedOrPartialCallbacks_KeepLiveDelegatesRooted:
  `Rejected init must retain A's malloc delegate`.
- PacketTests.Create_NoAllocateWithOffset_PointsAtOffsetData:
  `Assert.Equal() Failure: Collections differ`; expected `[2, 3, 4, 5]`, actual `[0, 1, 2, 3]`.
- PoolTests.ThreadDetach_AfterRetire_PacketDestroyBypassesPool:
  `Assert.Null() Failure: Value is not null`, containing
  `Retired cache reactivated: Caches 0 -> 1, ThreadRetained 1`.
- ThreadSafetyTests.FreeCallbacks_DisposedOnOtherThreadAfterGC_EachInvokedOnce passed as allowed.

The previous session recorded that the three previously failing tests passed. Its impacted-tests command returned
`full: no node --test suite was found (run the project's own tests)`, authorizing the full runs:

- Windows pooled, Source/Native/build/Release/enet.dll: **30 passed, 0 skipped, 0 failed**.
- Windows no-pool, ENET_NATIVE_LIB_PATH=Source/Native/build-nopool/Release/enet.dll and
  ENET_EXPECT_POOL=0: **20 passed, 10 skipped, 0 failed**.
- Managed Release build: netstandard2.1 and net10.0, **0 warnings, 0 errors**.
- Native pooled and no-pool Release builds: **0 errors**, existing MSVC enum warnings unchanged.
- Builds used the prescribed CMake commands with TrackFileAccess=false. dotnet used --no-restore
  after an offline restore with all package sources disabled and already-cached packages.
  Test runs used --blame-hang-timeout (2 minutes focused, 3 minutes full).
- `git -c core.whitespace=cr-at-eol diff --check` passes; enet.h already uses CRLF in the base blob.
- There are no Unity-generated csproj files or Library/ScriptAssemblies in the owner checkout;
  the relevant compile checks are the two SDK-managed targets above.

Those reproduction results are historical session evidence, not a test-only commit. The prior
session's ignored logs and test-only patch are absent from this fresh worktree; this review pass
does not claim to have rerun the unfixed sources.

Review follow-up verification (2026-11-02, 2007989 plus the callback stress edit):

- `impacted-tests --base origin/master` again returned
  `full: no node --test suite was found (run the project's own tests)`.
- Full Windows pooled suite: **30 passed, 0 skipped, 0 failed** (23 seconds).
- Full Windows no-pool suite with ENET_EXPECT_POOL=0: **20 passed, 10 skipped, 0 failed**
  (21 seconds). Both runs include the 80,000-packet callback test and use a 3-minute hang limit.
- Both native Release variants built successfully; existing MSVC enum warnings remain.
- Managed Release netstandard2.1 and net10.0: **0 warnings, 0 errors**.
- Spec lint and whitespace checks passed. Both spec end-mode entries were read.
- Protocol/version search is empty against 49297d5; the literal local-master search still
  contains the pre-existing 2.6.1-to-2.7.0 changes. No production code changed in this pass.
- Logs are in ignored Source/Native/build/review-*.log. After testing, the generated no-pool
  build directory was moved under Source/Native/build/review-nopool-artifacts to keep generated
  files out of the pending changes; rerun CMake at build-nopool for a subsequent rebuild.
- spec-stamp computed 2007989 with clean sources before the test edit, then correctly refused
  restamping the uncommitted test edit after Git staging was denied.

## Delivery remaining

PR #2 already exists according to the dispatch and received changes requested against 2007989.
Review finding 1 is valid: the committed history does not satisfy the original branch name or
tests-before-fixes acceptance. The current follow-up explicitly requires preserving this branch
and adding work on top. Do not split, revert, rebase or reconstruct the implementation history.
Owner acceptance of the history/branch discrepancy remains required; no waiver is inferred.

The Dispatcher must commit the review edits (Git cannot create index.lock here), rerun
spec-stamp against that source commit, and commit the final docs stamp. Obtain independent
re-review, post the session close on ticket #2, update the existing PR body with verification
counts, and link green test-windows, test-linux, test-macos, test-linux-nopool and sanitize-linux
runs. Remote CI, PR-page state, ticket comment and tag comparison cannot be verified in this
network-disabled session. No push, remote mutation, tag or publish is performed here.

## Tried & rejected

- Committing the review test change: `git add Source/Managed.Tests/ThreadSafetyTests.cs` failed
  because `.git/worktrees/w1/index.lock` is read-only. Leave the review edits for the Dispatcher.
- Reconstructing a test-first history would rewrite 2007989 and conflict with the follow-up's
  explicit instruction to preserve the existing implementation and add commits on top.
- Offline dependency resolution with a relocated DOTNET_CLI_HOME initially missed cached
  packages (NU1101). Pointing NUGET_PACKAGES at the existing user cache succeeded with all
  package sources disabled; no packages were downloaded.
- Default MSBuild file tracking: CMake compiler detection failed with FileTracker
  UnauthorizedAccessException. `TrackFileAccess=false` permits the installed MSVC build.
- Updating roots only when the native return value is zero would miss accepted callbacks when
  platform initialization fails: native assigns callbacks before calling enet_initialize.
- The exact protocol search against local master is not a valid baseline here: it reports the
  pre-existing 2.6.1-to-2.7.0 release diff. Verify against supplied base 49297d5 and origin/master;
  do not rewrite versions or move refs to hide that discrepancy.
- The spec-stamp script refuses dirty feature sources. The computed 2007989 stamp covers the
  committed implementation; it cannot cover the pending callback stress edit until committed.

---

### 2026-11-02 · Reproduction · @49297d5
Did: Added the four requested tests and ran them against unchanged production sources.
Why: Runtime evidence is required before fixing the reported findings.
Tried & rejected: See above; no production fix was made before reproduction.
Next: Reproduction completed. F1 failed with "Rejected init must retain A's malloc delegate";
F2 expected [2, 3, 4, 5], got [0, 1, 2, 3]; F5 reported "Retired cache reactivated:
Caches 0 -> 1, ThreadRetained 1". F9 passed. Totals: 3 failed, 1 passed, 0 skipped.

### 2026-11-02 · Implementation and Windows verification · working tree based on @49297d5
Did: Implemented per-field callback roots, NoAllocate offsets and terminal Windows retirement;
strengthened cross-thread callback coverage and back-filled the contract/invariants.
Why: Each production defect reproduced before its fix; the F9 test needed stronger lifetime coverage.
Tried & rejected: Read-only Git metadata prevents commits/stamping; stale local master prevents
claiming an empty literal master diff. Evidence and workarounds are recorded above.
Next: Dispatcher commit/stamp, ticket session-close comment, PR, independent review and remote CI.

### 2026-11-02 · Review follow-up · @2007989 plus pending review edits
Did: Corrected stale delivery state and computed the committed-source stamp; restored the callback
test to four producers with 20,000 packets each and its Dictionary concurrency explanation.
Why: Review finding 2 identified stale session memory; finding 3 identified reduced stress coverage.
Tried & rejected: Rewriting history conflicts with the follow-up dispatch; staging fails on read-only
index.lock. Finding 1 remains open for owner acceptance, not dismissed as an invalid finding.
Next: Both Windows variants passed; Dispatcher commit/restamp, re-review and remote checks remain.

## Spec end-mode
The implementation is committed at 2007989. Review edits are pending Dispatcher commit and
restamping; finding 1, re-review, remote CI, ticket close comment and remote tag comparison remain open.

# Packet memory internals

> **Status:** Implementation and Windows verification complete; uncommitted, awaiting independent review.
> **Now:** Pooled tests: 30 passed; no-pool: 20 passed, 10 skipped; both have 0 failures.
> **Next:** Dispatcher commits tests first, then fixes/docs, stamps this spec, and opens the PR for review/CI.
> **Watch out:** Git metadata is read-only; local master is stale. Keep callbacks rooted and retirement terminal.
> **Last verified against:** 49297d5 + uncommitted working-tree changes (2026-11-02; stamp blocked)

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
- The packet callback test has four producer threads enqueue 4000 packets with unique callback
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

No existing feature spec or Tried & rejected section existed at session start. The drift script
reported missing internals.md. HEAD and origin/master are 49297d5; local master is stale at
463d57b (2.6.1). The requested protocol/version search against local master therefore includes
the pre-existing 2.7.0 changes. The same search against 49297d5 and origin/master produces no
output. No version, public callback signature, protocol layout or command table was changed.

The unfixed four-test run compiled the new tests against unchanged production sources and gave
3 failed, 1 passed, 0 skipped. Exact failures for the PR description:

- ThreadSafetyTests.Initialize_RejectedOrPartialCallbacks_KeepLiveDelegatesRooted:
  `Rejected init must retain A's malloc delegate`.
- PacketTests.Create_NoAllocateWithOffset_PointsAtOffsetData:
  `Assert.Equal() Failure: Collections differ`; expected `[2, 3, 4, 5]`, actual `[0, 1, 2, 3]`.
- PoolTests.ThreadDetach_AfterRetire_PacketDestroyBypassesPool:
  `Assert.Null() Failure: Value is not null`, containing
  `Retired cache reactivated: Caches 0 -> 1, ThreadRetained 1`.
- ThreadSafetyTests.FreeCallbacks_DisposedOnOtherThreadAfterGC_EachInvokedOnce passed as allowed.

After the fixes, the three previously failing tests passed. The impacted-tests command returned
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

Local logs and the test-first patch are in the ignored Source/Native/build directory:
tests-before-fixes.patch, before-fixes.log, after-fixes.log, regression-pooled.log,
regression-nopool.log, managed-build.log, fixed-build.log and nopool-build.log.

## Delivery remaining

The Dispatcher must create task/2-sol-review-must-haves from 49297d5 and commit the test-only
files first, then the implementation and documentation. No commits could be written here.
Run spec-stamp after committing source, update the session hash, lint, and commit the docs.
The test-only patch preserves the first part; the implementation was changed only after all
three expected failures were observed. Suggested commits: `test: reproduce callback roots,
packet offsets and thread retirement`; `fix: preserve accepted native callback roots`;
`fix: apply offsets to caller-owned packet data`; `fix: keep Windows thread caches retired`;
`docs: record packet memory contracts and regression evidence`.

Post the session close on ticket #2, open the PR to master, attach the failure evidence and both
suite counts, and obtain independent review. The existing CI jobs test-windows, test-linux,
test-macos, test-linux-nopool and sanitize-linux are present but were not run remotely here.
Link their green run in the PR. No network access, push, remote mutation, tag or publish was
performed; remote tag comparison and PR-page verification remain for the Dispatcher.

## Tried & rejected

- Committing tests and creating the requested branch: Git cannot write the owner's read-only
  `.git/refs` or `.git/worktrees/w1/index.lock`. Leave changes uncommitted for the Dispatcher;
  `Source/Native/build/tests-before-fixes.patch` preserves the test-only first part.
- Default MSBuild file tracking: CMake compiler detection failed with FileTracker
  UnauthorizedAccessException. `TrackFileAccess=false` permits the installed MSVC build.
- Updating roots only when the native return value is zero would miss accepted callbacks when
  platform initialization fails: native assigns callbacks before calling enet_initialize.
- The exact protocol search against local master is not a valid baseline here: it reports the
  pre-existing 2.6.1-to-2.7.0 release diff. Verify against supplied base 49297d5 and origin/master;
  do not rewrite versions or move refs to hide that discrepancy.
- The spec-stamp script refuses dirty feature sources. Since Git writes are denied, do not
  fabricate a verified commit stamp; leave the working-tree verification explicitly labeled.

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

## Spec end-mode
Implementation and local Windows verification are complete. Delivery is partial: Git commits,
computed stamp, ticket comment, PR, independent review, remote CI and remote tag comparison remain.

---
feature: packet-memory
source:
  - Source/Native/enet.h
  - Source/Managed/ENet.cs
  - Source/Managed.Tests/ThreadSafetyTests.cs
  - Source/Managed.Tests/PacketTests.cs
  - Source/Managed.Tests/PoolTests.cs
  - Source/Managed.Tests/ENetFixture.cs
  - Source/Managed.Tests/ENet-CSharp.Tests.csproj
---

# Packet memory contract

Scope: the native Callbacks, Buffer pool and Packet sections, managed Library callback roots,
and managed Packet free callbacks. Protocol layouts and both library versions remain unchanged.

## Callback lifetime

Library roots malloc, free and noMemory separately for as long as native can reference them,
including after Deinitialize. Initialization is serialized by the caller, as with native ENet;
this does not add concurrent allocator replacement support.

A malloc/free pair is either fully supplied or fully omitted. An incomplete pair rejects the
whole callback update and preserves every existing root, including noMemory. A complete pair
replaces both allocator roots; an omitted pair preserves them. A non-null noMemory replaces
only that root; null preserves it. Native accepts these fields before platform initialization,
so accepted delegates must remain rooted even if platform initialization subsequently fails.
The public Callbacks constructor and Library.Initialize(Callbacks) signatures stay unchanged.

Changing allocator families while blocks are live or cached is unsupported: later frees use the
current allocator. Tests drain the pool before replacement and use compatible pass-through
NativeMemory allocation/free callbacks. Per-packet callbacks remain rooted by the concurrent
packet registry until destruction, including handoff to a different thread after a full GC.
Each callback is removed and invoked once for its packet.

## External packet storage

Create(data, offset, length, NoAllocate) exposes data + offset with Length == length - offset.
The caller owns the original allocation and must keep it valid until the packet is destroyed,
including all queued sends. ENet frees the packet header/block, never the external data.
The caller frees the original base pointer, not the offset pointer. The copying path exposes
the same byte range in ENet-owned storage. Managed arrays cannot be used with NoAllocate.
Offsets outside [0, length] are rejected; the native null-data behavior remains unchanged.

## Thread retirement

In Windows DLL builds with the existing MSVC/clang-cl detach hook, DLL_THREAD_DETACH permanently
retires that thread's cache, even when it was not active. No subsequent acquire or release on
that thread may reactivate it. Acquisitions bypass the pool with pooled == 0; release of a
previously pooled block calls enet_free and increments Freed. It must not retain that block.
Ordinary DrainPool and Deinitialize are not terminal retirement. POSIX destructor behavior,
Windows static/MinGW limitations, and ENET_NO_POOL behavior are unchanged.

## Acceptance evidence

- ThreadSafetyTests.Initialize_RejectedOrPartialCallbacks_KeepLiveDelegatesRooted uses weak
  references and two GC/finalizer cycles per assertion phase, then 1000 oversized packets.
- PacketTests.Create_NoAllocateWithOffset_PointsAtOffsetData checks external and copied bytes,
  pointer/length, loopback delivery and caller-owned deallocation.
- PoolTests.ThreadDetach_AfterRetire_PacketDestroyBypassesPool exercises a real Windows DLL
  entry point on a disposable worker and checks cache, retention, hit/miss and free counters.
- ThreadSafetyTests.FreeCallbacks_DisposedOnOtherThreadAfterGC_EachInvokedOnce uses four
  producers, a queue, separate consumers, full GC and unique per-packet callback counts.
- The Windows pooled and no-pool suites, both managed targets and both native builds must pass.
  Dispatcher CI must additionally verify Linux, macOS and Linux sanitizers before merge.

## Spec end-mode
Contract back-filled for ticket #2. Runtime results and remaining delivery checks are recorded
in internals.md; independent review and remote CI are required before merge.

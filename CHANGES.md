ENet-CSharp-FigNet 2.7.1
--------

- Keep each live native allocator callback rooted across rejected or partial initialization (F1).
- Apply the offset to caller-owned NoAllocate packet data without changing ownership (F2).
- Permanently retire Windows thread caches at DLL_THREAD_DETACH so later cleanup bypasses pooling (F5).
- Strengthen packet free-callback tests with cross-thread disposal after garbage collection and one invocation per packet (F9).
- Version bumped to 2.7.1 in lockstep (`ENET_VERSION`, managed `Library.version`, package, assembly and file versions); no wire-protocol change, interoperable with 2.7.0, 2.6.x and 2.5.3 peers.

ENet-CSharp-FigNet 2.7.0
--------

- **Fixed a native crash when hosts are serviced on more than one thread** (bragvr-gdd#351). The 2.6.x packet-buffer pool kept one process-wide free list with no synchronization, so two threads creating or destroying packets at once (for example FigNet's Entangle and FnVoice sockets, each on its own network thread) could hand the same block to two packets: heap corruption, or an access violation in `enet_packet_create`. The pool is now lock-free and thread-safe: every thread owns a cache, so the hot path takes no lock and performs no atomic read-modify-write (counts are published in batches). A packet may be destroyed on a different thread than the one that created it.
- Each thread cache is a fixed array of 128 block pointers used as a stack (an index move per create and destroy). The array replaced a per-thread linked free list after both were benchmarked: they measured the same in every cell, and pre-allocated contiguous slabs were no faster and could not return memory.
- Pool blocks are now 1288 bytes (payloads up to 1248 bytes on 64-bit), so every packet ENet sends unfragmented at the default 1280-byte MTU (payloads up to 1244 bytes) is pooled.
- Threads that exit park their cached blocks process-wide, and the next thread whose cache runs dry adopts them (a reconnect's new network thread reuses its predecessor's blocks). The exit hook is a pthread key destructor on POSIX and `DLL_THREAD_DETACH` in the Windows DLL built with MSVC or clang-cl; static Windows and MinGW builds should call `DrainPool()` before a thread exits.
- Pool semantics: the cap is 128 blocks per thread (2.6.x: 576 for the whole process); `DrainPool()` frees the calling thread's cache plus parked blocks; counters are process-lifetime totals and are no longer reset by `Initialize()`. Statistics are safe to read from any thread (threads publish in small batches).
- New native exports (additive; existing ones unchanged): `enet_pool_get_statistics_ex` (adds `freed`, `drained`, `threadRetained`, `orphaned`, `caches`) and `enet_pool_get_block_size` (0 when built with `ENET_NO_POOL`). Managed: `PoolStatistics` gains `Freed`, `Drained`, `ThreadRetained`, `Orphaned` and `Caches`; new `Library.PoolBlockSize`.
- Fixed a race in the managed wrapper: `Packet.SetFreeCallback`'s delegate registry was a plain `Dictionary` shared by all threads (and written from the thread that destroys the packet); it is now a `ConcurrentDictionary`.
- The managed free-callback thunk carries a `MonoPInvokeCallback` attribute, which IL2CPP requires before it can hand the callback to native code (`Packet.SetFreeCallback(PacketFreeCallback)`).
- Fixed a race in the Windows clock: `clock_gettime`'s lazily initialized statics could be read half-initialized by a second thread (divide by zero, garbage time). It is now stateless.
- CMake: new `ENET_NO_POOL` option; Unix builds link pthreads; `ENET_DLL` is now defined for the shared library only, so a static library configured alongside it no longer exports symbols or carries the DLL's `DllMain`.
- Tests: multi-threaded regression suite (the #351 repro, cross-thread create/destroy, two hosts serviced on two threads, thread exit and reconnect cycles, counter conservation, concurrent free callbacks); pool-counter tests skip against `ENET_NO_POOL` builds. New `Source/Managed.Benchmarks` (allocator benchmark, crash repro and multi-process stress test with telemetry) and `Source/Native/bench` (native benchmark and sanitizer stress harness).
- Version bumped to 2.7.0 in lockstep (`ENET_VERSION`, managed `Library.version`); no wire-protocol change, interoperable with 2.6.x and 2.5.3 peers.

ENet-CSharp-FigNet 2.6.1
--------

- Published to nuget.org as `ENet-CSharp-FigNet` (public, anonymous consumption; the `ENet-CSharp` id belongs to the original author). CI publishes via NuGet Trusted Publishing (OIDC) - no API key stored. The assembly name (`ENet-CSharp.dll`) and namespace (`ENet`) are unchanged, so it is a drop-in for the original `ENet-CSharp` package.
- Removed the upstream "Supporters" section from the README; README now documents nuget.org installation.
- Version bumped to 2.6.1 in lockstep (`ENET_VERSION`, managed `Library.version`); no wire-protocol change, interoperable with 2.6.0/2.5.3 peers.

ENet-CSharp 2.6.0
--------

- Fixed-size native packet-buffer pooling: packet blocks up to 1280 bytes (~1240-byte payloads on 64-bit, covering game/MTU-class packets) are recycled through a shared pool capped at 576 retained blocks, with transparent malloc fallback for larger packets; block size is the tunable `ENET_POOL_BLOCK_SIZE`; pool statistics via `Library.GetPoolStatistics()` / `enet_pool_get_statistics`, drain via `Library.DrainPool()` / `enet_pool_drain`, opt-out via the `ENET_NO_POOL` compile definition
- Managed project retargeted to `netstandard2.1` + `net10.0` (dropped EOL `netcoreapp3.1`/`net8.0`), packaging moved from the stale nuspec into the csproj, embedded PDBs
- Distribution moved to GitHub Packages under `Ahmed310` (nuget.org untouched); CI now tests on Windows/Linux/macOS against the built binaries, packs, and publishes on version tags; macOS native library is now universal (arm64 + x86_64)
- Wrapper stability fixes: native-callback delegates are rooted so the GC cannot collect them while native holds the function pointer (`Packet.SetFreeCallback`, `Host.SetInterceptCallback`/`SetChecksumCallback`, `Library.Initialize(Callbacks)`); `Packet.Create` validates `offset <= length` (prevented a native size_t underflow) and rejects `NoAllocate` with managed arrays (dangling pointer); `Packet.CopyTo` validates the destination size; packet creation failures now throw instead of yielding an unset packet; callback delegates carry `[UnmanagedFunctionPointer(Cdecl)]`
- Native fixes: `enet_packet_create_offset` rejects `dataOffset > dataLength`
- New test suite (`Source/Managed.Tests`) covering connect/disconnect, payload boundaries (984/985/1024), fragmentation, 10k-packet loops, pool exhaustion, burst sends over multiple peers, and shutdown with held buffers
- Version lockstep: managed `Library.version` and native `ENET_VERSION` both report 2.6.0; the wire protocol is unchanged and remains interoperable with 2.5.3 peers

The most notable changes that were made before 2.0.3 version
--------

- Added functionality for easier bindings
- Added monotonic time
- Improved connection-related calculations
- Improved compatibility with various compilers
- Improved transmission statistics
- Eliminated unnecessary memory allocations
- Removed/replaced legacy functionality
- Amalgamated code base into a single header
- Cleaned and reorganized code base

For other changes, check the [release section](https://github.com/nxrighthere/ENet-CSharp/releases).

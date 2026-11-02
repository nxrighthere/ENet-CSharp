using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Xunit;
using Xunit.Abstractions;

namespace ENet.Tests {
	/// <summary>
	/// Packets and hosts used from several threads at once, the way FigNet runs one network thread per
	/// socket (Entangle and FnVoice in one client). Against 2.6.1 the first tests here kill the test host
	/// within seconds (heap corruption or an access violation inside enet_packet_create), because the
	/// buffer pool's free list was process-global and unsynchronized.
	/// </summary>
	[Collection("ENet")]
	[FlushPoolCounters]
	public class ThreadSafetyTests {
		private readonly ITestOutputHelper output;

		// Set as soon as any worker fails, so the others stop instead of blocking or spinning
		private static volatile bool abort;

		public ThreadSafetyTests(ITestOutputHelper output) {
			this.output = output;
		}

		[Theory]
		[InlineData(2)]
		[InlineData(4)]
		public void ConcurrentCreateDestroy_HeldPackets_NoCorruption(int threadCount) {
			// The bragvr-gdd#351 repro: each thread holds 8 packets of 1-180 bytes, then destroys them.
			// Two threads sharing one block would also show up here as a payload mismatch.
			TimeSpan duration = SoakDuration(5);
			long total = 0;

			RunThreads(threadCount, index => {
				Random random = new Random(1000 + index);
				byte[] data = new byte[180];
				byte[] check = new byte[180];
				Packet[] held = new Packet[8];
				int[] lengths = new int[held.Length];
				Stopwatch stopwatch = Stopwatch.StartNew();
				long created = 0;

				while (stopwatch.Elapsed < duration && !abort) {
					for (int i = 0; i < held.Length; i++) {
						lengths[i] = 1 + random.Next(180);

						FillPattern(data, lengths[i], (int)(created + i) ^ (index << 24));

						held[i].Create(data, lengths[i], PacketFlags.Unsequenced);
					}

					for (int i = 0; i < held.Length; i++) {
						Assert.Equal(lengths[i], held[i].Length);

						held[i].CopyTo(check);

						Assert.True(HasPattern(check, lengths[i], (int)(created + i) ^ (index << 24)), "A held packet's payload was overwritten by another thread");

						held[i].Dispose();
					}

					created += held.Length;
				}

				Interlocked.Add(ref total, created);
			});

			output.WriteLine($"{threadCount} threads: {total:N0} packets in {duration.TotalSeconds:F0} s");
		}

		[Fact]
		public void CrossThread_CreateOnOneDestroyOnAnother_PayloadsIntact() {
			// Blocks migrate between thread caches; sizes also cover the oversized (non-pooled) path
			TimeSpan duration = SoakDuration(3);
			const int pairs = 2;

			BlockingCollection<Packet>[] queues = new BlockingCollection<Packet>[pairs];

			for (int i = 0; i < pairs; i++)
				queues[i] = new BlockingCollection<Packet>(256);

			long transferred = 0;

			RunThreads(pairs * 2, index => {
				BlockingCollection<Packet> queue = queues[index / 2];

				if (index % 2 == 0) {
					byte[] data = new byte[1300];
					Stopwatch stopwatch = Stopwatch.StartNew();
					int sequence = 0;

					try {
						while (stopwatch.Elapsed < duration && !abort) {
							int length = SequenceLength(sequence, 1300);

							FillPattern(data, length, sequence);

							Packet packet = new Packet();
							packet.Create(data, length, PacketFlags.Reliable);

							// Bounded queue: never block forever on a consumer that failed
							while (!queue.TryAdd(packet, 50)) {
								if (abort) {
									packet.Dispose();

									return;
								}
							}

							sequence++;
						}
					} finally {
						queue.CompleteAdding();
					}
				} else {
					byte[] check = new byte[1300];
					int expected = 0;

					foreach (Packet received in queue.GetConsumingEnumerable()) {
						Packet packet = received;
						int length = SequenceLength(expected, 1300);

						Assert.Equal(length, packet.Length);

						packet.CopyTo(check);

						Assert.True(HasPattern(check, length, expected), $"Packet {expected} arrived corrupted on the consumer thread");

						packet.Dispose();

						expected++;
					}

					Interlocked.Add(ref transferred, expected);
				}
			});

			output.WriteLine($"{transferred:N0} packets created on one thread and destroyed on another");
		}

		[Fact]
		public void MixedHandoff_FourThreads_PayloadsIntact() {
			long operations = RunMixedHandoff(4, SoakDuration(3));

			output.WriteLine($"4 threads: {operations:N0} packets with random cross-thread handoff");
		}

		[Fact]
		public void TwoHostPairs_ServicedOnSeparateThreads_ReliableTrafficIntact() {
			// Two independent hosts in one process, each serviced by its own thread, as FigNet does for
			// Entangle and FnVoice. Every host is touched by exactly one thread, which is ENet's contract.
			TimeSpan duration = SoakDuration(4);
			long delivered = 0;

			RunThreads(2, index => {
				using LoopbackPair pair = new LoopbackPair();

				int payloadSize = index == 0 ? 181 : 53;
				byte[] data = new byte[payloadSize];
				byte[] check = new byte[payloadSize];
				int sent = 0;
				int received = 0;
				int clientSequence = 0;
				int serverSequence = 0;
				int nextFromClient = 0;
				int nextFromServer = 0;
				bool intact = true;

				Action<Host, Event> onEvent = (host, e) => {
					if (e.Type != EventType.Receive)
						return;

					Packet packet = e.Packet;
					int expected = host == pair.Server ? nextFromClient++ : nextFromServer++;

					if (packet.Length != payloadSize) {
						intact = false;
					} else {
						packet.CopyTo(check);

						if (!HasPattern(check, payloadSize, expected))
							intact = false;
					}

					packet.Dispose();
					received++;
				};

				Stopwatch stopwatch = Stopwatch.StartNew();

				while (stopwatch.Elapsed < duration && !abort) {
					for (int i = 0; i < 10; i++) {
						FillPattern(data, payloadSize, clientSequence++);

						Packet toServer = new Packet();
						toServer.Create(data, payloadSize, PacketFlags.Reliable);

						Assert.True(pair.ClientToServer.Send(0, ref toServer));

						FillPattern(data, payloadSize, serverSequence++);

						Packet toClient = new Packet();
						toClient.Create(data, payloadSize, PacketFlags.Reliable);

						Assert.True(pair.ServerToClient.Send(0, ref toClient));

						sent += 2;
					}

					LoopbackPair.DrainHost(pair.Server, onEvent);
					LoopbackPair.DrainHost(pair.Client, onEvent);
				}

				Assert.True(pair.PumpUntil(() => received >= sent, onEvent, 30000), $"Timed out at {received}/{sent} packets on pair {index}");
				Assert.True(intact, $"A packet arrived corrupted or out of order on pair {index}");

				Interlocked.Add(ref delivered, received);
			});

			output.WriteLine($"{delivered:N0} reliable packets delivered across two concurrently serviced host pairs");
		}

		[Fact]
		public void FreeCallbacks_DisposedOnOtherThreadAfterGC_EachInvokedOnce() {
			const int threadCount = 4;
			// 2.6.1 used a plain Dictionary; concurrent registration and removal need sustained contention.
			const int perThread = 20000;
			int[] invoked = new int[threadCount * perThread];
			using BlockingCollection<Packet> queue = new BlockingCollection<Packet>();

			RunThreads(threadCount, index => {
				for (int i = 0; i < perThread; i++)
					queue.Add(CreateCallbackPacket(index * perThread + i, invoked));
			});

			queue.CompleteAdding();
			CollectDelegates();

			// Producers have exited and hold no delegates; different threads destroy their packets.
			RunThreads(threadCount, index => {
				foreach (Packet received in queue.GetConsumingEnumerable()) {
					Packet packet = received;
					packet.Dispose();
				}
			});

			int total = 0;

			for (int id = 0; id < invoked.Length; id++) {
				Assert.True(invoked[id] == 1, $"Packet {id}: expected one callback, got {invoked[id]}");
				total += invoked[id];
			}

			Assert.Equal(threadCount * perThread, total);
		}

		[MethodImpl(MethodImplOptions.NoInlining)]
		private static Packet CreateCallbackPacket(int id, int[] invoked) {
			Packet packet = new Packet();
			packet.Create(new byte[32]);
			packet.SetFreeCallback(freed => Interlocked.Increment(ref invoked[id]));
			return packet;
		}

		[Fact]
		public void Initialize_RejectedOrPartialCallbacks_KeepLiveDelegatesRooted() {
			Library.DrainPool();
			AllocatorCounters counters = new AllocatorCounters();
			int initialized = 0;

			try {
				WeakReference[] original = InstallAllocator(counters);
				initialized++;
				RejectPartialAllocator(new AllocatorCounters());

				CollectDelegates();
				Assert.True(original[0].IsAlive, "Rejected init must retain A's malloc delegate");
				Assert.True(original[1].IsAlive, "Rejected init must retain A's free delegate");
				Assert.True(original[2].IsAlive, "Rejected init must retain A's noMemory delegate");

				WeakReference replacement = InstallNoMemory(new AllocatorCounters());
				initialized++;

				CollectDelegates();
				Assert.True(original[0].IsAlive, "Partial init must retain A's malloc delegate");
				Assert.True(original[1].IsAlive, "Partial init must retain A's free delegate");
				Assert.True(replacement.IsAlive, "Partial init must retain C's noMemory delegate");

				long allocations = Interlocked.Read(ref counters.Allocations);
				long frees = Interlocked.Read(ref counters.Frees);

				for (int i = 0; i < 1000; i++) {
					Packet packet = new Packet();
					packet.Create(new byte[2000]);
					packet.Dispose();
				}

				Assert.True(Interlocked.Read(ref counters.Allocations) - allocations >= 1000, "A's malloc must service 1000 oversized packets");
				Assert.True(Interlocked.Read(ref counters.Frees) - frees >= 1000, "A's free must release 1000 oversized packets");
			} finally {
				// Replace dangling pointers even on the unfixed build before any drain can invoke them.
				InstallAllocator(new AllocatorCounters());
				// Balance the fixture's init, this test's successful inits and the cleanup init (WSAStartup).
				for (int i = 0; i < initialized + 2; i++)
					Library.Deinitialize();
				Assert.True(Library.Initialize(), "Restore the fixture's initialized state");
			}
		}

		private sealed class AllocatorCounters {
			public long Allocations;
			public long Frees;
			public long OutOfMemory;

			public unsafe IntPtr Allocate(IntPtr size) {
				Interlocked.Increment(ref Allocations);
				return (IntPtr)NativeMemory.Alloc((nuint)size);
			}

			public unsafe void Free(IntPtr memory) {
				Interlocked.Increment(ref Frees);
				NativeMemory.Free((void*)memory);
			}

			public void NoMemory() {
				Interlocked.Increment(ref OutOfMemory);
			}
		}

		[MethodImpl(MethodImplOptions.NoInlining)]
		private static WeakReference[] InstallAllocator(AllocatorCounters counters) {
			AllocCallback malloc = counters.Allocate;
			FreeCallback free = counters.Free;
			NoMemoryCallback noMemory = counters.NoMemory;
			Assert.True(Library.Initialize(new Callbacks(malloc, free, noMemory)));
			return new[] { new WeakReference(malloc), new WeakReference(free), new WeakReference(noMemory) };
		}

		[MethodImpl(MethodImplOptions.NoInlining)]
		private static void RejectPartialAllocator(AllocatorCounters counters) {
			Assert.False(Library.Initialize(new Callbacks(counters.Allocate, null, null)), "Native must reject a malloc-only init");
		}

		[MethodImpl(MethodImplOptions.NoInlining)]
		private static WeakReference InstallNoMemory(AllocatorCounters counters) {
			NoMemoryCallback noMemory = counters.NoMemory;
			Assert.True(Library.Initialize(new Callbacks(null, null, noMemory)));
			return new WeakReference(noMemory);
		}

		private static void CollectDelegates() {
			for (int i = 0; i < 2; i++) {
				GC.Collect();
				GC.WaitForPendingFinalizers();
			}
		}

		[Fact]
		public void Time_ReadFromSeveralThreads_NeverGoesBackwards() {
			RunThreads(4, index => {
				uint previous = Library.Time;

				for (int i = 0; i < 200000; i++) {
					uint now = Library.Time;

					// Unsigned difference: a step backwards wraps to a huge value
					Assert.True(now - previous < 60000, $"Library.Time went from {previous} to {now}");

					previous = now;
				}
			});
		}

		[PoolFact]
		public void ThreadExit_ParksCachedBlocks_AndTheNextThreadAdoptsThem() {
			const int blocks = 64;

			Library.DrainPool();

			PoolStatistics inside = default(PoolStatistics);

			RunThreads(1, index => {
				CreateAndDestroy(blocks, 200);

				inside = Library.GetPoolStatistics();
			});

			Assert.True(inside.ThreadRetained >= blocks, $"Expected the worker to cache {blocks} blocks, it cached {inside.ThreadRetained}");

			// The exit hook runs while the OS thread winds down, which can finish just after Join returns
			Assert.True(SpinWait.SpinUntil(() => Library.GetPoolStatistics().Orphaned >= blocks, 5000), "The exited thread's cached blocks were not parked for reuse");

			PoolStatistics before = default(PoolStatistics);
			PoolStatistics after = default(PoolStatistics);

			RunThreads(1, index => {
				before = Library.GetPoolStatistics();

				CreateAndDestroy(blocks, 200);

				after = Library.GetPoolStatistics();
			});

			Assert.Equal(0ul, after.Misses - before.Misses);
			Assert.Equal((ulong)blocks, after.Hits - before.Hits);

			Library.DrainPool();
		}

		[PoolFact]
		public void ThreadStartStopCycles_ReuseBlocksInsteadOfAllocating() {
			// A reconnect starts a new network thread each time; its predecessor's blocks must be reused,
			// not leaked and not reallocated
			const int cycles = 100;
			const int blocks = 32;

			Library.DrainPool();

			PoolStatistics start = Library.GetPoolStatistics();

			for (int cycle = 0; cycle < cycles; cycle++) {
				RunThreads(1, index => CreateAndDestroy(blocks, 300));

				Assert.True(SpinWait.SpinUntil(() => Library.GetPoolStatistics().Orphaned >= blocks, 5000), $"Cycle {cycle}: the exited thread's blocks were not parked");
			}

			PoolStatistics end = Library.GetPoolStatistics();

			Assert.True(end.Misses - start.Misses <= blocks, $"Expected only the first cycle to allocate, got {end.Misses - start.Misses} misses over {cycles} cycles");
			Assert.True(end.Orphaned <= 128, $"Orphaned blocks grew to {end.Orphaned}");

			output.WriteLine($"{cycles} thread cycles: {end.Misses - start.Misses} misses, {end.Hits - start.Hits} hits, {end.Orphaned} orphaned at the end");

			Library.DrainPool();
		}

		[PoolFact]
		public void DrainPool_OnWorkerThreadBeforeExit_FreesItsBlocks() {
			const int blocks = 64;

			Library.DrainPool();

			PoolStatistics cached = default(PoolStatistics);
			PoolStatistics drained = default(PoolStatistics);

			RunThreads(1, index => {
				CreateAndDestroy(blocks, 100);

				cached = Library.GetPoolStatistics();

				Library.DrainPool();

				drained = Library.GetPoolStatistics();
			});

			Assert.True(cached.ThreadRetained >= blocks);
			Assert.Equal(0u, drained.ThreadRetained);
			Assert.True(drained.Drained - cached.Drained >= blocks, "DrainPool must free the calling thread's cached blocks");
		}

		[PoolFact]
		public void Counters_Conserve_AfterMultiThreadedChurn() {
			Library.DrainPool();

			PoolStatistics before = Library.GetPoolStatistics();

			// Each worker publishes its own pending counts before exiting (RunThreads)
			RunMixedHandoff(4, SoakDuration(2));

			PoolStatistics after = Library.GetPoolStatistics();

			ulong hits = after.Hits - before.Hits;
			ulong misses = after.Misses - before.Misses;
			ulong returned = after.Returned - before.Returned;
			ulong freed = after.Freed - before.Freed;
			ulong drained = after.Drained - before.Drained;

			output.WriteLine($"hits {hits:N0}, misses {misses:N0}, returned {returned:N0}, freed {freed:N0}, drained {drained:N0}, retained {before.Retained} -> {after.Retained}");

			// Every pooled acquisition was released again: no packet is alive
			Assert.Equal(hits + misses, returned + freed);

			// The retained level moved by exactly what was cached minus what was reused or drained
			Assert.Equal((long)returned - (long)hits - (long)drained, (long)after.Retained - (long)before.Retained);

			Library.DrainPool();
		}

		private long RunMixedHandoff(int threadCount, TimeSpan duration) {
			ConcurrentQueue<Packet>[] inboxes = new ConcurrentQueue<Packet>[threadCount];

			for (int i = 0; i < threadCount; i++)
				inboxes[i] = new ConcurrentQueue<Packet>();

			int stopped = 0;
			long operations = 0;

			RunThreads(threadCount, index => {
				Random random = new Random(2000 + index);
				byte[] data = new byte[1240];
				byte[] check = new byte[1240];
				ConcurrentQueue<Packet> inbox = inboxes[index];
				Stopwatch stopwatch = Stopwatch.StartNew();
				long local = 0;

				try {
					while (stopwatch.Elapsed < duration && !abort) {
						int length = 4 + random.Next(1237);
						int seed = random.Next();

						FillPattern(data, length, seed);

						Packet packet = new Packet();
						packet.Create(data, length, PacketFlags.None);

						int target = random.Next(threadCount);

						if (target == index || inboxes[target].Count > 4096) {
							VerifyAndDispose(packet, check);
						} else {
							inboxes[target].Enqueue(packet);
						}

						Packet incoming;

						while (inbox.TryDequeue(out incoming))
							VerifyAndDispose(incoming, check);

						local++;
					}
				} finally {
					// Counted even when this worker fails, so the others do not wait forever
					Interlocked.Increment(ref stopped);
				}

				// Nothing is enqueued once every thread has stopped producing
				while (!abort) {
					bool allStopped = Volatile.Read(ref stopped) == threadCount;
					Packet incoming;

					while (inbox.TryDequeue(out incoming))
						VerifyAndDispose(incoming, check);

					if (allStopped && inbox.IsEmpty)
						break;

					Thread.Yield();
				}

				Interlocked.Add(ref operations, local);
			});

			return operations;
		}

		private static void VerifyAndDispose(Packet packet, byte[] check) {
			int length = packet.Length;

			packet.CopyTo(check);

			int seed = BitConverter.ToInt32(check, 0);

			Assert.True(HasPattern(check, length, seed), "A packet handed between threads arrived corrupted");

			packet.Dispose();
		}

		private static void CreateAndDestroy(int count, int size) {
			byte[] data = new byte[size];
			Packet[] held = new Packet[count];

			for (int i = 0; i < count; i++)
				held[i].Create(data);

			for (int i = 0; i < count; i++)
				held[i].Dispose();
		}

		// The first four bytes carry the seed when they fit, the rest is derived from it
		private static void FillPattern(byte[] buffer, int length, int seed) {
			if (length >= 4)
				BitConverter.TryWriteBytes(buffer.AsSpan(0, 4), seed);

			for (int i = length >= 4 ? 4 : 0; i < length; i++)
				buffer[i] = (byte)(seed * 131 + i * 7);
		}

		private static bool HasPattern(byte[] buffer, int length, int seed) {
			if (length >= 4 && BitConverter.ToInt32(buffer, 0) != seed)
				return false;

			for (int i = length >= 4 ? 4 : 0; i < length; i++) {
				if (buffer[i] != (byte)(seed * 131 + i * 7))
					return false;
			}

			return true;
		}

		private static int SequenceLength(int sequence, int maximum) {
			return 4 + (int)((uint)(sequence * 7919) % (uint)(maximum - 3));
		}

		// ENET_SOAK_SECONDS overrides every timed test's duration (the defaults keep this class under ~30 s)
		private static TimeSpan SoakDuration(double defaultSeconds) {
			string value = Environment.GetEnvironmentVariable("ENET_SOAK_SECONDS");
			double seconds;

			if (!String.IsNullOrEmpty(value) && Double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out seconds) && seconds > 0)
				return TimeSpan.FromSeconds(seconds);

			return TimeSpan.FromSeconds(defaultSeconds);
		}

		private static void RunThreads(int count, Action<int> body) {
			Exception[] failures = new Exception[count];
			Thread[] threads = new Thread[count];

			abort = false;

			for (int i = 0; i < count; i++) {
				int index = i;

				threads[i] = new Thread(() => {
					try {
						body(index);
					} catch (Exception exception) {
						failures[index] = exception;
						abort = true;
					} finally {
						// Publish this worker's pending pool counts before its thread-exit hook runs
						Library.GetPoolStatistics();
					}
				});

				threads[i].IsBackground = true;
				threads[i].Name = "enet-test-" + i;
			}

			foreach (Thread thread in threads)
				thread.Start();

			Stopwatch stopwatch = Stopwatch.StartNew();

			foreach (Thread thread in threads) {
				// Poll so a failing worker is reported at once instead of after the join timeout
				while (!thread.Join(50)) {
					if (abort && stopwatch.Elapsed > TimeSpan.FromSeconds(10))
						break;

					if (!abort && stopwatch.Elapsed > TimeSpan.FromMinutes(5))
						break;
				}
			}

			foreach (Exception failure in failures) {
				if (failure != null)
					throw new Exception("Worker thread failed", failure);
			}

			foreach (Thread thread in threads)
				Assert.False(thread.IsAlive, "A worker thread did not finish (deadlock or livelock)");
		}
	}
}

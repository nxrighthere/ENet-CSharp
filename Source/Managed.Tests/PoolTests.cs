using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using Xunit;

namespace ENet.Tests {
	[Collection("ENet")]
	[FlushPoolCounters]
	public class PoolTests {
		[UnmanagedFunctionPointer(CallingConvention.Winapi)]
		[return: MarshalAs(UnmanagedType.Bool)]
		private delegate bool DllEntryPoint(IntPtr module, uint reason, IntPtr reserved);

		[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
		private static extern IntPtr GetModuleHandleW(string moduleName);

		[WindowsPoolFact]
		public void ThreadDetach_AfterRetire_PacketDestroyBypassesPool() {
			Library.DrainPool();
			IntPtr module = GetModuleHandleW("enet.dll");
			Assert.NotEqual(IntPtr.Zero, module);

			// AddressOfEntryPoint is at optional-header + 16 in both PE32 and PE32+.
			Assert.Equal((short)0x5a4d, Marshal.ReadInt16(module));
			int peOffset = Marshal.ReadInt32(module, 0x3c);
			Assert.Equal(0x00004550, Marshal.ReadInt32(module, peOffset));
			int entryOffset = Marshal.ReadInt32(module, peOffset + 24 + 16);
			Assert.True(entryOffset > 0, "The native DLL must have a thread-detach entry point");
			DllEntryPoint entry = Marshal.GetDelegateForFunctionPointer<DllEntryPoint>(IntPtr.Add(module, entryOffset));
			Exception failure = null;

			Thread worker = new Thread(() => {
				Packet first = new Packet();
				Packet second = new Packet();

				try {
					first.Create(new byte[1000]);
					Assert.True(entry(module, 3, IntPtr.Zero), "DLL_THREAD_DETACH failed");
					PoolStatistics retired = Library.GetPoolStatistics();

					first.Dispose();
					second.Create(new byte[1000]);
					second.Dispose();

					PoolStatistics after = Library.GetPoolStatistics();
					Assert.True(after.Caches == retired.Caches, $"Retired cache reactivated: Caches {retired.Caches} -> {after.Caches}, ThreadRetained {after.ThreadRetained}");
					Assert.Equal(0u, after.ThreadRetained);
					Assert.Equal(retired.Hits, after.Hits);
					Assert.Equal(retired.Misses, after.Misses);
					Assert.Equal(retired.Freed + 1, after.Freed);
				} catch (Exception exception) {
					failure = exception;
				} finally {
					first.Dispose();
					second.Dispose();
					Library.DrainPool();
				}
			});

			worker.IsBackground = true;
			worker.Start();
			Assert.True(worker.Join(TimeSpan.FromSeconds(30)), "The detach worker did not finish");
			Assert.Null(failure);
		}

		[PoolFact]
		public void PoolExhaustion_Hold700Undisposed_ThenDisposeAll() {
			using LoopbackPair pair = new LoopbackPair();

			const int total = 700;
			const int batchSize = 50;
			const int payloadSize = 512;

			byte[] payload = PacketTests.MakePayload(payloadSize, 6);
			List<Packet> held = new List<Packet>();

			Action<Host, Event> holdReceived = (host, e) => {
				if (host == pair.Server && e.Type == EventType.Receive)
					held.Add(e.Packet);
			};

			PoolStatistics start = Library.GetPoolStatistics();

			int sent = 0;

			while (sent < total) {
				int batch = Math.Min(batchSize, total - sent);

				for (int i = 0; i < batch; i++) {
					Packet packet = new Packet();
					packet.Create(payload, payload.Length, PacketFlags.Reliable);

					Assert.True(pair.ClientToServer.Send(0, ref packet));

					sent++;
				}

				Assert.True(pair.PumpUntil(() => held.Count >= sent, holdReceived, 30000), $"Timed out at {held.Count}/{sent} held packets");
			}

			Assert.Equal(total, held.Count);

			// Far more blocks are live than the pool can ever retain, so creates had to fall back to malloc
			PoolStatistics whileHolding = Library.GetPoolStatistics();

			Assert.True(whileHolding.Misses > start.Misses, "Expected pool misses while holding more packets than the retention cap");

			// Payloads must stay intact while the packets are held
			byte[] buffer = new byte[payloadSize];

			for (int i = 0; i < held.Count; i++) {
				Packet packet = held[i];

				Assert.Equal(payloadSize, packet.Length);

				packet.CopyTo(buffer);

				Assert.Equal(payload, buffer);
			}

			// Empty this thread's cache so the release accounting below is deterministic: disposing
			// 700 pooled blocks into an empty cache must retain exactly the 128-block per-thread cap
			Library.DrainPool();

			Assert.Equal(0u, Library.GetPoolStatistics().ThreadRetained);

			PoolStatistics beforeDispose = Library.GetPoolStatistics();

			for (int i = 0; i < held.Count; i++) {
				Packet packet = held[i];
				packet.Dispose();
			}

			PoolStatistics afterDispose = Library.GetPoolStatistics();

			Assert.Equal(128ul, afterDispose.Returned - beforeDispose.Returned);
			Assert.Equal(572ul, afterDispose.Freed - beforeDispose.Freed);
			Assert.Equal(128u, afterDispose.ThreadRetained);

			Library.DrainPool();

			Assert.Equal(0u, Library.GetPoolStatistics().ThreadRetained);

			// Traffic still works after exhaustion, release, and drain
			Packet final = new Packet();
			final.Create(payload, payload.Length, PacketFlags.Reliable);

			Assert.True(pair.ClientToServer.Send(0, ref final));

			bool delivered = false;

			Assert.True(pair.PumpUntil(() => delivered, (host, e) => {
				if (host == pair.Server && e.Type == EventType.Receive) {
					Packet incoming = e.Packet;

					Assert.Equal(payloadSize, incoming.Length);

					incoming.Dispose();
					delivered = true;
				}
			}), "Timed out on the post-exhaustion packet");
		}

		[Fact]
		public void GracefulShutdown_HeldPacketsDisposedAfterDeinitialize() {
			List<Packet> held = new List<Packet>();
			byte[] payload = PacketTests.MakePayload(256, 7);

			LoopbackPair pair = new LoopbackPair();

			try {
				const int total = 100;
				int sent = 0;

				Action<Host, Event> holdReceived = (host, e) => {
					if (host == pair.Server && e.Type == EventType.Receive)
						held.Add(e.Packet);
				};

				for (int i = 0; i < total; i++) {
					Packet packet = new Packet();
					packet.Create(payload, payload.Length, PacketFlags.Reliable);

					Assert.True(pair.ClientToServer.Send(0, ref packet));

					sent++;
				}

				Assert.True(pair.PumpUntil(() => held.Count >= sent, holdReceived, 30000), "Timed out receiving packets to hold");
			} finally {
				pair.Dispose();
			}

			// Hosts are gone and the library is shut down, but held packets remain valid and
			// disposing them must be safe (their blocks bypass the now-disabled pool)
			Library.Deinitialize();

			try {
				byte[] buffer = new byte[payload.Length];

				for (int i = 0; i < held.Count; i++) {
					Packet packet = held[i];

					Assert.Equal(payload.Length, packet.Length);

					packet.CopyTo(buffer);

					Assert.Equal(payload, buffer);

					packet.Dispose();
				}
			} finally {
				Assert.True(Library.Initialize(), "ENet failed to re-initialize after shutdown");
			}

			// The pool works again after re-initialization
			using LoopbackPair fresh = new LoopbackPair();

			Packet probe = new Packet();
			probe.Create(payload, payload.Length, PacketFlags.Reliable);

			Assert.True(fresh.ClientToServer.Send(0, ref probe));

			bool delivered = false;

			Assert.True(fresh.PumpUntil(() => delivered, (host, e) => {
				if (host == fresh.Server && e.Type == EventType.Receive) {
					e.Packet.Dispose();
					delivered = true;
				}
			}), "Timed out after re-initialization");
		}
	}
}

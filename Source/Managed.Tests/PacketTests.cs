using System;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Xunit;

namespace ENet.Tests {
	[Collection("ENet")]
	[FlushPoolCounters]
	public class PacketTests {
		[Fact]
		public void Create_NoAllocateWithOffset_PointsAtOffsetData() {
			byte[] source = { 0, 1, 2, 3, 4, 5 };
			byte[] expected = { 2, 3, 4, 5 };
			IntPtr memory = Marshal.AllocHGlobal(source.Length);

			try {
				Marshal.Copy(source, 0, memory, source.Length);
				Packet packet = new Packet();

				try {
					packet.Create(memory, 2, 6, PacketFlags.NoAllocate);
					Assert.Equal(4, packet.Length);
					byte[] actual = new byte[packet.Length];
					packet.CopyTo(actual);
					Assert.Equal(expected, actual);
					Assert.Equal(IntPtr.Add(memory, 2), packet.Data);
				} finally {
					packet.Dispose();
				}

				try {
					packet.Create(memory, 2, 6, PacketFlags.None);
					Assert.Equal(4, packet.Length);
					byte[] actual = new byte[packet.Length];
					packet.CopyTo(actual);
					Assert.Equal(expected, actual);
				} finally {
					packet.Dispose();
				}

				// Dispose the hosts before freeing caller-owned memory, including on a timeout.
				using LoopbackPair pair = new LoopbackPair();
				packet.Create(memory, 2, 6, PacketFlags.NoAllocate);

				try {
					Assert.True(pair.ClientToServer.Send(0, ref packet));
					packet = default(Packet); // The host now owns the outgoing packet.
				} finally {
					packet.Dispose();
				}

				byte[] received = null;
				Assert.True(pair.PumpUntil(() => received != null, (host, e) => {
					if (e.Type != EventType.Receive)
						return;

					Packet incoming = e.Packet;
					try {
						if (host == pair.Server) {
							received = new byte[incoming.Length];
							incoming.CopyTo(received);
						}
					} finally {
						incoming.Dispose();
					}
				}), "Timed out receiving the external-memory packet");
				Assert.Equal(expected, received);
			} finally {
				Marshal.FreeHGlobal(memory);
			}
		}

		internal static byte[] MakePayload(int size, int seed) {
			byte[] payload = new byte[size];

			for (int i = 0; i < size; i++)
				payload[i] = (byte)(seed + i * 31 + (i >> 8));

			return payload;
		}

		private static byte[] RoundTrip(byte[] payload) {
			using LoopbackPair pair = new LoopbackPair();

			Packet packet = new Packet();
			packet.Create(payload, payload.Length, PacketFlags.Reliable);

			Assert.True(pair.ClientToServer.Send(0, ref packet));

			byte[] received = null;

			bool delivered = pair.PumpUntil(() => received != null, (host, e) => {
				if (host == pair.Server && e.Type == EventType.Receive) {
					Packet incoming = e.Packet;

					received = new byte[incoming.Length];
					incoming.CopyTo(received);
					incoming.Dispose();
				}
			}, 30000);

			Assert.True(delivered, "Timed out waiting for the packet");

			// Let acknowledgements flow so the outgoing packet is destroyed before stats are read
			pair.PumpFor(50);

			return received;
		}

		[Fact]
		public void Send_256Bytes_RoundTripsIntact() {
			byte[] payload = MakePayload(256, 1);

			Assert.Equal(payload, RoundTrip(payload));
		}

		[PoolFact]
		public void Send_1024Bytes_UsesPool() {
			// 1024 + 40-byte header = 1064, within the 1288-byte pool block
			PoolStatistics before = Library.GetPoolStatistics();
			byte[] payload = MakePayload(1024, 2);

			Assert.Equal(payload, RoundTrip(payload));

			PoolStatistics after = Library.GetPoolStatistics();

			// Send-side create + receive-side create both fit the pool
			Assert.True(after.Hits + after.Misses - before.Hits - before.Misses >= 2, "Expected at least two pool acquisitions");
			Assert.True(after.Returned > before.Returned, "Expected at least one block returned to the pool");
			Assert.True(after.ThreadRetained <= 128);
		}

		[PoolFact]
		public void Send_1200Bytes_UsesPool() {
			// 1200-byte game/MTU-class payload: 1200 + 40 = 1240, still within the 1288 block
			PoolStatistics before = Library.GetPoolStatistics();
			byte[] payload = MakePayload(1200, 3);

			Assert.Equal(payload, RoundTrip(payload));

			PoolStatistics after = Library.GetPoolStatistics();

			Assert.True(after.Hits + after.Misses - before.Hits - before.Misses >= 2, "Expected the 1200-byte payload to be pooled");
			Assert.True(after.ThreadRetained <= 128);
		}

		[PoolFact]
		public void Send_OversizedPayload_FallsBack() {
			// 1300 bytes exceeds the 1288-byte block on its own (header included), so it can never be
			// pooled. At the default 1280 MTU it is also fragmented: the sent packet and the reassembled
			// one on the receiving side are both allocated directly
			PoolStatistics before = Library.GetPoolStatistics();
			byte[] payload = MakePayload(1300, 4);

			Assert.Equal(payload, RoundTrip(payload));

			PoolStatistics after = Library.GetPoolStatistics();

			Assert.True(after.Oversized - before.Oversized >= 2, "Payloads larger than the pool block must fall back to malloc on both sides");
		}

		[Fact]
		public void Send_100KB_Reliable_FragmentsAndReassembles() {
			byte[] payload = MakePayload(100 * 1024, 5);
			byte[] received = RoundTrip(payload);

			Assert.NotNull(received);
			Assert.Equal(payload.Length, received.Length);
			Assert.Equal(SHA256.HashData(payload), SHA256.HashData(received));
		}
	}
}

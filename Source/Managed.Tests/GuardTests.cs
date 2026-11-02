using System;
using Xunit;

namespace ENet.Tests {
	[Collection("ENet")]
	[FlushPoolCounters]
	public class GuardTests {
		[Fact]
		public void Create_OffsetGreaterThanLength_Throws() {
			Packet packet = new Packet();

			Assert.Throws<ArgumentOutOfRangeException>(() => packet.Create(new byte[10], 8, 4, PacketFlags.None));
		}

		[Fact]
		public void CopyTo_TooSmallDestination_Throws() {
			Packet packet = new Packet();
			packet.Create(PacketTests.MakePayload(100, 8));

			try {
				Assert.Throws<ArgumentOutOfRangeException>(() => packet.CopyTo(new byte[50]));
			} finally {
				packet.Dispose();
			}
		}

		[Fact]
		public void Create_NoAllocateWithManagedArray_Throws() {
			Packet packet = new Packet();

			Assert.Throws<ArgumentException>(() => packet.Create(new byte[16], 16, PacketFlags.NoAllocate));
			Assert.Throws<ArgumentException>(() => packet.Create(new byte[16], 0, 16, PacketFlags.NoAllocate));
		}

		[Fact]
		public void Version_IsLockstepped() {
			// 2.7.1 on both sides; the fixture's successful Initialize() already proved the
			// native library agrees (a mismatch throws "Incompatible version")
			Assert.Equal((2u << 16) | (7u << 8) | 1u, Library.version);
		}

		[Fact]
		public void BuildFlavor_MatchesExpectation() {
			// CI sets ENET_EXPECT_POOL so a job cannot silently test the wrong native build
			string expected = Environment.GetEnvironmentVariable("ENET_EXPECT_POOL");

			if (expected == "0") {
				Assert.Equal(0, Library.PoolBlockSize);

				Packet packet = new Packet();
				packet.Create(PacketTests.MakePayload(64, 10));
				packet.Dispose();

				PoolStatistics statistics = Library.GetPoolStatistics();

				Assert.Equal(0ul, statistics.Hits + statistics.Misses + statistics.Returned + statistics.Freed);
			} else if (expected == "1") {
				Assert.Equal(1288, Library.PoolBlockSize);
			}
		}

		[PoolFact]
		public void PoolStatistics_And_Drain_BehaveSanely() {
			Packet packet = new Packet();
			packet.Create(PacketTests.MakePayload(64, 9));
			packet.Dispose();

			PoolStatistics statistics = Library.GetPoolStatistics();

			Assert.True(statistics.Hits + statistics.Misses > 0, "Expected at least one pool acquisition by this point in the run");
			Assert.True(statistics.ThreadRetained >= 1 && statistics.ThreadRetained <= 128);
			Assert.True(statistics.Caches >= 1, "The calling thread must own a cache after using the pool");

			Library.DrainPool();

			statistics = Library.GetPoolStatistics();

			Assert.Equal(0u, statistics.ThreadRetained);
			Assert.Equal(0u, statistics.Orphaned);
		}
	}
}

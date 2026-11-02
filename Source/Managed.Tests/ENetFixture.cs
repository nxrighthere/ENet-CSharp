using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Xunit;
using Xunit.Sdk;

namespace ENet.Tests {
	/// <summary>
	/// Initializes the ENet library once for the whole run. Library initialization and the pool's
	/// process-wide counters are shared by every test, and several tests assert counter deltas or
	/// spin up their own threads, so all test classes share this collection and parallelization is
	/// disabled in xunit.runner.json. Each host must still be serviced by one thread at a time.
	/// </summary>
	[CollectionDefinition("ENet", DisableParallelization = true)]
	public class ENetCollection : ICollectionFixture<ENetFixture> {
	}

	/// <summary>
	/// Publishes the executing thread's pending pool counts before and after every test. Threads
	/// publish in batches, so without this an idle test thread that exits later (the thread pool
	/// retires idle threads) would add its leftover counts in the middle of another test's deltas.
	/// </summary>
	public sealed class FlushPoolCountersAttribute : BeforeAfterTestAttribute {
		public override void Before(MethodInfo methodUnderTest) {
			Flush();
		}

		public override void After(MethodInfo methodUnderTest) {
			Flush();
		}

		private static void Flush() {
			try {
				Library.GetPoolStatistics();
			} catch (Exception) {
				// The test itself reports a native library that failed to load
			}
		}
	}

	/// <summary>
	/// A fact that asserts buffer-pool counters: skipped when the native library under test was
	/// built with ENET_NO_POOL (every counter stays zero there).
	/// </summary>
	public sealed class PoolFactAttribute : FactAttribute {
		public PoolFactAttribute() {
			if (!ENetFixture.PoolCompiledIn)
				Skip = "Native library was built with ENET_NO_POOL";
		}
	}

	public sealed class WindowsPoolFactAttribute : FactAttribute {
		public WindowsPoolFactAttribute() {
			if (!OperatingSystem.IsWindows())
				Skip = "DLL_THREAD_DETACH is Windows-only";
			else if (!ENetFixture.PoolCompiledIn)
				Skip = "Native library was built with ENET_NO_POOL";
		}
	}

	public class ENetFixture : IDisposable {
		static ENetFixture() {
			NativeLibrary.SetDllImportResolver(typeof(Library).Assembly, ResolveNativeLibrary);
		}

		/// <summary>Whether the native library under test pools packet blocks (no initialization needed).</summary>
		public static bool PoolCompiledIn {
			get {
				try {
					return Library.PoolBlockSize > 0;
				} catch (Exception) {
					// Let the test itself surface the load failure
					return true;
				}
			}
		}

		public ENetFixture() {
			if (!Library.Initialize())
				throw new InvalidOperationException("ENet library initialization failed");
		}

		public void Dispose() {
			Library.Deinitialize();
		}

		private static IntPtr ResolveNativeLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath) {
			if (libraryName != "enet")
				return IntPtr.Zero;

			string exactPath = Environment.GetEnvironmentVariable("ENET_NATIVE_LIB_PATH");

			if (!String.IsNullOrEmpty(exactPath) && File.Exists(exactPath))
				return NativeLibrary.Load(exactPath);

			string fileName = OperatingSystem.IsWindows() ? "enet.dll" : OperatingSystem.IsMacOS() ? "libenet.dylib" : "libenet.so";
			string directory = Environment.GetEnvironmentVariable("ENET_NATIVE_LIB_DIR");

			if (!String.IsNullOrEmpty(directory)) {
				string candidate = Path.Combine(directory, fileName);

				if (File.Exists(candidate))
					return NativeLibrary.Load(candidate);
			}

			// Local dev fallback: find the CMake output by walking up to the repo root
			for (DirectoryInfo current = new DirectoryInfo(AppContext.BaseDirectory); current != null; current = current.Parent) {
				string multiConfig = Path.Combine(current.FullName, "Source", "Native", "build", "Release", fileName);

				if (File.Exists(multiConfig))
					return NativeLibrary.Load(multiConfig);

				string singleConfig = Path.Combine(current.FullName, "Source", "Native", "build", fileName);

				if (File.Exists(singleConfig))
					return NativeLibrary.Load(singleConfig);
			}

			return IntPtr.Zero;
		}
	}
}

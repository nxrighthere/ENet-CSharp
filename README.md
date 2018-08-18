<p align="center"> 
  <img src="https://i.imgur.com/2zyQ1q9.png" alt="alt logo">
</p>

[![GitHub release](https://img.shields.io/github/release/nxrighthere/ENet-CSharp.svg)](https://github.com/nxrighthere/BenchmarkNet/releases) [![PayPal](https://drive.google.com/uc?id=1OQrtNBVJehNVxgPf6T6yX1wIysz1ElLR)](https://www.paypal.me/nxrighthere) [![Bountysource](https://drive.google.com/uc?id=19QRobscL8Ir2RL489IbVjcw3fULfWS_Q)](https://salt.bountysource.com/checkout/amount?team=nxrighthere)

This project is based on collaborative work with [@inlife](https://github.com/inlife) and inherited all features of the original [fork](https://github.com/zpl-c/enet). This version is extended and optimized to run safely in the managed .NET environment with the highest possible performance.

Building
--------
To build a native library, you will need [CMake](https://cmake.org/download/) with GNU Make or Visual Studio. You can always just grab the compiled binaries from the release section.

Usage
--------
Before starting to work, the library should be initialized using `ENet.Library.Initialize();` function.

Start a new server:
```c#
Host server = new Host();
Address address = new Address();

address.Port = port;
server.Create(address, maxClients);

while (true) {
	server.Service(15, out Event netEvent);

	switch (netEvent.Type) {
		case EventType.Connect:
			Console.WriteLine("Client connected (ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.Address.GetIP() + ")");
			break;

		case EventType.Disconnect:
			Console.WriteLine("Client disconnected (ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.Address.GetIP() + ")");
			break;

		case EventType.Timeout:
			Console.WriteLine("Client timeout (ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.Address.GetIP() + ")");
			break;

		case EventType.Receive:
			Console.WriteLine("Packet received from (ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.Address.GetIP() + ", Channel ID: " + netEvent.ChannelID + ", Data length: " + netEvent.Packet.Length + ")");
			netEvent.Packet.Dispose();
			break;
	}
}

server.Flush();
server.Dispose();
```

Create a new client:
```c#
Host client = new Host();
Address address = new Address();

address.SetHost(ip);
address.Port = port;
client.Create();

Peer peer = client.Connect(address);

while (true) {
	client.Service(15, out Event netEvent);

	switch (netEvent.Type) {
		case EventType.Connect:
			Console.WriteLine("Client connected to server (ID: " + peer.ID + ")");
			break;

		case EventType.Disconnect:
			Console.WriteLine("Client disconnected from server");
			break;

		case EventType.Timeout:
			Console.WriteLine("Client connection timeout");
			break;

		case EventType.Receive:
			Console.WriteLine("Packet received from server (Channel ID: " + netEvent.ChannelID + ", Data length: " + netEvent.Packet.Length + ")");
			netEvent.Packet.Dispose();
			break;
	}
}

client.Flush();
client.Dispose();
```

Create and send a new packet:
```csharp
Packet packet = new Packet();
byte[] data = new byte[64];

packet.Create(data);
peer.Send(channelID, packet);
```

When the work is done, deinitialize the library using `ENet.Library.Deinitialize();` function.

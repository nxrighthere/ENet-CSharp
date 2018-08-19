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

while (!Console.KeyAvailable) {
	server.Service(15, out Event netEvent);

	switch (netEvent.Type) {
		case EventType.None:
			break;

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

while (!Console.KeyAvailable) {
	client.Service(15, out Event netEvent);

	switch (netEvent.Type) {
		case EventType.None:
			break;

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

API reference
--------
### Enumerations
#### PacketFlags
Definitions of flag for `Peer.Send` function:

`PacketFlags.None` unreliable sequenced, delivery of packet is not guaranteed.

`PacketFlags.Reliable` reliable sequenced, packet must be received by the target peer and resend attempts should be made until the packet is delivered.

`PacketFlags.Unsequenced` unreliable unsequenced, packet will not be sequenced with other packets and may be delivered out of order.

`PacketFlags.NoAllocate` packet will not allocate data, and user must supply it instead.

`PacketFlags.UnreliableFragment` unreliable sequenced, packet will be fragmented if it exceeds the MTU.

#### EventType
Definitions of event types for `Event.Type` property:

`EventType.None` no event occurred within the specified time limit.

`EventType.Connect` a connection request initiated by `Peer.Connect` has completed. `Event.Peer` returns the managed pointer to the peer which successfully connected. `Peer.Data` returns user supplied `uint` data describing the connection, or zero, if none is available.

`EventType.Disconnect` a peer has disconnected. This event is generated on a successful completion of a disconnect initiated by `Peer.Disconnect`. `Event.Peer` returns the managed pointer to the peer which disconnected. `Peer.Data` returns user supplied `uint` data describing the disconnection, or zero, if none is available.

`EventType.Receive` a packet has been received from a peer. `Event.Peer` returns the managed pointer to the peer which sent the packet. `Event.ChannelID` specifies the channel number upon which the packet was received. `Event.Packet` returns the managed pointer to the packet that was received. This packet must be destroyed with `Event.Packet.Dispose()` after use.

`EventType.Timeout` a peer has timed out. This event occurs if a peer has timed out, or if a connection request intialized by `Peer.Connect` has timed out. `Event.Peer` returns the managed pointer to the peer which timed out.

#### PeerState
Definitions of peer states for `Peer.State` property:

`PeerState.Uninitialized` a peer not initialized.

`PeerState.Disconnected` a peer disconnected or timed out.

`PeerState.Connecting` a peer connection in-progress.

`PeerState.Connected` a peer successfuly connected.

`PeerState.Disconnecting` a peer disconnection in-progress.

`PeerState.Zombie` a peer not properly disconnected.

### Structures
#### Address
Contains a marshalled structure from the unmanaged side with host data and port number.

`Address.Port` set or get a port number.

`Address.SetHost` set host name or an IP address (IPv4/IPv6).

`Address.GetIP` gives the printable form of the IP address.

`Address.GetName` attempts to do a reverse lookup of the host.

#### Event
Contains a marshalled structure from the unmanaged side with event type, managed pointer to the peer, channel ID, user supplied data, and managed pointer to the packet.

`Event.Type` type of the event.

`Event.Peer` peer that generated a connect, disconnect, receive, or timeout event.

`Event.ChannelID` channel on the peer that generated the event, if appropriate.

`Event.Data` user supplied data, if appropriate.

`Event.Packet` packet associated with the event, if appropriate.

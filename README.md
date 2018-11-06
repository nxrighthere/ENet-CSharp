<p align="center"> 
  <img src="https://i.imgur.com/CxkUxTs.png" alt="alt logo">
</p>

[![GitHub release](https://img.shields.io/github/release/nxrighthere/ENet-CSharp.svg)](https://github.com/nxrighthere/ENet-CSharp/releases) [![PayPal](https://drive.google.com/uc?id=1OQrtNBVJehNVxgPf6T6yX1wIysz1ElLR)](https://www.paypal.me/nxrighthere) [![Bountysource](https://drive.google.com/uc?id=19QRobscL8Ir2RL489IbVjcw3fULfWS_Q)](https://salt.bountysource.com/checkout/amount?team=nxrighthere)

This project is based on collaborative work with [@inlife](https://github.com/inlife) and inherited all features of the original [fork](https://github.com/zpl-c/enet) where the native library was heavily modified. You can find the most notable changes [here](https://github.com/nxrighthere/ENet-CSharp/issues/22#issuecomment-432982154). This version is extended and optimized to run safely in the managed .NET environment with the highest possible performance.

Features:

- Lightweight and straightforward
- Low resource consumption
- IPv4/IPv6 support
- Connection management
- Sequencing
- Channels
- Reliability
- Fragmentation and reassembly
- Compression
- Aggregation
- Adaptability
- Portability

Building
--------
To build the native library appropriate software is required:

For desktop platforms [CMake](https://cmake.org/download/) with GNU Make or Visual Studio.

For mobile platforms [NDK](https://developer.android.com/ndk/downloads/) for Android and [XCode](https://developer.apple.com/xcode/) for iOS.

Define `ENET_LZ4` to build the library with support for an optional packet-level compression.

A managed assembly can be built using any available compiling platform that supports C# 3.0 or higher.

Compiled libraries
--------
You can grab compiled libraries from the [release](https://github.com/nxrighthere/ENet-CSharp/releases) section:

`ENet-CSharp` contains compiled assembly with native libraries for the .NET environment.

`ENet-Unity` contains script with native libraries for the Unity.

These packages are provided only for traditional platforms: Windows, Linux, and MacOS.

Usage
--------
Before starting to work, the library should be initialized using `ENet.Library.Initialize();` function.

After the work is done, deinitialize the library using `ENet.Library.Deinitialize();` function.

### .NET environment
##### Start a new server:
```c#
using (Host server = new Host()) {
	Address address = new Address();

	address.Port = port;
	server.Create(address, maxClients);

	while (!Console.KeyAvailable) {
		server.Service(15, out Event netEvent);

		switch (netEvent.Type) {
			case EventType.None:
				break;

			case EventType.Connect:
				Console.WriteLine("Client connected - ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.IP);
				break;

			case EventType.Disconnect:
				Console.WriteLine("Client disconnected - ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.IP);
				break;

			case EventType.Timeout:
				Console.WriteLine("Client timeout - ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.IP);
				break;

			case EventType.Receive:
				Console.WriteLine("Packet received from - ID: " + netEvent.Peer.ID + ", IP: " + netEvent.Peer.IP + ", Channel ID: " + netEvent.ChannelID + ", Data length: " + netEvent.Packet.Length);
				netEvent.Packet.Dispose();
				break;
		}
	}

	server.Flush();
}
```

##### Start a new client:
```c#
using (Host client = new Host()) {
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
				Console.WriteLine("Client connected to server - ID: " + peer.ID);
				break;

			case EventType.Disconnect:
				Console.WriteLine("Client disconnected from server");
				break;

			case EventType.Timeout:
				Console.WriteLine("Client connection timeout");
				break;

			case EventType.Receive:
				Console.WriteLine("Packet received from server - Channel ID: " + netEvent.ChannelID + ", Data length: " + netEvent.Packet.Length);
				netEvent.Packet.Dispose();
				break;
		}
	}

	client.Flush();
}
```

##### Create and send a new packet:
```csharp
Packet packet = default(Packet);
byte[] data = new byte[64];

packet.Create(data);
peer.Send(channelID, ref packet);
```

##### Copy payload from a packet:
```csharp
byte[] buffer = new byte[1024];

netEvent.Packet.CopyTo(buffer);
```

##### Integrate with a custom memory allocator:
```csharp
AllocCallback OnMemoryAllocate = (size) => {
	return Marshal.AllocHGlobal(size);
};

FreeCallback OnMemoryFree = (memory) => {
	Marshal.FreeHGlobal(memory);
};

NoMemoryCallback OnNoMemory = () => {
	throw new OutOfMemoryException();
};

Callbacks callbacks = new Callbacks(OnMemoryAllocate, OnMemoryFree, OnNoMemory);

if (ENet.Library.Initialize(callbacks))
	Console.WriteLine("ENet successfully initialized using a custom memory allocator");
```

### Unity
Usage is almost the same as in the .NET environment, except that the console functions must be replaced with functions provided by Unity. If the `Host.Service()` will be called in a game loop, then make sure that the timeout parameter set to 0 which means non-blocking. Also, keep Unity run in background by enabling the appropriate option in the player settings.

API reference
--------
### Enumerations
#### PacketFlags
Definitions of a flags for `Peer.Send()` function:

`PacketFlags.None` unreliable sequenced, delivery of packet is not guaranteed.

`PacketFlags.Reliable` reliable sequenced, a packet must be received by the target peer and resend attempts should be made until the packet is delivered.

`PacketFlags.Unsequenced` a packet will not be sequenced with other packets and may be delivered out of order.

`PacketFlags.NoAllocate` a packet will not allocate data, and the user must supply it instead.

`PacketFlags.UnreliableFragment` a packet will be fragmented if it exceeds the MTU.

#### EventType
Definitions of event types for `Event.Type` property:

`EventType.None` no event occurred within the specified time limit.

`EventType.Connect` a connection request initiated by `Peer.Connect()` function has completed. `Event.Peer` returns a peer which successfully connected. `Event.Data` returns user-supplied data describing the connection or 0 if none is available.

`EventType.Disconnect` a peer has disconnected. This event is generated on a successful completion of a disconnect initiated by `Peer.Disconnect`. `Event.Peer` returns a peer which disconnected. `Event.Data` returns user-supplied data describing the disconnection or 0 if none is available.

`EventType.Receive` a packet has been received from a peer. `Event.Peer` returns a peer which sent the packet. `Event.ChannelID` specifies the channel number upon which the packet was received. `Event.Packet` returns a packet that was received, and this packet must be destroyed using `Event.Packet.Dispose()` function after use.

`EventType.Timeout` a peer has timed out. This event occurs if a peer has timed out or if a connection request initialized by `Peer.Connect` has timed out. `Event.Peer` returns a peer which timed out.

#### PeerState
Definitions of peer states for `Peer.State` property:

`PeerState.Uninitialized` a peer not initialized.

`PeerState.Disconnected` a peer disconnected or timed out.

`PeerState.Connecting` a peer connection in-progress.

`PeerState.Connected` a peer successfully connected.

`PeerState.Disconnecting` a peer disconnection in-progress.

`PeerState.Zombie` a peer not properly disconnected.

### Delegates
#### Memory callbacks
Provides per application events.

`AllocCallback(IntPtr size)` notifies when a memory is requested for allocation. Expects pointer to the newly allocated memory.

`FreeCallback(IntPtr memory)` notifies when the memory can be freed.

`NoMemoryCallback()` notifies when memory is not enough.

#### Packet callbacks
Provides per packet events.

`PacketFreeCallback(Packet packet)` notifies when a packet is being destroyed.

### Structures
#### Address
Contains marshalled structure with host data and port number.

`Address.Port` set or get a port number.

`Address.SetHost(string hostName)` set host name or an IP address (IPv4/IPv6). Should be used for binding to a network interface or for connection to a foreign host. Returns true on success or false on failure.

#### Event
Contains marshalled structure with the event type, managed pointer to the peer, channel ID, user-supplied data, and managed pointer to the packet.

`Event.Type` returns a type of the event.

`Event.Peer` returns a peer that generated a connect, disconnect, receive or a timeout event.

`Event.ChannelID` returns a channel ID on the peer that generated the event, if appropriate.

`Event.Data` returns user-supplied data, if appropriate.

`Event.Packet` returns a packet associated with the event, if appropriate.

#### Packet
Contains a managed pointer to the packet.

`Packet.Dispose()` destroys the packet. Should be called only when the packet obtained from `EventType.Receive` event.

`Packet.IsSet` returns a state of the managed pointer.

`Packet.Data` returns a managed pointer to the packet data.

`Packet.Length` returns a length of payload in the packet.

`Packet.SetFreeCallback(PacketFreeCallback callback)` set callback to notify the user when an appropriate packet is being destroyed.

`Packet.Create(byte[] data, int length, PacketFlags flags)` creates a packet that may be sent to a peer. The length and packet flags parameters are optional. Multiple flags can be specified at once. Managed pointer `IntPtr` to a native buffer can be used instead of a reference to a byte array.

`Packet.CopyTo(byte[] destination)` copies payload from the packet to the destination array.

#### Peer
Contains a managed pointer to the peer.

`Peer.IsSet` returns a state of the managed pointer.

`Peer.ID` returns a peer ID.

`Peer.IP` returns an IP address in a printable form.

`Peer.Port` returns a port number.

`Peer.MTU` returns an MTU.

`Peer.State` returns a peer state described in the `PeerState` enumeration.

`Peer.RoundTripTime` returns a round trip time in milliseconds.

`Peer.LastSendTime` returns a last packet send time in milliseconds.

`Peer.LastReceiveTime` returns a last packet receive time in milliseconds.

`Peer.PacketsSent` returns a total number of packets sent during the connection.

`Peer.PacketsLost` returns a total number of lost packets during the connection.

`Peer.BytesSent` returns a total number of bytes sent during the connection.

`Peer.BytesReceived` returns a total number of bytes received during the connection.

`Peer.Data` set or get the user-supplied data. Should be used with an explicit cast to appropriate data type.

`Peer.ConfigureThrottle(uint interval, uint acceleration, uint deceleration)` configures throttle parameter for a peer. Unreliable packets are dropped by ENet in response to the varying conditions of the connection to the peer. The throttle represents a probability that an unreliable packet should not be dropped and thus sent by ENet to the peer. The lowest mean round trip time from the sending of a reliable packet to the receipt of its acknowledgment is measured over an amount of time specified by the interval parameter in milliseconds. If a measured round trip time happens to be significantly less than the mean round trip time measured over the interval, then the throttle probability is increased to allow more traffic by an amount specified in the acceleration parameter, which is a ratio to the `Library.throttleScale` constant. If a measured round trip time happens to be significantly greater than the mean round trip time measured over the interval, then the throttle probability is decreased to limit traffic by an amount specified in the deceleration parameter, which is a ratio to the `Library.throttleScale` constant. When the throttle has a value of `Library.throttleScale`, no unreliable packets are dropped by ENet, and so 100% of all unreliable packets will be sent. When the throttle has a value of 0, all unreliable packets are dropped by ENet, and so 0% of all unreliable packets will be sent. Intermediate values for the throttle represent intermediate probabilities between 0% and 100% of unreliable packets being sent. The bandwidth limits of the local and foreign hosts are taken into account to determine a sensible limit for the throttle probability above which it should not raise even in the best of conditions.

`Peer.Send(byte channelID, ref Packet packet)` queues a packet to be sent. Returns true on success or false on failure.

`Peer.Ping()` sends a ping request to a peer. ENet automatically pings all connected peers at regular intervals, however, this function may be called to ensure more frequent ping requests.

`Peer.PingInterval(uint interval)` sets an interval at which pings will be sent to a peer. Pings are used both to monitor the liveness of the connection and also to dynamically adjust the throttle during periods of low traffic so that the throttle has reasonable responsiveness during traffic spikes.

`Peer.Timeout(uint timeoutLimit, uint timeoutMinimum, uint timeoutMaximum)` sets a timeout parameters for a peer. The timeout parameters control how and when a peer will timeout from a failure to acknowledge reliable traffic. Timeout values used in the semi-linear mechanism, where if a reliable packet is not acknowledged within an average round trip time plus a variance tolerance until timeout reaches a set limit. If the timeout is thus at this limit and reliable packets have been sent but not acknowledged within a certain minimum time period, the peer will be disconnected. Alternatively, if reliable packets have been sent but not acknowledged for a certain maximum time period, the peer will be disconnected regardless of the current timeout limit value.

`Peer.Disconnect(uint data)` request a disconnection from a peer.

`Peer.DisconnectNow(uint data)` force an immediate disconnection from a peer.

`Peer.DisconnectLater(uint data)` request a disconnection from a peer, but only after all queued outgoing packets are sent.

`Peer.Reset()` forcefully disconnects a peer. The foreign host represented by the peer is not notified of the disconnection and will timeout on its connection to the local host.

### Classes
#### Host
Contains a managed pointer to the host.

`Host.Dispose()` destroys the host.

`Host.IsSet` returns a state of the managed pointer.

`Host.PeersCount` returns a number of connected peers.

`Host.PacketsSent` returns a total number of packets sent during the session.

`Host.PacketsReceived` returns a total number of packets received during the session.

`Host.BytesSent` returns a total number of bytes sent during the session.

`Host.BytesReceived` returns a total number of bytes received during the session.

`Host.Create(Address? address, int peerLimit, int channelLimit, uint incomingBandwidth, uint outgoingBandwidth)` creates a host for communicating with peers. The bandwidth parameters determine the window size of a connection which limits the number of reliable packets that may be in transit at any given time. ENet will strategically drop packets on specific sides of a connection between hosts to ensure the host's bandwidth is not overwhelmed. All the parameters are optional except the address and peer limit in cases where the function is used to create a host which will listen for incoming connections.

`Host.EnableCompression()` enables packet-level compression.

`Host.PreventConnections(bool state)` prevents access to the host for new incoming connections.

`Host.Broadcast(byte channelID, ref Packet packet)` queues a packet to be sent to all peers associated with the host. 

`Host.CheckEvents(out Event @event)` checks for any queued events on the host and dispatches one if available. Returns > 0 if an event was dispatched, 0 if no events are available, < 0 on failure.

`Host.Connect(Address address, int channelLimit, uint data)` initiates a connection to a foreign host. Returns a peer representing the foreign host on success or `null` on failure. The peer returned will not have completed the connection until `Host.Service()` notifies of an `EventType.Connect` event. The channel limit and user-supplied data parameters are optional.

`Host.Service(int timeout, out Event @event)` waits for events on the specified host and shuttles packets between the host and its peers. ENet uses a polled event model to notify the programmer of significant events. ENet hosts are polled for events with this function, where an optional timeout value in milliseconds may be specified to control how long ENet will poll. If a timeout of 0 is specified, this function will return immediately if there are no events to dispatch. Otherwise, it will return 1 if an event was dispatched within the specified timeout. This function should be regularly called to ensure packets are sent and received. The timeout parameter set to 0 means non-blocking which required for cases where the function is called in a game loop.

`Host.SetBandwidthLimit(uint incomingBandwidth, uint outgoingBandwidth)` adjusts the bandwidth limits of a host in bytes per second.

`Host.SetChannelLimit(int channelLimit)` limits the maximum allowed channels of future incoming connections. 

`Host.Flush()` sends any queued packets on the specified host to its designated peers. 

#### Library
Contains constant fields.

`Library.maxChannelCount` the maximum possible number of channels.

`Library.maxPeers` the maximum possible number of peers.

`Library.version` the current version of the native library.

`Library.Initialize(ref Callbacks inits)` initializes the native library. Callbacks parameter is optional and should be used only with a custom memory allocator. Should be called before starting the work. Returns true on success or false on failure.

`Library.Deinitialize()` deinitializes the native library. Should be called after the work is done.

`Library.Time` returns a current local monotonic time in milliseconds. It never reset while the application remains alive.

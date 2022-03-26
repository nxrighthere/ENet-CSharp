/*
 *  ENet reliable UDP networking library
 *  Copyright (c) 2018 Lee Salzman, Vladyslav Hrytsenko, Dominik Madarász, Stanislav Denisov
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef ENET_H
#define ENET_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define ENET_VERSION_MAJOR 2
#define ENET_VERSION_MINOR 4
#define ENET_VERSION_PATCH 8
#define ENET_VERSION_CREATE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ENET_VERSION_GET_MAJOR(version) (((version) >> 16) & 0xFF)
#define ENET_VERSION_GET_MINOR(version) (((version) >> 8) & 0xFF)
#define ENET_VERSION_GET_PATCH(version) ((version) & 0xFF)
#define ENET_VERSION ENET_VERSION_CREATE(ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH)

#define ENET_TIME_OVERFLOW 86400000
#define ENET_TIME_LESS(a, b) ((a) - (b) >= ENET_TIME_OVERFLOW)
#define ENET_TIME_GREATER(a, b) ((b) - (a) >= ENET_TIME_OVERFLOW)
#define ENET_TIME_LESS_EQUAL(a, b) (!ENET_TIME_GREATER(a, b))
#define ENET_TIME_GREATER_EQUAL(a, b) (!ENET_TIME_LESS(a, b))
#define ENET_TIME_DIFFERENCE(a, b) ((a) - (b) >= ENET_TIME_OVERFLOW ? (b) - (a) : (a) - (b))

#define ENET_MAX(x, y) ((x) > (y) ? (x) : (y))
#define ENET_MIN(x, y) ((x) < (y) ? (x) : (y))

/*
=======================================================================

	System differences

=======================================================================
*/

#ifdef _WIN32
	#ifndef ENET_NO_PRAGMA_LINK
		#pragma comment(lib, "ws2_32.lib")
		#pragma comment(lib, "winmm.lib")
	#endif

	#if _MSC_VER >= 1910
		/* It looks like there were changes as of Visual Studio 2017 and there are no 32/64 bit
		versions of _InterlockedExchange[operation], only InterlockedExchange[operation]
		(without leading underscore), so we have to distinguish between compiler versions */
		#define ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
	#endif

	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <mmsystem.h>
	#include <intrin.h>

	#if defined(_WIN32) && defined(_MSC_VER)
		#if _MSC_VER < 1900
			typedef struct timespec {
				long tv_sec;
				long tv_nsec;
			};
		#endif
		#define CLOCK_MONOTONIC 0
	#endif

	typedef SOCKET ENetSocket;

	#define ENET_SOCKET_NULL INVALID_SOCKET

	typedef struct {
		size_t dataLength;
		void* data;
	} ENetBuffer;

	#define ENET_CALLBACK __cdecl

	#ifdef ENET_DLL
		#ifdef ENET_IMPLEMENTATION
			#define ENET_API __declspec(dllexport)
		#else
			#define ENET_API __declspec(dllimport)
		#endif
	#else
		#define ENET_API extern
	#endif
#else
	#include <sys/types.h>
	#include <sys/ioctl.h>
	#include <sys/time.h>
	#include <sys/socket.h>
	#include <poll.h>
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <string.h>
	#include <errno.h>
	#include <fcntl.h>

	#ifdef __APPLE__
		#include <mach/clock.h>
		#include <mach/mach.h>
		#include <Availability.h>
	#endif

	#ifndef MSG_NOSIGNAL
		#define MSG_NOSIGNAL 0
	#endif

	#ifdef MSG_MAXIOVLEN
		#define ENET_BUFFER_MAXIMUM MSG_MAXIOVLEN
	#endif

	typedef int ENetSocket;

	#define ENET_SOCKET_NULL -1

	typedef struct {
		void* data;
		size_t dataLength;
	} ENetBuffer;

	#define ENET_CALLBACK
	#define ENET_API extern
#endif

#ifndef ENET_BUFFER_MAXIMUM
	#define ENET_BUFFER_MAXIMUM (1 + 2 * ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS)
#endif

#define ENET_HOST_ANY in6addr_any
#define ENET_PORT_ANY 0
#define ENET_HOST_SIZE 1025

#define ENET_HOST_TO_NET_16(value) (htons(value))
#define ENET_HOST_TO_NET_32(value) (htonl(value))
#define ENET_NET_TO_HOST_16(value) (ntohs(value))
#define ENET_NET_TO_HOST_32(value) (ntohl(value))

#if __BIG_ENDIAN__
	#define ENET_HOST_TO_NET_64(x) (x)
	#define ENET_NET_TO_HOST_64(x) (x)
#else
	#define ENET_HOST_TO_NET_64(x) (((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
	#define ENET_NET_TO_HOST_64(x) (((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
=======================================================================

	Internals

=======================================================================
*/

	typedef uint32_t ENetVersion;

	typedef fd_set ENetSocketSet;

	typedef struct _ENetCallbacks {
		void* (ENET_CALLBACK *malloc)(size_t size);
		void (ENET_CALLBACK *free)(void* memory);
		void (ENET_CALLBACK *noMemory)(void);
	} ENetCallbacks;

	extern void* enet_malloc(size_t);
	extern void enet_free(void*);

	typedef struct _ENetListNode {
		struct _ENetListNode* next;
		struct _ENetListNode* previous;
	} ENetListNode;

	typedef ENetListNode* ENetListIterator;

	typedef struct _ENetList {
		ENetListNode sentinel;
	} ENetList;

	extern ENetListIterator enet_list_insert(ENetListIterator, void*);
	extern ENetListIterator enet_list_move(ENetListIterator, void*, void*);
	extern void* enet_list_remove(ENetListIterator);
	extern void enet_list_clear(ENetList*);
	extern size_t enet_list_size(ENetList*);

	#define enet_list_begin(list) ((list)->sentinel.next)
	#define enet_list_end(list) (&(list)->sentinel)
	#define enet_list_empty(list) (enet_list_begin(list) == enet_list_end(list))
	#define enet_list_next(iterator) ((iterator)->next)
	#define enet_list_previous(iterator) ((iterator)->previous)
	#define enet_list_front(list) ((void*)(list)->sentinel.next)
	#define enet_list_back(list) ((void*)(list)->sentinel.previous)

	#define enet_in6_equal(a, b) (memcmp(&a, &b, sizeof(struct in6_addr)) == 0)

/*
=======================================================================

	Protocol

=======================================================================
*/

	enum {
		ENET_PROTOCOL_MINIMUM_MTU             = 576,
		ENET_PROTOCOL_MAXIMUM_MTU             = 4096,
		ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS = 32,
		ENET_PROTOCOL_MINIMUM_WINDOW_SIZE     = 4096,
		ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE     = 65536,
		ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT   = 1,
		ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT   = 255,
		ENET_PROTOCOL_MAXIMUM_PEER_ID         = 0xFFF,
		ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT  = 1024 * 1024
	};

	typedef enum _ENetProtocolCommand {
		ENET_PROTOCOL_COMMAND_NONE                     = 0,
		ENET_PROTOCOL_COMMAND_ACKNOWLEDGE              = 1,
		ENET_PROTOCOL_COMMAND_CONNECT                  = 2,
		ENET_PROTOCOL_COMMAND_VERIFY_CONNECT           = 3,
		ENET_PROTOCOL_COMMAND_DISCONNECT               = 4,
		ENET_PROTOCOL_COMMAND_PING                     = 5,
		ENET_PROTOCOL_COMMAND_SEND_RELIABLE            = 6,
		ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE          = 7,
		ENET_PROTOCOL_COMMAND_SEND_FRAGMENT            = 8,
		ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED         = 9,
		ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT          = 10,
		ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE       = 11,
		ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT = 12,
		ENET_PROTOCOL_COMMAND_COUNT                    = 13,
		ENET_PROTOCOL_COMMAND_MASK                     = 0x0F
	} ENetProtocolCommand;

	typedef enum _ENetProtocolFlag {
		ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE = (1 << 7),
		ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED = (1 << 6),
		ENET_PROTOCOL_HEADER_FLAG_SENT_TIME    = (1 << 14),
		ENET_PROTOCOL_HEADER_FLAG_MASK         = ENET_PROTOCOL_HEADER_FLAG_SENT_TIME,
		ENET_PROTOCOL_HEADER_SESSION_MASK      = (3 << 12),
		ENET_PROTOCOL_HEADER_SESSION_SHIFT     = 12
	} ENetProtocolFlag;

	#ifdef _MSC_VER
		#pragma pack(push, 1)
		#define ENET_PACKED
	#elif defined(__GNUC__) || defined(__clang__)
		#define ENET_PACKED __attribute__ ((packed))
	#else
		#define ENET_PACKED
	#endif

	typedef struct _ENetProtocolHeader {
		uint16_t peerID;
		uint16_t sentTime;
	} ENET_PACKED ENetProtocolHeader;

	typedef struct _ENetProtocolCommandHeader {
		uint8_t command;
		uint8_t channelID;
		uint16_t reliableSequenceNumber;
	} ENET_PACKED ENetProtocolCommandHeader;

	typedef struct _ENetProtocolAcknowledge {
		ENetProtocolCommandHeader header;
		uint16_t receivedReliableSequenceNumber;
		uint16_t receivedSentTime;
	} ENET_PACKED ENetProtocolAcknowledge;

	typedef struct _ENetProtocolConnect {
		ENetProtocolCommandHeader header;
		uint16_t outgoingPeerID;
		uint8_t incomingSessionID;
		uint8_t outgoingSessionID;
		uint32_t mtu;
		uint32_t windowSize;
		uint32_t channelCount;
		uint32_t incomingBandwidth;
		uint32_t outgoingBandwidth;
		uint32_t packetThrottleInterval;
		uint32_t packetThrottleAcceleration;
		uint32_t packetThrottleDeceleration;
		uint32_t connectID;
		uint32_t data;
	} ENET_PACKED ENetProtocolConnect;

	typedef struct _ENetProtocolVerifyConnect {
		ENetProtocolCommandHeader header;
		uint16_t outgoingPeerID;
		uint8_t incomingSessionID;
		uint8_t outgoingSessionID;
		uint32_t mtu;
		uint32_t windowSize;
		uint32_t channelCount;
		uint32_t incomingBandwidth;
		uint32_t outgoingBandwidth;
		uint32_t packetThrottleInterval;
		uint32_t packetThrottleAcceleration;
		uint32_t packetThrottleDeceleration;
		uint32_t connectID;
	} ENET_PACKED ENetProtocolVerifyConnect;

	typedef struct _ENetProtocolBandwidthLimit {
		ENetProtocolCommandHeader header;
		uint32_t incomingBandwidth;
		uint32_t outgoingBandwidth;
	} ENET_PACKED ENetProtocolBandwidthLimit;

	typedef struct _ENetProtocolThrottleConfigure {
		ENetProtocolCommandHeader header;
		uint32_t packetThrottleInterval;
		uint32_t packetThrottleAcceleration;
		uint32_t packetThrottleDeceleration;
	} ENET_PACKED ENetProtocolThrottleConfigure;

	typedef struct _ENetProtocolDisconnect {
		ENetProtocolCommandHeader header;
		uint32_t data;
	} ENET_PACKED ENetProtocolDisconnect;

	typedef struct _ENetProtocolPing {
		ENetProtocolCommandHeader header;
	} ENET_PACKED ENetProtocolPing;

	typedef struct _ENetProtocolSendReliable {
		ENetProtocolCommandHeader header;
		uint16_t dataLength;
	} ENET_PACKED ENetProtocolSendReliable;

	typedef struct _ENetProtocolSendUnreliable {
		ENetProtocolCommandHeader header;
		uint16_t unreliableSequenceNumber;
		uint16_t dataLength;
	} ENET_PACKED ENetProtocolSendUnreliable;

	typedef struct _ENetProtocolSendUnsequenced {
		ENetProtocolCommandHeader header;
		uint16_t unsequencedGroup;
		uint16_t dataLength;
	} ENET_PACKED ENetProtocolSendUnsequenced;

	typedef struct _ENetProtocolSendFragment {
		ENetProtocolCommandHeader header;
		uint16_t startSequenceNumber;
		uint16_t dataLength;
		uint32_t fragmentCount;
		uint32_t fragmentNumber;
		uint32_t totalLength;
		uint32_t fragmentOffset;
	} ENET_PACKED ENetProtocolSendFragment;

	typedef union _ENetProtocol {
		ENetProtocolCommandHeader header;
		ENetProtocolAcknowledge acknowledge;
		ENetProtocolConnect connect;
		ENetProtocolVerifyConnect verifyConnect;
		ENetProtocolDisconnect disconnect;
		ENetProtocolPing ping;
		ENetProtocolSendReliable sendReliable;
		ENetProtocolSendUnreliable sendUnreliable;
		ENetProtocolSendUnsequenced sendUnsequenced;
		ENetProtocolSendFragment sendFragment;
		ENetProtocolBandwidthLimit bandwidthLimit;
		ENetProtocolThrottleConfigure throttleConfigure;
	} ENET_PACKED ENetProtocol;

	#ifdef _MSC_VER
		#pragma pack(pop)
	#endif

/*
=======================================================================

	General structs/enums

=======================================================================
*/

	typedef enum _ENetSocketType {
		ENET_SOCKET_TYPE_STREAM   = 1,
		ENET_SOCKET_TYPE_DATAGRAM = 2
	} ENetSocketType;

	typedef enum _ENetSocketWait {
		ENET_SOCKET_WAIT_NONE      = 0,
		ENET_SOCKET_WAIT_SEND      = (1 << 0),
		ENET_SOCKET_WAIT_RECEIVE   = (1 << 1),
		ENET_SOCKET_WAIT_INTERRUPT = (1 << 2)
	} ENetSocketWait;

	typedef enum _ENetSocketOption {
		ENET_SOCKOPT_NONBLOCK    = 1,
		ENET_SOCKOPT_BROADCAST   = 2,
		ENET_SOCKOPT_RCVBUF      = 3,
		ENET_SOCKOPT_SNDBUF      = 4,
		ENET_SOCKOPT_REUSEADDR   = 5,
		ENET_SOCKOPT_RCVTIMEO    = 6,
		ENET_SOCKOPT_SNDTIMEO    = 7,
		ENET_SOCKOPT_ERROR       = 8,
		ENET_SOCKOPT_NODELAY     = 9,
		ENET_SOCKOPT_IPV6_V6ONLY = 10
	} ENetSocketOption;

	typedef enum _ENetSocketShutdown {
		ENET_SOCKET_SHUTDOWN_READ       = 0,
		ENET_SOCKET_SHUTDOWN_WRITE      = 1,
		ENET_SOCKET_SHUTDOWN_READ_WRITE = 2
	} ENetSocketShutdown;

	typedef struct _ENetAddress {
		union {
			struct in6_addr ipv6;
			struct {
				uint8_t zeros[10];
				uint16_t ffff;
				struct in_addr ip;
			} ipv4;
		};
		uint16_t port;
	} ENetAddress;

	typedef enum _ENetPacketFlag {
		ENET_PACKET_FLAG_NONE                  = 0,
		ENET_PACKET_FLAG_RELIABLE              = (1 << 0),
		ENET_PACKET_FLAG_UNSEQUENCED           = (1 << 1),
		ENET_PACKET_FLAG_NO_ALLOCATE           = (1 << 2),
		ENET_PACKET_FLAG_UNRELIABLE_FRAGMENTED = (1 << 3),
		ENET_PACKET_FLAG_INSTANT               = (1 << 4),
		ENET_PACKET_FLAG_UNTHROTTLED           = (1 << 5),
		ENET_PACKET_FLAG_SENT                  = (1 << 8)
	} ENetPacketFlag;

	typedef void (ENET_CALLBACK *ENetPacketFreeCallback)(void*);

	typedef struct _ENetPacket {
		uint32_t flags;
		uint32_t dataLength;
		uint8_t* data;
		ENetPacketFreeCallback freeCallback;
		uint32_t referenceCount;
		void* userData;
	} ENetPacket;

	typedef struct _ENetAcknowledgement {
		ENetListNode acknowledgementList;
		uint32_t sentTime;
		ENetProtocol command;
	} ENetAcknowledgement;

	typedef struct _ENetOutgoingCommand {
		ENetListNode outgoingCommandList;
		uint16_t reliableSequenceNumber;
		uint16_t unreliableSequenceNumber;
		uint32_t sentTime;
		uint32_t roundTripTimeout;
		uint32_t roundTripTimeoutLimit;
		uint32_t fragmentOffset;
		uint16_t fragmentLength;
		uint16_t sendAttempts;
		ENetProtocol command;
		ENetPacket* packet;
	} ENetOutgoingCommand;

	typedef struct _ENetIncomingCommand {
		ENetListNode incomingCommandList;
		uint16_t reliableSequenceNumber;
		uint16_t unreliableSequenceNumber;
		ENetProtocol command;
		uint32_t fragmentCount;
		uint32_t fragmentsRemaining;
		uint32_t* fragments;
		ENetPacket* packet;
	} ENetIncomingCommand;

	typedef enum _ENetPeerState {
		ENET_PEER_STATE_DISCONNECTED             = 0,
		ENET_PEER_STATE_CONNECTING               = 1,
		ENET_PEER_STATE_ACKNOWLEDGING_CONNECT    = 2,
		ENET_PEER_STATE_CONNECTION_PENDING       = 3,
		ENET_PEER_STATE_CONNECTION_SUCCEEDED     = 4,
		ENET_PEER_STATE_CONNECTED                = 5,
		ENET_PEER_STATE_DISCONNECT_LATER         = 6,
		ENET_PEER_STATE_DISCONNECTING            = 7,
		ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT = 8,
		ENET_PEER_STATE_ZOMBIE                   = 9
	} ENetPeerState;

	enum {
		ENET_HOST_BUFFER_SIZE_MIN              = 256 * 1024,
		ENET_HOST_BUFFER_SIZE_MAX              = 1024 * 1024,
		ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL  = 1000,
		ENET_HOST_DEFAULT_MTU                  = 1280,
		ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE  = 32 * 1024 * 1024,
		ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA = 32 * 1024 * 1024,
		ENET_PEER_DEFAULT_ROUND_TRIP_TIME      = 1,
		ENET_PEER_DEFAULT_PACKET_THROTTLE      = 32,
		ENET_PEER_PACKET_THROTTLE_THRESHOLD    = 40,
		ENET_PEER_PACKET_THROTTLE_SCALE        = 32,
		ENET_PEER_PACKET_THROTTLE_COUNTER      = 7,
		ENET_PEER_PACKET_THROTTLE_ACCELERATION = 2,
		ENET_PEER_PACKET_THROTTLE_DECELERATION = 2,
		ENET_PEER_PACKET_THROTTLE_INTERVAL     = 5000,
		ENET_PEER_WINDOW_SIZE_SCALE            = 64 * 1024,
		ENET_PEER_TIMEOUT_LIMIT                = 32,
		ENET_PEER_TIMEOUT_MINIMUM              = 5000,
		ENET_PEER_TIMEOUT_MAXIMUM              = 30000,
		ENET_PEER_PING_INTERVAL                = 250,
		ENET_PEER_UNSEQUENCED_WINDOWS          = 64,
		ENET_PEER_UNSEQUENCED_WINDOW_SIZE      = 1024,
		ENET_PEER_FREE_UNSEQUENCED_WINDOWS     = 32,
		ENET_PEER_RELIABLE_WINDOWS             = 16,
		ENET_PEER_RELIABLE_WINDOW_SIZE         = 0x1000,
		ENET_PEER_FREE_RELIABLE_WINDOWS        = 8
	};

	typedef struct _ENetChannel {
		uint16_t outgoingReliableSequenceNumber;
		uint16_t outgoingUnreliableSequenceNumber;
		uint16_t usedReliableWindows;
		uint16_t reliableWindows[ENET_PEER_RELIABLE_WINDOWS];
		uint16_t incomingReliableSequenceNumber;
		uint16_t incomingUnreliableSequenceNumber;
		ENetList incomingReliableCommands;
		ENetList incomingUnreliableCommands;
	} ENetChannel;

	typedef struct _ENetPeer {
		ENetListNode dispatchList;
		struct _ENetHost* host;
		uint16_t outgoingPeerID;
		uint16_t incomingPeerID;
		uint32_t connectID;
		uint8_t outgoingSessionID;
		uint8_t incomingSessionID;
		ENetAddress address;
		void* data;
		ENetPeerState state;
		ENetChannel* channels;
		size_t channelCount;
		uint32_t incomingBandwidth;
		uint32_t outgoingBandwidth;
		uint32_t incomingBandwidthThrottleEpoch;
		uint32_t outgoingBandwidthThrottleEpoch;
		uint32_t incomingDataTotal;
		uint64_t totalDataReceived;
		uint32_t outgoingDataTotal;
		uint64_t totalDataSent;
		uint32_t lastSendTime;
		uint32_t lastReceiveTime;
		uint32_t nextTimeout;
		uint32_t earliestTimeout;
		uint64_t totalPacketsSent;
		uint64_t totalPacketsLost;
		uint32_t packetThrottle;
		uint32_t packetThrottleThreshold;
		uint32_t packetThrottleLimit;
		uint32_t packetThrottleCounter;
		uint32_t packetThrottleEpoch;
		uint32_t packetThrottleAcceleration;
		uint32_t packetThrottleDeceleration;
		uint32_t packetThrottleInterval;
		uint32_t pingInterval;
		uint32_t timeoutLimit;
		uint32_t timeoutMinimum;
		uint32_t timeoutMaximum;
		uint32_t lastRoundTripTime;
		uint32_t lowestRoundTripTime;
		uint32_t lastRoundTripTimeVariance;
		uint32_t highestRoundTripTimeVariance;
		uint32_t roundTripTime;
		uint32_t roundTripTimeVariance;
		uint32_t mtu;
		uint32_t windowSize;
		uint32_t reliableDataInTransit;
		uint16_t outgoingReliableSequenceNumber;
		ENetList acknowledgements;
		ENetList sentReliableCommands;
		ENetList sentUnreliableCommands;
		ENetList outgoingCommands;
		ENetList dispatchedCommands;
		int needsDispatch;
		uint16_t incomingUnsequencedGroup;
		uint16_t outgoingUnsequencedGroup;
		uint32_t unsequencedWindow[ENET_PEER_UNSEQUENCED_WINDOW_SIZE / 32];
		uint32_t eventData;
		size_t totalWaitingData;
	} ENetPeer;

	typedef enum _ENetEventType {
		ENET_EVENT_TYPE_NONE               = 0,
		ENET_EVENT_TYPE_CONNECT            = 1,
		ENET_EVENT_TYPE_DISCONNECT         = 2,
		ENET_EVENT_TYPE_RECEIVE            = 3,
		ENET_EVENT_TYPE_DISCONNECT_TIMEOUT = 4
	} ENetEventType;

	typedef struct _ENetEvent {
		ENetEventType type;
		ENetPeer* peer;
		uint8_t channelID;
		uint32_t data;
		ENetPacket* packet;
	} ENetEvent;

	typedef uint64_t (ENET_CALLBACK *ENetChecksumCallback)(const ENetBuffer* buffers, int bufferCount);

	typedef int (ENET_CALLBACK *ENetInterceptCallback)(ENetEvent* event, ENetAddress* address, uint8_t* receivedData, int receivedDataLength);

	typedef struct _ENetHost {
		ENetSocket socket;
		ENetAddress address;
		uint32_t incomingBandwidth;
		uint32_t outgoingBandwidth;
		uint32_t bandwidthThrottleEpoch;
		uint32_t mtu;
		uint32_t randomSeed;
		int recalculateBandwidthLimits;
		uint8_t preventConnections;
		ENetPeer* peers;
		size_t peerCount;
		size_t channelLimit;
		uint32_t serviceTime;
		ENetList dispatchQueue;
		int continueSending;
		size_t packetSize;
		uint16_t headerFlags;
		uint32_t totalSentData;
		uint32_t totalSentPackets;
		uint32_t totalReceivedData;
		uint32_t totalReceivedPackets;
		ENetProtocol commands[ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS];
		size_t commandCount;
		ENetBuffer buffers[ENET_BUFFER_MAXIMUM];
		size_t bufferCount;
		ENetChecksumCallback checksumCallback;
		uint8_t packetData[2][ENET_PROTOCOL_MAXIMUM_MTU];
		ENetAddress receivedAddress;
		uint8_t* receivedData;
		size_t receivedDataLength;
		ENetInterceptCallback interceptCallback;
		size_t connectedPeers;
		size_t bandwidthLimitedPeers;
		size_t duplicatePeers;
		size_t maximumPacketSize;
		size_t maximumWaitingData;
	} ENetHost;

/*
=======================================================================

	Public API

=======================================================================
*/

	ENET_API int enet_initialize(void);
	ENET_API int enet_initialize_with_callbacks(ENetVersion, const ENetCallbacks*);
	ENET_API void enet_deinitialize(void);
	ENET_API ENetVersion enet_linked_version(void);
	ENET_API int enet_array_is_zeroed(const uint8_t*, int);
	ENET_API uint32_t enet_time_get(void);
	ENET_API uint64_t enet_crc64(const ENetBuffer*, int);

	ENET_API ENetPacket* enet_packet_create(const void*, size_t, uint32_t);
	ENET_API ENetPacket* enet_packet_create_offset(const void*, size_t, size_t, uint32_t);
	ENET_API void enet_packet_destroy(ENetPacket*);

	ENET_API int enet_peer_send(ENetPeer*, uint8_t, ENetPacket*);
	ENET_API ENetPacket* enet_peer_receive(ENetPeer*, uint8_t*);
	ENET_API void enet_peer_ping(ENetPeer*);
	ENET_API void enet_peer_ping_interval(ENetPeer*, uint32_t);
	ENET_API void enet_peer_timeout(ENetPeer*, uint32_t, uint32_t, uint32_t);
	ENET_API void enet_peer_reset(ENetPeer*);
	ENET_API void enet_peer_disconnect(ENetPeer*, uint32_t);
	ENET_API void enet_peer_disconnect_now(ENetPeer*, uint32_t);
	ENET_API void enet_peer_disconnect_later(ENetPeer*, uint32_t);
	ENET_API void enet_peer_throttle_configure(ENetPeer*, uint32_t, uint32_t, uint32_t, uint32_t);

	ENET_API ENetHost* enet_host_create(const ENetAddress*, size_t, size_t, uint32_t, uint32_t, int);
	ENET_API void enet_host_destroy(ENetHost*);
	ENET_API void enet_host_prevent_connections(ENetHost*, uint8_t);
	ENET_API ENetPeer* enet_host_connect(ENetHost*, const ENetAddress*, size_t, uint32_t);
	ENET_API int enet_host_check_events(ENetHost*, ENetEvent*);
	ENET_API int enet_host_service(ENetHost*, ENetEvent*, uint32_t);
	ENET_API void enet_host_flush(ENetHost*);
	ENET_API void enet_host_broadcast(ENetHost*, uint8_t, ENetPacket*);
	ENET_API void enet_host_broadcast_exclude(ENetHost*, uint8_t, ENetPacket*, ENetPeer*);
	ENET_API void enet_host_broadcast_selective(ENetHost*, uint8_t, ENetPacket*, ENetPeer**, size_t);
	ENET_API void enet_host_channel_limit(ENetHost*, size_t);
	ENET_API void enet_host_bandwidth_limit(ENetHost*, uint32_t, uint32_t);

	ENET_API int enet_address_set_ip(ENetAddress*, const char*);
	ENET_API int enet_address_set_hostname(ENetAddress*, const char*);
	ENET_API int enet_address_get_ip(const ENetAddress*, char*, size_t);
	ENET_API int enet_address_get_hostname(const ENetAddress*, char*, size_t);

	ENET_API ENetSocket enet_socket_create(ENetSocketType);
	ENET_API int enet_socket_bind(ENetSocket, const ENetAddress*);
	ENET_API int enet_socket_get_address(ENetSocket, ENetAddress*);
	ENET_API int enet_socket_listen(ENetSocket, int);
	ENET_API ENetSocket enet_socket_accept(ENetSocket, ENetAddress*);
	ENET_API int enet_socket_connect(ENetSocket, const ENetAddress*);
	ENET_API int enet_socket_send(ENetSocket, const ENetAddress*, const ENetBuffer*, size_t);
	ENET_API int enet_socket_receive(ENetSocket, ENetAddress*, ENetBuffer*, size_t);
	ENET_API int enet_socket_wait(ENetSocket, uint32_t*, uint64_t);
	ENET_API int enet_socket_set_option(ENetSocket, ENetSocketOption, int);
	ENET_API int enet_socket_get_option(ENetSocket, ENetSocketOption, int*);
	ENET_API int enet_socket_shutdown(ENetSocket, ENetSocketShutdown);
	ENET_API void enet_socket_destroy(ENetSocket);
	ENET_API int enet_socket_set_select(ENetSocket, ENetSocketSet*, ENetSocketSet*, uint32_t);

	/* Extended API for easier binding in other programming languages */
	ENET_API void* enet_packet_get_data(const ENetPacket*);
	ENET_API void* enet_packet_get_user_data(const ENetPacket*);
	ENET_API void enet_packet_set_user_data(ENetPacket*, void* userData);
	ENET_API int enet_packet_get_length(const ENetPacket*);
	ENET_API void enet_packet_set_free_callback(ENetPacket*, ENetPacketFreeCallback);
	ENET_API int enet_packet_check_references(const ENetPacket*);
	ENET_API void enet_packet_dispose(ENetPacket*);

	ENET_API uint32_t enet_host_get_peers_count(const ENetHost*);
	ENET_API uint32_t enet_host_get_packets_sent(const ENetHost*);
	ENET_API uint32_t enet_host_get_packets_received(const ENetHost*);
	ENET_API uint32_t enet_host_get_bytes_sent(const ENetHost*);
	ENET_API uint32_t enet_host_get_bytes_received(const ENetHost*);
	ENET_API void enet_host_set_max_duplicate_peers(ENetHost*, uint16_t);
	ENET_API void enet_host_set_intercept_callback(ENetHost*, ENetInterceptCallback);
	ENET_API void enet_host_set_checksum_callback(ENetHost*, ENetChecksumCallback);

	ENET_API uint32_t enet_peer_get_id(const ENetPeer*);
	ENET_API int enet_peer_get_ip(const ENetPeer*, char*, size_t);
	ENET_API uint16_t enet_peer_get_port(const ENetPeer*);
	ENET_API uint32_t enet_peer_get_mtu(const ENetPeer*);
	ENET_API ENetPeerState enet_peer_get_state(const ENetPeer*);
	ENET_API uint32_t enet_peer_get_rtt(const ENetPeer*);
	ENET_API uint32_t enet_peer_get_last_rtt(const ENetPeer* peer);
	ENET_API uint32_t enet_peer_get_lastsendtime(const ENetPeer*);
	ENET_API uint32_t enet_peer_get_lastreceivetime(const ENetPeer*);
	ENET_API uint64_t enet_peer_get_packets_sent(const ENetPeer*);
	ENET_API uint64_t enet_peer_get_packets_lost(const ENetPeer*);
	ENET_API float enet_peer_get_packets_throttle(const ENetPeer*);
	ENET_API uint64_t enet_peer_get_bytes_sent(const ENetPeer*);
	ENET_API uint64_t enet_peer_get_bytes_received(const ENetPeer*);
	ENET_API void* enet_peer_get_data(const ENetPeer*);
	ENET_API void enet_peer_set_data(ENetPeer*, const void*);

/*
=======================================================================

	Private API

=======================================================================
*/

	extern void enet_host_bandwidth_throttle(ENetHost*);
	extern uint64_t enet_host_random_seed(void);

	extern int enet_peer_throttle(ENetPeer*, uint32_t);
	extern void enet_peer_reset_queues(ENetPeer*);
	extern void enet_peer_setup_outgoing_command(ENetPeer*, ENetOutgoingCommand*);
	extern ENetOutgoingCommand* enet_peer_queue_outgoing_command(ENetPeer*, const ENetProtocol*, ENetPacket*, uint32_t, uint16_t);
	extern ENetIncomingCommand* enet_peer_queue_incoming_command(ENetPeer*, const ENetProtocol*, const void*, size_t, uint32_t, uint32_t);
	extern ENetAcknowledgement* enet_peer_queue_acknowledgement(ENetPeer*, const ENetProtocol*, uint16_t);
	extern void enet_peer_dispatch_incoming_unreliable_commands(ENetPeer*, ENetChannel*, ENetIncomingCommand*);
	extern void enet_peer_dispatch_incoming_reliable_commands(ENetPeer*, ENetChannel*, ENetIncomingCommand*);
	extern void enet_peer_on_connect(ENetPeer*);
	extern void enet_peer_on_disconnect(ENetPeer*);

	extern size_t enet_protocol_command_size(uint8_t);

#ifdef __cplusplus
}
#endif

#if defined(ENET_IMPLEMENTATION) && !defined(ENET_IMPLEMENTATION_DONE)
	#define ENET_IMPLEMENTATION_DONE 1

	#ifdef _MSC_VER
		#pragma warning(push)
		#pragma warning(disable: 4244) /* 64-bit to 32-bit integer conversion */
		#pragma warning(disable: 4267) /* size_t to integer conversion */
	#endif

/*
=======================================================================

	Atomics

=======================================================================
*/

	#ifdef _MSC_VER
		#define ENET_AT_CASSERT_PRED(predicate) sizeof(char[2 * !!(predicate) - 1])
		#define ENET_IS_SUPPORTED_ATOMIC(size) ENET_AT_CASSERT_PRED(size == 1 || size == 2 || size == 4 || size == 8)
		#define ENET_ATOMIC_SIZEOF(variable) (ENET_IS_SUPPORTED_ATOMIC(sizeof(*(variable))), sizeof(*(variable)))

		__inline int64_t enet_at_atomic_read(char* ptr, size_t size) {
			switch (size) {
				case 1:
					return _InterlockedExchangeAdd8((volatile char*)ptr, 0);

				case 2:
					return _InterlockedExchangeAdd16((volatile SHORT*)ptr, 0);

				case 4:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchangeAdd((volatile LONG*)ptr, 0);
					#else
						return _InterlockedExchangeAdd((volatile LONG*)ptr, 0);
					#endif

				case 8:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchangeAdd64((volatile LONGLONG*)ptr, 0);
					#else
						return _InterlockedExchangeAdd64((volatile LONGLONG*)ptr, 0);
					#endif

				default:
					return 0x0;
			}
		}

		__inline int64_t enet_at_atomic_write(char* ptr, int64_t value, size_t size) {
			switch (size) {
				case 1:
					return _InterlockedExchange8((volatile char*)ptr, (char)value);

				case 2:
					return _InterlockedExchange16((volatile SHORT*)ptr, (SHORT)value);

				case 4:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchange((volatile LONG*)ptr, (LONG)value);
					#else
						return _InterlockedExchange((volatile LONG*)ptr, (LONG)value);
					#endif

				case 8:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchange64((volatile LONGLONG*)ptr, (LONGLONG)value);
					#else
						return _InterlockedExchange64((volatile LONGLONG*)ptr, (LONGLONG)value);
					#endif

				default:
					return 0x0;
			}
		}

		__inline int64_t enet_at_atomic_cas(char* ptr, int64_t new_val, int64_t old_val, size_t size) {
			switch (size) {
				case 1:
					return _InterlockedCompareExchange8((volatile char*)ptr, (char)new_val, (char)old_val);

				case 2:
					return _InterlockedCompareExchange16((volatile SHORT*)ptr, (SHORT)new_val, (SHORT)old_val);

				case 4:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedCompareExchange((volatile LONG*)ptr, (LONG)new_val, (LONG)old_val);
					#else
						return _InterlockedCompareExchange((volatile LONG*)ptr, (LONG)new_val, (LONG)old_val);
					#endif

				case 8:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedCompareExchange64((volatile LONGLONG*)ptr, (LONGLONG)new_val, (LONGLONG)old_val);
					#else
						return _InterlockedCompareExchange64((volatile LONGLONG*)ptr, (LONGLONG)new_val, (LONGLONG)old_val);
					#endif

				default:
					return 0x0;
			}
		}

		__inline int64_t enet_at_atomic_inc(char* ptr, int64_t delta, size_t data_size) {
			switch (data_size) {
				case 1:
					return _InterlockedExchangeAdd8((volatile char*)ptr, (char)delta);

				case 2:
					return _InterlockedExchangeAdd16((volatile SHORT*)ptr, (SHORT)delta);

				case 4:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchangeAdd((volatile LONG*)ptr, (LONG)delta);
					#else
						return _InterlockedExchangeAdd((volatile LONG*)ptr, (LONG)delta);
					#endif

				case 8:
					#ifdef ENET_NOT_UNDERSCORED_INTERLOCKED_EXCHANGE
						return InterlockedExchangeAdd64((volatile LONGLONG*)ptr, (LONGLONG)delta);
					#else
						return _InterlockedExchangeAdd64((volatile LONGLONG*)ptr, (LONGLONG)delta);
					#endif

				default:
					return 0x0;
			}
		}

		#define ENET_ATOMIC_READ(variable) enet_at_atomic_read((char*)(variable), ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_WRITE(variable, new_val) enet_at_atomic_write((char*)(variable), (int64_t)(new_val), ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_CAS(variable, old_value, new_val) enet_at_atomic_cas((char*)(variable), (int64_t)(new_val), (int64_t)(old_value), ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_INC(variable) enet_at_atomic_inc((char*)(variable), 1, ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_DEC(variable) enet_at_atomic_inc((char*)(variable), -1, ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_INC_BY(variable, delta) enet_at_atomic_inc((char*)(variable), (delta), ENET_ATOMIC_SIZEOF(variable))
		#define ENET_ATOMIC_DEC_BY(variable, delta) enet_at_atomic_inc((char*)(variable), -(delta), ENET_ATOMIC_SIZEOF(variable))
	#elif defined(__GNUC__) || defined(__clang__)
		#if defined(__clang__) || (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
			#define AT_HAVE_ATOMICS
		#endif

		/* We want to use __atomic built-ins if possible because the __sync primitives are
		deprecated, because the __atomic build-ins allow us to use ENET_ATOMIC_WRITE on
		uninitialized memory without running into undefined behavior, and because the
		__atomic versions generate more efficient code since we don't need to rely on
		CAS when we don't actually want it.

		Note that we use acquire-release memory order (like mutexes do). We could use
		sequentially consistent memory order but that has lower performance and is
		almost always unneeded. */
		#ifdef AT_HAVE_ATOMICS
			#define ENET_ATOMIC_READ(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
			#define ENET_ATOMIC_WRITE(ptr, value) __atomic_store_n((ptr), (value), __ATOMIC_RELEASE)

			#ifndef typeof
				#define typeof __typeof__
			#endif

			/* clang_analyzer doesn't know that CAS writes to memory so it complains about
			potentially lost data. Replace the code with the equivalent non-sync code. */
			#ifdef __clang_analyzer__
				#define ENET_ATOMIC_CAS(ptr, old_value, new_value)\
					({\
						typeof(*(ptr)) ENET_ATOMIC_CAS_old_actual_ = (*(ptr));\
						if (ATOMIC_CAS_old_actual_ == (old_value))\
							*(ptr) = new_value;\
						ENET_ATOMIC_CAS_old_actual_;\
					})
				#else
				/* Could use __auto_type instead of typeof but that shouldn't work in C++.
				The ({ }) syntax is a GCC extension called statement expression. It lets
				us return a value out of the macro.

				TODO We should return bool here instead of the old value to avoid the ABA
				problem. */
				#define ENET_ATOMIC_CAS(ptr, old_value, new_value)\
					({\
						typeof(*(ptr)) ENET_ATOMIC_CAS_expected_ = (old_value);\
						__atomic_compare_exchange_n((ptr), &ENET_ATOMIC_CAS_expected_, (new_value), false,\
						__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);\
						ENET_ATOMIC_CAS_expected_;\
					})
				#endif

				#define ENET_ATOMIC_INC(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_ACQ_REL)
				#define ENET_ATOMIC_DEC(ptr) __atomic_fetch_sub((ptr), 1, __ATOMIC_ACQ_REL)
				#define ENET_ATOMIC_INC_BY(ptr, delta) __atomic_fetch_add((ptr), (delta), __ATOMIC_ACQ_REL)
				#define ENET_ATOMIC_DEC_BY(ptr, delta) __atomic_fetch_sub((ptr), (delta), __ATOMIC_ACQ_REL)
			#else
				#define ENET_ATOMIC_READ(variable) __sync_fetch_and_add(variable, 0)
				#define ENET_ATOMIC_WRITE(variable, new_val) (void)__sync_val_compare_and_swap((variable), *(variable), (new_val))
				#define ENET_ATOMIC_CAS(variable, old_value, new_val) __sync_val_compare_and_swap((variable), (old_value), (new_val))
				#define ENET_ATOMIC_INC(variable) __sync_fetch_and_add((variable), 1)
				#define ENET_ATOMIC_DEC(variable) __sync_fetch_and_sub((variable), 1)
				#define ENET_ATOMIC_INC_BY(variable, delta) __sync_fetch_and_add((variable), (delta), 1)
				#define ENET_ATOMIC_DEC_BY(variable, delta) __sync_fetch_and_sub((variable), (delta), 1)
			#endif
		#undef AT_HAVE_ATOMICS
	#endif

/*
=======================================================================

	Callbacks

=======================================================================
*/

	static ENetCallbacks callbacks = {
		malloc,
		free,
		abort
	};

	int enet_initialize_with_callbacks(ENetVersion version, const ENetCallbacks* inits) {
		if (version < ENET_VERSION_CREATE(1, 3, 0))
			return -1;

		if (inits->malloc != NULL || inits->free != NULL) {
			if (inits->malloc == NULL || inits->free == NULL)
				return -1;

			callbacks.malloc = inits->malloc;
			callbacks.free = inits->free;
		}

		if (inits->noMemory != NULL)
			callbacks.noMemory = inits->noMemory;

		return enet_initialize();
	}

	void* enet_malloc(size_t size) {
		void* memory = callbacks.malloc(size);

		if (memory == NULL)
			callbacks.noMemory();

		return memory;
	}

	void enet_free(void* memory) {
		callbacks.free(memory);
	}

/*
=======================================================================

	List

=======================================================================
*/

	void enet_list_clear(ENetList* list) {
		list->sentinel.next = &list->sentinel;
		list->sentinel.previous = &list->sentinel;
	}

	ENetListIterator enet_list_insert(ENetListIterator position, void* data) {
		ENetListIterator result = (ENetListIterator)data;
		result->previous = position->previous;
		result->next = position;
		result->previous->next = result;
		position->previous = result;

		return result;
	}

	void* enet_list_remove(ENetListIterator position) {
		position->previous->next = position->next;
		position->next->previous = position->previous;

		return position;
	}

	ENetListIterator enet_list_move(ENetListIterator position, void* dataFirst, void* dataLast) {
		ENetListIterator first = (ENetListIterator)dataFirst;
		ENetListIterator last = (ENetListIterator)dataLast;
		first->previous->next = last->next;
		last->next->previous = first->previous;
		first->previous = position->previous;
		last->next = position;
		first->previous->next = first;
		position->previous = last;

		return first;
	}

	size_t enet_list_size(ENetList* list) {
		size_t size = 0;
		ENetListIterator position;

		for (position = enet_list_begin(list); position != enet_list_end(list); position = enet_list_next(position)) {
			++size;
		}

		return size;
	}

/*
=======================================================================

	Utilities

=======================================================================
*/

	ENetVersion enet_linked_version(void) {
		return ENET_VERSION;
	}

	int enet_array_is_zeroed(const uint8_t* array, int length) {
		size_t i;

		for (i = 0; i < length; i++) {
			if (array[i] != 0)
				return -1;
		}

		return 0;
	}

/*
=======================================================================

	Time

=======================================================================
*/

	#ifdef _WIN32
		static LARGE_INTEGER gettime_offset(void) {
			SYSTEMTIME s;
			FILETIME f;
			LARGE_INTEGER t;
			s.wYear = 1970;
			s.wMonth = 1;
			s.wDay = 1;
			s.wHour = 0;
			s.wMinute = 0;
			s.wSecond = 0;
			s.wMilliseconds = 0;

			SystemTimeToFileTime(&s, &f);

			t.QuadPart = f.dwHighDateTime;
			t.QuadPart <<= 32;
			t.QuadPart |= f.dwLowDateTime;

			return t;
		}

		int clock_gettime(int X, struct timespec* tv) {
			LARGE_INTEGER t;
			FILETIME f;
			double microseconds;

			static LARGE_INTEGER offset;
			static double frequencyToMicroseconds;
			static int initialized = 0;
			static BOOL usePerformanceCounter = 0;

			if (!initialized) {
				LARGE_INTEGER performanceFrequency;
				initialized = 1;
				usePerformanceCounter = QueryPerformanceFrequency(&performanceFrequency);

				if (usePerformanceCounter) {
					QueryPerformanceCounter(&offset);

					frequencyToMicroseconds = (double)performanceFrequency.QuadPart / 1000000.;
				} else {
					offset = gettime_offset();
					frequencyToMicroseconds = 10.;
				}
			}

			if (usePerformanceCounter) {
				QueryPerformanceCounter(&t);
			} else {
				GetSystemTimeAsFileTime(&f);

				t.QuadPart = f.dwHighDateTime;
				t.QuadPart <<= 32;
				t.QuadPart |= f.dwLowDateTime;
			}

			t.QuadPart -= offset.QuadPart;
			microseconds = (double)t.QuadPart / frequencyToMicroseconds;
			t.QuadPart = (LONGLONG)microseconds;
			tv->tv_sec = (long)(t.QuadPart / 1000000);
			tv->tv_nsec = t.QuadPart % 1000000 * 1000;

			return 0;
		}
	#elif __APPLE__ && (__MAC_OS_X_VERSION_MIN_REQUIRED < 101200 || __IPHONE_OS_VERSION_MIN_REQUIRED < 100000) && !defined(CLOCK_MONOTONIC)
		#define CLOCK_MONOTONIC 0

		int clock_gettime(int X, struct timespec* ts) {
			clock_serv_t cclock;
			mach_timespec_t mts;

			host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
			clock_get_time(cclock, &mts);
			mach_port_deallocate(mach_task_self(), cclock);

			ts->tv_sec = mts.tv_sec;
			ts->tv_nsec = mts.tv_nsec;

			return 0;
		}
	#endif

	uint32_t enet_time_get(void) {
		static uint64_t start_time_ns = 0;

		struct timespec ts;

		#ifdef CLOCK_MONOTONIC_RAW
			clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
		#else
			clock_gettime(CLOCK_MONOTONIC, &ts);
		#endif

		static const uint64_t ns_in_s = 1000 * 1000 * 1000;
		static const uint64_t ns_in_ms = 1000 * 1000;

		uint64_t current_time_ns = ts.tv_nsec + (uint64_t)ts.tv_sec * ns_in_s;
		uint64_t offset_ns = ENET_ATOMIC_READ(&start_time_ns);

		if (offset_ns == 0) {
			uint64_t want_value = current_time_ns - 1 * ns_in_ms;
			uint64_t old_value = ENET_ATOMIC_CAS(&start_time_ns, 0, want_value);
			offset_ns = old_value == 0 ? want_value : old_value;
		}

		uint64_t result_in_ns = current_time_ns - offset_ns;

		return (uint32_t)(result_in_ns / ns_in_ms);
	}

/*
=======================================================================

	Checksum

=======================================================================
*/

	typedef uint64_t enet_checksum;

	static const uint64_t crcTable[256] = {
		UINT64_C(0x0000000000000000), UINT64_C(0x7ad870c830358979),
		UINT64_C(0xf5b0e190606b12f2), UINT64_C(0x8f689158505e9b8b),
		UINT64_C(0xc038e5739841b68f), UINT64_C(0xbae095bba8743ff6),
		UINT64_C(0x358804e3f82aa47d), UINT64_C(0x4f50742bc81f2d04),
		UINT64_C(0xab28ecb46814fe75), UINT64_C(0xd1f09c7c5821770c),
		UINT64_C(0x5e980d24087fec87), UINT64_C(0x24407dec384a65fe),
		UINT64_C(0x6b1009c7f05548fa), UINT64_C(0x11c8790fc060c183),
		UINT64_C(0x9ea0e857903e5a08), UINT64_C(0xe478989fa00bd371),
		UINT64_C(0x7d08ff3b88be6f81), UINT64_C(0x07d08ff3b88be6f8),
		UINT64_C(0x88b81eabe8d57d73), UINT64_C(0xf2606e63d8e0f40a),
		UINT64_C(0xbd301a4810ffd90e), UINT64_C(0xc7e86a8020ca5077),
		UINT64_C(0x4880fbd87094cbfc), UINT64_C(0x32588b1040a14285),
		UINT64_C(0xd620138fe0aa91f4), UINT64_C(0xacf86347d09f188d),
		UINT64_C(0x2390f21f80c18306), UINT64_C(0x594882d7b0f40a7f),
		UINT64_C(0x1618f6fc78eb277b), UINT64_C(0x6cc0863448deae02),
		UINT64_C(0xe3a8176c18803589), UINT64_C(0x997067a428b5bcf0),
		UINT64_C(0xfa11fe77117cdf02), UINT64_C(0x80c98ebf2149567b),
		UINT64_C(0x0fa11fe77117cdf0), UINT64_C(0x75796f2f41224489),
		UINT64_C(0x3a291b04893d698d), UINT64_C(0x40f16bccb908e0f4),
		UINT64_C(0xcf99fa94e9567b7f), UINT64_C(0xb5418a5cd963f206),
		UINT64_C(0x513912c379682177), UINT64_C(0x2be1620b495da80e),
		UINT64_C(0xa489f35319033385), UINT64_C(0xde51839b2936bafc),
		UINT64_C(0x9101f7b0e12997f8), UINT64_C(0xebd98778d11c1e81),
		UINT64_C(0x64b116208142850a), UINT64_C(0x1e6966e8b1770c73),
		UINT64_C(0x8719014c99c2b083), UINT64_C(0xfdc17184a9f739fa),
		UINT64_C(0x72a9e0dcf9a9a271), UINT64_C(0x08719014c99c2b08),
		UINT64_C(0x4721e43f0183060c), UINT64_C(0x3df994f731b68f75),
		UINT64_C(0xb29105af61e814fe), UINT64_C(0xc849756751dd9d87),
		UINT64_C(0x2c31edf8f1d64ef6), UINT64_C(0x56e99d30c1e3c78f),
		UINT64_C(0xd9810c6891bd5c04), UINT64_C(0xa3597ca0a188d57d),
		UINT64_C(0xec09088b6997f879), UINT64_C(0x96d1784359a27100),
		UINT64_C(0x19b9e91b09fcea8b), UINT64_C(0x636199d339c963f2),
		UINT64_C(0xdf7adabd7a6e2d6f), UINT64_C(0xa5a2aa754a5ba416),
		UINT64_C(0x2aca3b2d1a053f9d), UINT64_C(0x50124be52a30b6e4),
		UINT64_C(0x1f423fcee22f9be0), UINT64_C(0x659a4f06d21a1299),
		UINT64_C(0xeaf2de5e82448912), UINT64_C(0x902aae96b271006b),
		UINT64_C(0x74523609127ad31a), UINT64_C(0x0e8a46c1224f5a63),
		UINT64_C(0x81e2d7997211c1e8), UINT64_C(0xfb3aa75142244891),
		UINT64_C(0xb46ad37a8a3b6595), UINT64_C(0xceb2a3b2ba0eecec),
		UINT64_C(0x41da32eaea507767), UINT64_C(0x3b024222da65fe1e),
		UINT64_C(0xa2722586f2d042ee), UINT64_C(0xd8aa554ec2e5cb97),
		UINT64_C(0x57c2c41692bb501c), UINT64_C(0x2d1ab4dea28ed965),
		UINT64_C(0x624ac0f56a91f461), UINT64_C(0x1892b03d5aa47d18),
		UINT64_C(0x97fa21650afae693), UINT64_C(0xed2251ad3acf6fea),
		UINT64_C(0x095ac9329ac4bc9b), UINT64_C(0x7382b9faaaf135e2),
		UINT64_C(0xfcea28a2faafae69), UINT64_C(0x8632586aca9a2710),
		UINT64_C(0xc9622c4102850a14), UINT64_C(0xb3ba5c8932b0836d),
		UINT64_C(0x3cd2cdd162ee18e6), UINT64_C(0x460abd1952db919f),
		UINT64_C(0x256b24ca6b12f26d), UINT64_C(0x5fb354025b277b14),
		UINT64_C(0xd0dbc55a0b79e09f), UINT64_C(0xaa03b5923b4c69e6),
		UINT64_C(0xe553c1b9f35344e2), UINT64_C(0x9f8bb171c366cd9b),
		UINT64_C(0x10e3202993385610), UINT64_C(0x6a3b50e1a30ddf69),
		UINT64_C(0x8e43c87e03060c18), UINT64_C(0xf49bb8b633338561),
		UINT64_C(0x7bf329ee636d1eea), UINT64_C(0x012b592653589793),
		UINT64_C(0x4e7b2d0d9b47ba97), UINT64_C(0x34a35dc5ab7233ee),
		UINT64_C(0xbbcbcc9dfb2ca865), UINT64_C(0xc113bc55cb19211c),
		UINT64_C(0x5863dbf1e3ac9dec), UINT64_C(0x22bbab39d3991495),
		UINT64_C(0xadd33a6183c78f1e), UINT64_C(0xd70b4aa9b3f20667),
		UINT64_C(0x985b3e827bed2b63), UINT64_C(0xe2834e4a4bd8a21a),
		UINT64_C(0x6debdf121b863991), UINT64_C(0x1733afda2bb3b0e8),
		UINT64_C(0xf34b37458bb86399), UINT64_C(0x8993478dbb8deae0),
		UINT64_C(0x06fbd6d5ebd3716b), UINT64_C(0x7c23a61ddbe6f812),
		UINT64_C(0x3373d23613f9d516), UINT64_C(0x49aba2fe23cc5c6f),
		UINT64_C(0xc6c333a67392c7e4), UINT64_C(0xbc1b436e43a74e9d),
		UINT64_C(0x95ac9329ac4bc9b5), UINT64_C(0xef74e3e19c7e40cc),
		UINT64_C(0x601c72b9cc20db47), UINT64_C(0x1ac40271fc15523e),
		UINT64_C(0x5594765a340a7f3a), UINT64_C(0x2f4c0692043ff643),
		UINT64_C(0xa02497ca54616dc8), UINT64_C(0xdafce7026454e4b1),
		UINT64_C(0x3e847f9dc45f37c0), UINT64_C(0x445c0f55f46abeb9),
		UINT64_C(0xcb349e0da4342532), UINT64_C(0xb1eceec59401ac4b),
		UINT64_C(0xfebc9aee5c1e814f), UINT64_C(0x8464ea266c2b0836),
		UINT64_C(0x0b0c7b7e3c7593bd), UINT64_C(0x71d40bb60c401ac4),
		UINT64_C(0xe8a46c1224f5a634), UINT64_C(0x927c1cda14c02f4d),
		UINT64_C(0x1d148d82449eb4c6), UINT64_C(0x67ccfd4a74ab3dbf),
		UINT64_C(0x289c8961bcb410bb), UINT64_C(0x5244f9a98c8199c2),
		UINT64_C(0xdd2c68f1dcdf0249), UINT64_C(0xa7f41839ecea8b30),
		UINT64_C(0x438c80a64ce15841), UINT64_C(0x3954f06e7cd4d138),
		UINT64_C(0xb63c61362c8a4ab3), UINT64_C(0xcce411fe1cbfc3ca),
		UINT64_C(0x83b465d5d4a0eece), UINT64_C(0xf96c151de49567b7),
		UINT64_C(0x76048445b4cbfc3c), UINT64_C(0x0cdcf48d84fe7545),
		UINT64_C(0x6fbd6d5ebd3716b7), UINT64_C(0x15651d968d029fce),
		UINT64_C(0x9a0d8ccedd5c0445), UINT64_C(0xe0d5fc06ed698d3c),
		UINT64_C(0xaf85882d2576a038), UINT64_C(0xd55df8e515432941),
		UINT64_C(0x5a3569bd451db2ca), UINT64_C(0x20ed197575283bb3),
		UINT64_C(0xc49581ead523e8c2), UINT64_C(0xbe4df122e51661bb),
		UINT64_C(0x3125607ab548fa30), UINT64_C(0x4bfd10b2857d7349),
		UINT64_C(0x04ad64994d625e4d), UINT64_C(0x7e7514517d57d734),
		UINT64_C(0xf11d85092d094cbf), UINT64_C(0x8bc5f5c11d3cc5c6),
		UINT64_C(0x12b5926535897936), UINT64_C(0x686de2ad05bcf04f),
		UINT64_C(0xe70573f555e26bc4), UINT64_C(0x9ddd033d65d7e2bd),
		UINT64_C(0xd28d7716adc8cfb9), UINT64_C(0xa85507de9dfd46c0),
		UINT64_C(0x273d9686cda3dd4b), UINT64_C(0x5de5e64efd965432),
		UINT64_C(0xb99d7ed15d9d8743), UINT64_C(0xc3450e196da80e3a),
		UINT64_C(0x4c2d9f413df695b1), UINT64_C(0x36f5ef890dc31cc8),
		UINT64_C(0x79a59ba2c5dc31cc), UINT64_C(0x037deb6af5e9b8b5),
		UINT64_C(0x8c157a32a5b7233e), UINT64_C(0xf6cd0afa9582aa47),
		UINT64_C(0x4ad64994d625e4da), UINT64_C(0x300e395ce6106da3),
		UINT64_C(0xbf66a804b64ef628), UINT64_C(0xc5bed8cc867b7f51),
		UINT64_C(0x8aeeace74e645255), UINT64_C(0xf036dc2f7e51db2c),
		UINT64_C(0x7f5e4d772e0f40a7), UINT64_C(0x05863dbf1e3ac9de),
		UINT64_C(0xe1fea520be311aaf), UINT64_C(0x9b26d5e88e0493d6),
		UINT64_C(0x144e44b0de5a085d), UINT64_C(0x6e963478ee6f8124),
		UINT64_C(0x21c640532670ac20), UINT64_C(0x5b1e309b16452559),
		UINT64_C(0xd476a1c3461bbed2), UINT64_C(0xaeaed10b762e37ab),
		UINT64_C(0x37deb6af5e9b8b5b), UINT64_C(0x4d06c6676eae0222),
		UINT64_C(0xc26e573f3ef099a9), UINT64_C(0xb8b627f70ec510d0),
		UINT64_C(0xf7e653dcc6da3dd4), UINT64_C(0x8d3e2314f6efb4ad),
		UINT64_C(0x0256b24ca6b12f26), UINT64_C(0x788ec2849684a65f),
		UINT64_C(0x9cf65a1b368f752e), UINT64_C(0xe62e2ad306bafc57),
		UINT64_C(0x6946bb8b56e467dc), UINT64_C(0x139ecb4366d1eea5),
		UINT64_C(0x5ccebf68aecec3a1), UINT64_C(0x2616cfa09efb4ad8),
		UINT64_C(0xa97e5ef8cea5d153), UINT64_C(0xd3a62e30fe90582a),
		UINT64_C(0xb0c7b7e3c7593bd8), UINT64_C(0xca1fc72bf76cb2a1),
		UINT64_C(0x45775673a732292a), UINT64_C(0x3faf26bb9707a053),
		UINT64_C(0x70ff52905f188d57), UINT64_C(0x0a2722586f2d042e),
		UINT64_C(0x854fb3003f739fa5), UINT64_C(0xff97c3c80f4616dc),
		UINT64_C(0x1bef5b57af4dc5ad), UINT64_C(0x61372b9f9f784cd4),
		UINT64_C(0xee5fbac7cf26d75f), UINT64_C(0x9487ca0fff135e26),
		UINT64_C(0xdbd7be24370c7322), UINT64_C(0xa10fceec0739fa5b),
		UINT64_C(0x2e675fb4576761d0), UINT64_C(0x54bf2f7c6752e8a9),
		UINT64_C(0xcdcf48d84fe75459), UINT64_C(0xb71738107fd2dd20),
		UINT64_C(0x387fa9482f8c46ab), UINT64_C(0x42a7d9801fb9cfd2),
		UINT64_C(0x0df7adabd7a6e2d6), UINT64_C(0x772fdd63e7936baf),
		UINT64_C(0xf8474c3bb7cdf024), UINT64_C(0x829f3cf387f8795d),
		UINT64_C(0x66e7a46c27f3aa2c), UINT64_C(0x1c3fd4a417c62355),
		UINT64_C(0x935745fc4798b8de), UINT64_C(0xe98f353477ad31a7),
		UINT64_C(0xa6df411fbfb21ca3), UINT64_C(0xdc0731d78f8795da),
		UINT64_C(0x536fa08fdfd90e51), UINT64_C(0x29b7d047efec8728),
	};

	uint64_t enet_crc64(const ENetBuffer* buffers, int bufferCount) {
		uint64_t crc = 0xFFFFFFFFFFFFFFFF;

		while (bufferCount-- > 0) {
			const uint8_t* data = (const uint8_t*)buffers->data;
			const uint8_t* dataEnd = &data[buffers->dataLength];

			while (data < dataEnd) {
				crc = (crc >> 8) ^ crcTable[(uint8_t)crc ^* data++];
			}

			++buffers;
		}

		return ENET_HOST_TO_NET_64(~crc);
	}

/*
=======================================================================

	Packet

=======================================================================
*/

	ENetPacket* enet_packet_create(const void* data, size_t dataLength, uint32_t flags) {
		ENetPacket* packet;

		if (flags & ENET_PACKET_FLAG_NO_ALLOCATE) {
			packet = (ENetPacket*)enet_malloc(sizeof(ENetPacket));

			if (packet == NULL)
				return NULL;

			packet->data = (uint8_t*)data;
		} else {
			packet = (ENetPacket*)enet_malloc(sizeof(ENetPacket) + dataLength);

			if (packet == NULL)
				return NULL;

			packet->data = (uint8_t*)packet + sizeof(ENetPacket);

			if (data != NULL)
				memcpy(packet->data, data, dataLength);
		}

		packet->referenceCount = 0;
		packet->flags = flags;
		packet->dataLength = dataLength;
		packet->freeCallback = NULL;
		packet->userData = NULL;

		return packet;
	}

	ENetPacket* enet_packet_create_offset(const void* data, size_t dataLength, size_t dataOffset, uint32_t flags) {
		ENetPacket* packet;

		if (flags & ENET_PACKET_FLAG_NO_ALLOCATE) {
			packet = (ENetPacket*)enet_malloc(sizeof(ENetPacket));

			if (packet == NULL)
				return NULL;

			packet->data = (uint8_t*)data;
		} else {
			packet = (ENetPacket*)enet_malloc(sizeof(ENetPacket) + dataLength - dataOffset);

			if (packet == NULL)
				return NULL;

			packet->data = (uint8_t*)packet + sizeof(ENetPacket);

			if (data != NULL)
				memcpy(packet->data, (char*)data + dataOffset, dataLength - dataOffset);
		}

		packet->referenceCount = 0;
		packet->flags = flags;
		packet->dataLength = dataLength - dataOffset;
		packet->freeCallback = NULL;
		packet->userData = NULL;

		return packet;
	}

	void enet_packet_destroy(ENetPacket* packet) {
		if (packet == NULL)
			return;

		if (packet->freeCallback != NULL)
			(*packet->freeCallback)((void*)packet);

		enet_free(packet);
	}

/*
=======================================================================

	Protocol

=======================================================================
*/

	static size_t commandSizes[ENET_PROTOCOL_COMMAND_COUNT] = {
		0,
		sizeof(ENetProtocolAcknowledge),
		sizeof(ENetProtocolConnect),
		sizeof(ENetProtocolVerifyConnect),
		sizeof(ENetProtocolDisconnect),
		sizeof(ENetProtocolPing),
		sizeof(ENetProtocolSendReliable),
		sizeof(ENetProtocolSendUnreliable),
		sizeof(ENetProtocolSendFragment),
		sizeof(ENetProtocolSendUnsequenced),
		sizeof(ENetProtocolBandwidthLimit),
		sizeof(ENetProtocolThrottleConfigure),
		sizeof(ENetProtocolSendFragment)
	};

	size_t enet_protocol_command_size(uint8_t commandNumber) {
		return commandSizes[commandNumber & ENET_PROTOCOL_COMMAND_MASK];
	}

	static void enet_protocol_change_state(ENetHost* host, ENetPeer* peer, ENetPeerState state) {
		if (state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER)
			enet_peer_on_connect(peer);
		else
			enet_peer_on_disconnect(peer);

		peer->state = state;
	}

	static void enet_protocol_dispatch_state(ENetHost* host, ENetPeer* peer, ENetPeerState state) {
		enet_protocol_change_state(host, peer, state);

		if (!peer->needsDispatch) {
			enet_list_insert(enet_list_end(&host->dispatchQueue), &peer->dispatchList);

			peer->needsDispatch = 1;
		}
	}

	static int enet_protocol_dispatch_incoming_commands(ENetHost* host, ENetEvent* event) {
		while (!enet_list_empty(&host->dispatchQueue)) {
			ENetPeer* peer = (ENetPeer*)enet_list_remove(enet_list_begin(&host->dispatchQueue));
			peer->needsDispatch = 0;

			switch (peer->state) {
				case ENET_PEER_STATE_CONNECTION_PENDING:
				case ENET_PEER_STATE_CONNECTION_SUCCEEDED:
					enet_protocol_change_state(host, peer, ENET_PEER_STATE_CONNECTED);

					event->type = ENET_EVENT_TYPE_CONNECT;
					event->peer = peer;
					event->data = peer->eventData;

					return 1;

				case ENET_PEER_STATE_ZOMBIE:
					host->recalculateBandwidthLimits = 1;
					event->type = ENET_EVENT_TYPE_DISCONNECT;
					event->peer = peer;
					event->data = peer->eventData;

					enet_peer_reset(peer);

					return 1;

				case ENET_PEER_STATE_CONNECTED:
					if (enet_list_empty(&peer->dispatchedCommands))
						continue;

					event->packet = enet_peer_receive(peer, &event->channelID);

					if (event->packet == NULL)
						continue;

					event->type = ENET_EVENT_TYPE_RECEIVE;
					event->peer = peer;

					if (!enet_list_empty(&peer->dispatchedCommands)) {
						peer->needsDispatch = 1;

						enet_list_insert(enet_list_end(&host->dispatchQueue), &peer->dispatchList);
					}

					return 1;

				default:
					break;
			}
		}

		return 0;
	}

	static void enet_protocol_notify_connect(ENetHost* host, ENetPeer* peer, ENetEvent* event) {
		host->recalculateBandwidthLimits = 1;

		if (event != NULL) {
			enet_protocol_change_state(host, peer, ENET_PEER_STATE_CONNECTED);

			peer->totalDataSent = 0;
			peer->totalDataReceived = 0;
			peer->totalPacketsSent = 0;
			peer->totalPacketsLost = 0;
			event->type = ENET_EVENT_TYPE_CONNECT;
			event->peer = peer;
			event->data = peer->eventData;
		} else {
			enet_protocol_dispatch_state(host, peer, peer->state == ENET_PEER_STATE_CONNECTING ? ENET_PEER_STATE_CONNECTION_SUCCEEDED : ENET_PEER_STATE_CONNECTION_PENDING);
		}
	}

	static void enet_protocol_notify_disconnect(ENetHost* host, ENetPeer* peer, ENetEvent* event) {
		if (peer->state >= ENET_PEER_STATE_CONNECTION_PENDING)
			host->recalculateBandwidthLimits = 1;

		if (peer->state != ENET_PEER_STATE_CONNECTING && peer->state < ENET_PEER_STATE_CONNECTION_SUCCEEDED) {
			enet_peer_reset(peer);
		} else if (event != NULL) {
			event->type = ENET_EVENT_TYPE_DISCONNECT;
			event->peer = peer;
			event->data = 0;

			enet_peer_reset(peer);
		} else {
			peer->eventData = 0;

			enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
		}
	}

	static void enet_protocol_notify_disconnect_timeout(ENetHost* host, ENetPeer* peer, ENetEvent* event) {
		if (peer->state >= ENET_PEER_STATE_CONNECTION_PENDING)
			host->recalculateBandwidthLimits = 1;

		if (peer->state != ENET_PEER_STATE_CONNECTING && peer->state < ENET_PEER_STATE_CONNECTION_SUCCEEDED) {
			enet_peer_reset(peer);
		} else if (event != NULL) {
			event->type = ENET_EVENT_TYPE_DISCONNECT_TIMEOUT;
			event->peer = peer;
			event->data = 0;

			enet_peer_reset(peer);
		} else {
			peer->eventData = 0;

			enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
		}
	}

	static void enet_protocol_remove_sent_unreliable_commands(ENetPeer* peer) {
		ENetOutgoingCommand* outgoingCommand;

		if (enet_list_empty(&peer->sentUnreliableCommands))
			return;

		do {
			outgoingCommand = (ENetOutgoingCommand*)enet_list_front(&peer->sentUnreliableCommands);

			enet_list_remove(&outgoingCommand->outgoingCommandList);

			if (outgoingCommand->packet != NULL) {
				--outgoingCommand->packet->referenceCount;

				if (outgoingCommand->packet->referenceCount == 0) {
					outgoingCommand->packet->flags |= ENET_PACKET_FLAG_SENT;

					enet_packet_destroy(outgoingCommand->packet);
				}
			}

			enet_free(outgoingCommand);
		}

		while (!enet_list_empty(&peer->sentUnreliableCommands));

		if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER && enet_list_empty(&peer->outgoingCommands) && enet_list_empty(&peer->sentReliableCommands))
			enet_peer_disconnect(peer, peer->eventData);
	}

	static ENetProtocolCommand enet_protocol_remove_sent_reliable_command(ENetPeer* peer, uint16_t reliableSequenceNumber, uint8_t channelID) {
		ENetOutgoingCommand* outgoingCommand = NULL;
		ENetListIterator currentCommand;
		ENetProtocolCommand commandNumber;

		int wasSent = 1;

		for (currentCommand = enet_list_begin(&peer->sentReliableCommands); currentCommand != enet_list_end(&peer->sentReliableCommands); currentCommand = enet_list_next(currentCommand)) {
			outgoingCommand = (ENetOutgoingCommand*)currentCommand;

			if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber && outgoingCommand->command.header.channelID == channelID)
				break;
		}

		if (currentCommand == enet_list_end(&peer->sentReliableCommands)) {
			for (currentCommand = enet_list_begin(&peer->outgoingCommands); currentCommand != enet_list_end(&peer->outgoingCommands); currentCommand = enet_list_next(currentCommand)) {
				outgoingCommand = (ENetOutgoingCommand*)currentCommand;

				if (outgoingCommand->sendAttempts < 1)
					return ENET_PROTOCOL_COMMAND_NONE;

				if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber && outgoingCommand->command.header.channelID == channelID)
					break;
			}

			if (currentCommand == enet_list_end(&peer->outgoingCommands))
				return ENET_PROTOCOL_COMMAND_NONE;

			wasSent = 0;
		}

		if (outgoingCommand == NULL)
			return ENET_PROTOCOL_COMMAND_NONE;

		if (channelID < peer->channelCount) {
			ENetChannel* channel = &peer->channels[channelID];
			uint16_t reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

			if (channel->reliableWindows[reliableWindow] > 0) {
				--channel->reliableWindows[reliableWindow];

				if (!channel->reliableWindows[reliableWindow])
					channel->usedReliableWindows &= ~(1 << reliableWindow);
			}
		}

		commandNumber = (ENetProtocolCommand)(outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK);

		enet_list_remove(&outgoingCommand->outgoingCommandList);

		if (outgoingCommand->packet != NULL) {
			if (wasSent)
				peer->reliableDataInTransit -= outgoingCommand->fragmentLength;

			--outgoingCommand->packet->referenceCount;

			if (outgoingCommand->packet->referenceCount == 0) {
				outgoingCommand->packet->flags |= ENET_PACKET_FLAG_SENT;

				enet_packet_destroy(outgoingCommand->packet);
			}
		}

		enet_free(outgoingCommand);

		if (enet_list_empty(&peer->sentReliableCommands))
			return commandNumber;

		outgoingCommand = (ENetOutgoingCommand*)enet_list_front(&peer->sentReliableCommands);
		peer->nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;

		return commandNumber;
	}

	static ENetPeer* enet_protocol_handle_connect(ENetHost* host, ENetProtocolHeader* header, ENetProtocol* command) {
		uint8_t incomingSessionID, outgoingSessionID;
		uint32_t mtu, windowSize;
		ENetChannel* channel;
		size_t channelCount, duplicatePeers = 0;
		ENetPeer* currentPeer, *peer = NULL;
		ENetProtocol verifyCommand;
		channelCount = ENET_NET_TO_HOST_32(command->connect.channelCount);

		if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
			return NULL;

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED) {
				if (peer == NULL)
					peer = currentPeer;
			} else if (currentPeer->state != ENET_PEER_STATE_CONNECTING && enet_in6_equal(currentPeer->address.ipv6, host->receivedAddress.ipv6)) {
				if (currentPeer->address.port == host->receivedAddress.port && currentPeer->connectID == command->connect.connectID)
					return NULL;

				++duplicatePeers;
			}
		}

		if (peer == NULL || duplicatePeers >= host->duplicatePeers)
			return NULL;

		if (channelCount > host->channelLimit)
			channelCount = host->channelLimit;

		peer->channels = (ENetChannel*)enet_malloc(channelCount * sizeof(ENetChannel));

		if (peer->channels == NULL)
			return NULL;

		peer->channelCount = channelCount;
		peer->state = ENET_PEER_STATE_ACKNOWLEDGING_CONNECT;
		peer->connectID = command->connect.connectID;
		peer->address = host->receivedAddress;
		peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->connect.outgoingPeerID);
		peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->connect.incomingBandwidth);
		peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->connect.outgoingBandwidth);
		peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->connect.packetThrottleInterval);
		peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleAcceleration);
		peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleDeceleration);
		peer->eventData = ENET_NET_TO_HOST_32(command->connect.data);
		incomingSessionID = command->connect.incomingSessionID == 0xFF ? peer->outgoingSessionID : command->connect.incomingSessionID;
		incomingSessionID = (incomingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);

		if (incomingSessionID == peer->outgoingSessionID)
			incomingSessionID = (incomingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);

		peer->outgoingSessionID = incomingSessionID;
		outgoingSessionID = command->connect.outgoingSessionID == 0xFF ? peer->incomingSessionID : command->connect.outgoingSessionID;
		outgoingSessionID = (outgoingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);

		if (outgoingSessionID == peer->incomingSessionID)
			outgoingSessionID = (outgoingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);

		peer->incomingSessionID = outgoingSessionID;

		for (channel = peer->channels; channel < &peer->channels[channelCount]; ++channel) {
			channel->outgoingReliableSequenceNumber = 0;
			channel->outgoingUnreliableSequenceNumber = 0;
			channel->incomingReliableSequenceNumber = 0;
			channel->incomingUnreliableSequenceNumber = 0;

			enet_list_clear(&channel->incomingReliableCommands);
			enet_list_clear(&channel->incomingUnreliableCommands);

			channel->usedReliableWindows = 0;

			memset(channel->reliableWindows, 0, sizeof(channel->reliableWindows));
		}

		mtu = ENET_NET_TO_HOST_32(command->connect.mtu);

		if (mtu < ENET_PROTOCOL_MINIMUM_MTU)
			mtu = ENET_PROTOCOL_MINIMUM_MTU;
		else if (mtu > ENET_PROTOCOL_MAXIMUM_MTU)
			mtu = ENET_PROTOCOL_MAXIMUM_MTU;

		peer->mtu = mtu;

		if (host->outgoingBandwidth == 0 && peer->incomingBandwidth == 0)
			peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
		else if (host->outgoingBandwidth == 0 || peer->incomingBandwidth == 0)
			peer->windowSize = (ENET_MAX(host->outgoingBandwidth, peer->incomingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else
			peer->windowSize = (ENET_MIN(host->outgoingBandwidth, peer->incomingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

		if (peer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
			peer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else if (peer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
			peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

		if (host->incomingBandwidth == 0)
			windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
		else
			windowSize = (host->incomingBandwidth / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

		if (windowSize > ENET_NET_TO_HOST_32(command->connect.windowSize))
			windowSize = ENET_NET_TO_HOST_32(command->connect.windowSize);

		if (windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
			windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else if (windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
			windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

		verifyCommand.header.command = ENET_PROTOCOL_COMMAND_VERIFY_CONNECT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
		verifyCommand.header.channelID = 0xFF;
		verifyCommand.verifyConnect.outgoingPeerID = ENET_HOST_TO_NET_16(peer->incomingPeerID);
		verifyCommand.verifyConnect.incomingSessionID = incomingSessionID;
		verifyCommand.verifyConnect.outgoingSessionID = outgoingSessionID;
		verifyCommand.verifyConnect.mtu = ENET_HOST_TO_NET_32(peer->mtu);
		verifyCommand.verifyConnect.windowSize = ENET_HOST_TO_NET_32(windowSize);
		verifyCommand.verifyConnect.channelCount = ENET_HOST_TO_NET_32(channelCount);
		verifyCommand.verifyConnect.incomingBandwidth = ENET_HOST_TO_NET_32(host->incomingBandwidth);
		verifyCommand.verifyConnect.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);
		verifyCommand.verifyConnect.packetThrottleInterval = ENET_HOST_TO_NET_32(peer->packetThrottleInterval);
		verifyCommand.verifyConnect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(peer->packetThrottleAcceleration);
		verifyCommand.verifyConnect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(peer->packetThrottleDeceleration);
		verifyCommand.verifyConnect.connectID = peer->connectID;

		enet_peer_queue_outgoing_command(peer, &verifyCommand, NULL, 0, 0);

		return peer;
	}

	static int enet_protocol_handle_send_reliable(ENetHost* host, ENetPeer* peer, const ENetProtocol* command, uint8_t** currentData) {
		size_t dataLength;

		if (command->header.channelID >= peer->channelCount || (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
			return -1;

		dataLength = ENET_NET_TO_HOST_16(command->sendReliable.dataLength);
		*currentData += dataLength;

		if (dataLength > host->maximumPacketSize || *currentData < host->receivedData || *currentData > &host->receivedData[host->receivedDataLength])
			return -1;

		if (enet_peer_queue_incoming_command(peer, command, (const uint8_t*)command + sizeof(ENetProtocolSendReliable), dataLength, ENET_PACKET_FLAG_RELIABLE, 0) == NULL)
			return -1;

		return 0;
	}

	static int enet_protocol_handle_send_unsequenced(ENetHost* host, ENetPeer* peer, const ENetProtocol* command, uint8_t** currentData) {
		uint32_t unsequencedGroup, index;
		size_t dataLength;

		if (command->header.channelID >= peer->channelCount || (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
			return -1;

		dataLength = ENET_NET_TO_HOST_16(command->sendUnsequenced.dataLength);
		*currentData += dataLength;

		if (dataLength > host->maximumPacketSize || *currentData < host->receivedData || *currentData > &host->receivedData[host->receivedDataLength])
			return -1;

		unsequencedGroup = ENET_NET_TO_HOST_16(command->sendUnsequenced.unsequencedGroup);
		index = unsequencedGroup % ENET_PEER_UNSEQUENCED_WINDOW_SIZE;

		if (unsequencedGroup < peer->incomingUnsequencedGroup)
			unsequencedGroup += 0x10000;

		if (unsequencedGroup >= (uint32_t)peer->incomingUnsequencedGroup + ENET_PEER_FREE_UNSEQUENCED_WINDOWS * ENET_PEER_UNSEQUENCED_WINDOW_SIZE)
			return 0;

		unsequencedGroup &= 0xFFFF;

		if (unsequencedGroup - index != peer->incomingUnsequencedGroup) {
			peer->incomingUnsequencedGroup = unsequencedGroup - index;

			memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));
		} else if (peer->unsequencedWindow[index / 32] & (1 << (index % 32))) {
			return 0;
		}

		if (enet_peer_queue_incoming_command(peer, command, (const uint8_t*)command + sizeof(ENetProtocolSendUnsequenced), dataLength, ENET_PACKET_FLAG_UNSEQUENCED, 0) == NULL)
			return -1;

		peer->unsequencedWindow[index / 32] |= 1 << (index % 32);

		return 0;
	}

	static int enet_protocol_handle_send_unreliable(ENetHost* host, ENetPeer* peer, const ENetProtocol* command, uint8_t** currentData) {
		size_t dataLength;

		if (command->header.channelID >= peer->channelCount || (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
			return -1;

		dataLength = ENET_NET_TO_HOST_16(command->sendUnreliable.dataLength);
		*currentData += dataLength;

		if (dataLength > host->maximumPacketSize || *currentData < host->receivedData || *currentData > &host->receivedData[host->receivedDataLength])
			return -1;

		if (enet_peer_queue_incoming_command(peer, command, (const uint8_t*)command + sizeof(ENetProtocolSendUnreliable), dataLength, 0, 0) == NULL)
			return -1;

		return 0;
	}

	static int enet_protocol_handle_send_fragment(ENetHost* host, ENetPeer* peer, const ENetProtocol* command, uint8_t** currentData) {
		uint32_t fragmentNumber, fragmentCount, fragmentOffset, fragmentLength, startSequenceNumber, totalLength;
		ENetChannel* channel;
		uint16_t startWindow, currentWindow;
		ENetListIterator currentCommand;
		ENetIncomingCommand* startCommand = NULL;

		if (command->header.channelID >= peer->channelCount || (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
			return -1;

		fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
		*currentData += fragmentLength;

		if (fragmentLength > host->maximumPacketSize || *currentData < host->receivedData || *currentData > &host->receivedData[host->receivedDataLength])
			return -1;

		channel = &peer->channels[command->header.channelID];
		startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);
		startWindow = startSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
		currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

		if (startSequenceNumber < channel->incomingReliableSequenceNumber)
			startWindow += ENET_PEER_RELIABLE_WINDOWS;

		if (startWindow < currentWindow || startWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
			return 0;

		fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
		fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
		fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
		totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

		if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT || fragmentNumber >= fragmentCount || totalLength > host->maximumPacketSize || fragmentOffset >= totalLength || fragmentLength > totalLength - fragmentOffset)
			return -1;

		for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingReliableCommands)); currentCommand != enet_list_end(&channel->incomingReliableCommands); currentCommand = enet_list_previous(currentCommand)) {
			ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)currentCommand;

			if (startSequenceNumber >= channel->incomingReliableSequenceNumber) {
				if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
					continue;
			} else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
				break;
			}

			if (incomingCommand->reliableSequenceNumber <= startSequenceNumber) {
				if (incomingCommand->reliableSequenceNumber < startSequenceNumber)
					break;

				if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_FRAGMENT || totalLength != incomingCommand->packet->dataLength || fragmentCount != incomingCommand->fragmentCount)
					return -1;

				startCommand = incomingCommand;

				break;
			}
		}

		if (startCommand == NULL) {
			ENetProtocol hostCommand = *command;
			hostCommand.header.reliableSequenceNumber = startSequenceNumber;
			startCommand = enet_peer_queue_incoming_command(peer, &hostCommand, NULL, totalLength, ENET_PACKET_FLAG_RELIABLE, fragmentCount);

			if (startCommand == NULL)
				return -1;
		}

		if ((startCommand->fragments[fragmentNumber / 32] & (1 << (fragmentNumber % 32))) == 0) {
			--startCommand->fragmentsRemaining;
			startCommand->fragments[fragmentNumber / 32] |= (1 << (fragmentNumber % 32));

			if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
				fragmentLength = startCommand->packet->dataLength - fragmentOffset;

			memcpy(startCommand->packet->data + fragmentOffset, (uint8_t*)command + sizeof(ENetProtocolSendFragment), fragmentLength);

			if (startCommand->fragmentsRemaining <= 0)
				enet_peer_dispatch_incoming_reliable_commands(peer, channel, NULL);
		}

		return 0;
	}

	static int enet_protocol_handle_send_unreliable_fragment(ENetHost* host, ENetPeer* peer, const ENetProtocol* command, uint8_t** currentData) {
		uint32_t fragmentNumber, fragmentCount, fragmentOffset, fragmentLength, reliableSequenceNumber, startSequenceNumber, totalLength;
		uint16_t reliableWindow, currentWindow;
		ENetChannel* channel;
		ENetListIterator currentCommand;
		ENetIncomingCommand* startCommand = NULL;

		if (command->header.channelID >= peer->channelCount || (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
			return -1;

		fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
		*currentData += fragmentLength;

		if (fragmentLength > host->maximumPacketSize || *currentData < host->receivedData || *currentData > &host->receivedData[host->receivedDataLength])
			return -1;

		channel = &peer->channels[command->header.channelID];
		reliableSequenceNumber = command->header.reliableSequenceNumber;
		startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);
		reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
		currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

		if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
			reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

		if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
			return 0;

		if (reliableSequenceNumber == channel->incomingReliableSequenceNumber && startSequenceNumber <= channel->incomingUnreliableSequenceNumber)
			return 0;

		fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
		fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
		fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
		totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

		if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT || fragmentNumber >= fragmentCount || totalLength > host->maximumPacketSize || fragmentOffset >= totalLength || fragmentLength > totalLength - fragmentOffset)
			return -1;

			for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingUnreliableCommands)); currentCommand != enet_list_end(&channel->incomingUnreliableCommands); currentCommand = enet_list_previous(currentCommand)) {
				ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)currentCommand;

				if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
					if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
						continue;
				} else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
					break;
				}

				if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
					break;

				if (incomingCommand->reliableSequenceNumber > reliableSequenceNumber)
					continue;

				if (incomingCommand->unreliableSequenceNumber <= startSequenceNumber) {
					if (incomingCommand->unreliableSequenceNumber < startSequenceNumber)
						break;

					if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT || totalLength != incomingCommand->packet->dataLength || fragmentCount != incomingCommand->fragmentCount)
						return -1;

					startCommand = incomingCommand;

					break;
				}
			}

			if (startCommand == NULL) {
				startCommand = enet_peer_queue_incoming_command(peer, command, NULL, totalLength, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENTED, fragmentCount);

				if (startCommand == NULL)
					return -1;
			}

			if ((startCommand->fragments[fragmentNumber / 32] & (1 << (fragmentNumber % 32))) == 0) {
				--startCommand->fragmentsRemaining;
				startCommand->fragments[fragmentNumber / 32] |= (1 << (fragmentNumber % 32));

			if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
				fragmentLength = startCommand->packet->dataLength - fragmentOffset;

			memcpy(startCommand->packet->data + fragmentOffset, (uint8_t*)command + sizeof(ENetProtocolSendFragment), fragmentLength);

			if (startCommand->fragmentsRemaining <= 0)
				enet_peer_dispatch_incoming_unreliable_commands(peer, channel, NULL);
		}

		return 0;
	}

	static int enet_protocol_handle_ping(ENetHost* host, ENetPeer* peer, const ENetProtocol* command) {
		if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
			return -1;

		return 0;
	}

	static int enet_protocol_handle_bandwidth_limit(ENetHost* host, ENetPeer* peer, const ENetProtocol* command) {
		if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
			return -1;

		if (peer->incomingBandwidth != 0)
			--host->bandwidthLimitedPeers;

		peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.incomingBandwidth);
		peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.outgoingBandwidth);

		if (peer->incomingBandwidth != 0)
			++host->bandwidthLimitedPeers;

		if (peer->incomingBandwidth == 0 && host->outgoingBandwidth == 0)
			peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
		else if (peer->incomingBandwidth == 0 || host->outgoingBandwidth == 0)
			peer->windowSize = (ENET_MAX(peer->incomingBandwidth, host->outgoingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else
			peer->windowSize = (ENET_MIN(peer->incomingBandwidth, host->outgoingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

		if (peer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
			peer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else if (peer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
			peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

		return 0;
	}

	static int enet_protocol_handle_throttle_configure(ENetHost* host, ENetPeer* peer, const ENetProtocol* command) {
		if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
			return -1;

		peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleInterval);
		peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleAcceleration);
		peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleDeceleration);

		return 0;
	}

	static int enet_protocol_handle_disconnect(ENetHost* host, ENetPeer* peer, const ENetProtocol* command) {
		if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE || peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT)
			return 0;

		enet_peer_reset_queues(peer);

		if (peer->state == ENET_PEER_STATE_CONNECTION_SUCCEEDED || peer->state == ENET_PEER_STATE_DISCONNECTING || peer->state == ENET_PEER_STATE_CONNECTING) {
			enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
		} else if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) {
			if (peer->state == ENET_PEER_STATE_CONNECTION_PENDING)
				host->recalculateBandwidthLimits = 1;

			enet_peer_reset(peer);
		} else if (command->header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
			enet_protocol_change_state(host, peer, ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT);
		} else {
			enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
		}

		if (peer->state != ENET_PEER_STATE_DISCONNECTED)
			peer->eventData = ENET_NET_TO_HOST_32(command->disconnect.data);

		return 0;
	}

	static int enet_protocol_handle_acknowledge(ENetHost* host, ENetEvent* event, ENetPeer* peer, const ENetProtocol* command) {
		uint32_t roundTripTime, receivedSentTime, receivedReliableSequenceNumber;
		ENetProtocolCommand commandNumber;

		if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE)
			return 0;

		receivedSentTime = ENET_NET_TO_HOST_16(command->acknowledge.receivedSentTime);
		receivedSentTime |= host->serviceTime & 0xFFFF0000;

		if ((receivedSentTime & 0x8000) > (host->serviceTime & 0x8000))
			receivedSentTime -= 0x10000;

		if (ENET_TIME_LESS(host->serviceTime, receivedSentTime))
			return 0;

		roundTripTime = ENET_TIME_DIFFERENCE(host->serviceTime, receivedSentTime);

		if (roundTripTime == 0)
			roundTripTime = 1;

		enet_peer_throttle(peer, roundTripTime);

		if (peer->lastReceiveTime > 0) {
			if (roundTripTime >= peer->roundTripTime) {
				uint32_t diff = roundTripTime - peer->roundTripTime;
				peer->roundTripTimeVariance -= peer->roundTripTimeVariance / 4;
				peer->roundTripTimeVariance += diff / 4;
				peer->roundTripTime += diff / 8;
			} else {
				uint32_t diff = peer->roundTripTime - roundTripTime;

				if (diff <= peer->roundTripTimeVariance) {
					peer->roundTripTimeVariance -= peer->roundTripTimeVariance / 4;
					peer->roundTripTimeVariance += diff / 4;
				} else {
					peer->roundTripTimeVariance -= peer->roundTripTimeVariance / 32;
					peer->roundTripTimeVariance += diff / 32;
				}

				peer->roundTripTime -= diff / 8;
			}
		} else {
			peer->roundTripTime = roundTripTime;
			peer->roundTripTimeVariance = roundTripTime / 2;
		}

		if (peer->roundTripTime < peer->lowestRoundTripTime)
			peer->lowestRoundTripTime = peer->roundTripTime;

		if (peer->roundTripTimeVariance > peer->highestRoundTripTimeVariance)
			peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;

		if (peer->packetThrottleEpoch == 0 || ENET_TIME_DIFFERENCE(host->serviceTime, peer->packetThrottleEpoch) >= peer->packetThrottleInterval) {
			peer->lastRoundTripTime = peer->lowestRoundTripTime;
			peer->lastRoundTripTimeVariance = peer->highestRoundTripTimeVariance;
			peer->lowestRoundTripTime = peer->roundTripTime;
			peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;
			peer->packetThrottleEpoch = host->serviceTime;
		}

		peer->lastReceiveTime = ENET_MAX(host->serviceTime, 1);
		peer->earliestTimeout = 0;

		receivedReliableSequenceNumber = ENET_NET_TO_HOST_16(command->acknowledge.receivedReliableSequenceNumber);
		commandNumber = enet_protocol_remove_sent_reliable_command(peer, receivedReliableSequenceNumber, command->header.channelID);

		switch (peer->state) {
			case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
				if (commandNumber != ENET_PROTOCOL_COMMAND_VERIFY_CONNECT)
					return -1;

				enet_protocol_notify_connect(host, peer, event);

				break;

			case ENET_PEER_STATE_DISCONNECTING:
				if (commandNumber != ENET_PROTOCOL_COMMAND_DISCONNECT)
					return -1;

				enet_protocol_notify_disconnect(host, peer, event);

				break;

			case ENET_PEER_STATE_DISCONNECT_LATER:
				if (enet_list_empty(&peer->outgoingCommands) && enet_list_empty(&peer->sentReliableCommands))
					enet_peer_disconnect(peer, peer->eventData);

				break;

			default:
				break;
		}

		return 0;
	}

	static int enet_protocol_handle_verify_connect(ENetHost* host, ENetEvent* event, ENetPeer* peer, const ENetProtocol* command) {
		uint32_t mtu, windowSize;
		size_t channelCount;

		if (peer->state != ENET_PEER_STATE_CONNECTING)
			return 0;

		channelCount = ENET_NET_TO_HOST_32(command->verifyConnect.channelCount);

		if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT || ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleInterval) != peer->packetThrottleInterval || ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleAcceleration) != peer->packetThrottleAcceleration || ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleDeceleration) != peer->packetThrottleDeceleration || command->verifyConnect.connectID != peer->connectID) {
			peer->eventData = 0;

			enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);

			return -1;
		}

		enet_protocol_remove_sent_reliable_command(peer, 1, 0xFF);

		if (channelCount < peer->channelCount)
			peer->channelCount = channelCount;

		peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->verifyConnect.outgoingPeerID);
		peer->incomingSessionID = command->verifyConnect.incomingSessionID;
		peer->outgoingSessionID = command->verifyConnect.outgoingSessionID;
		mtu = ENET_NET_TO_HOST_32(command->verifyConnect.mtu);

		if (mtu < ENET_PROTOCOL_MINIMUM_MTU)
			mtu = ENET_PROTOCOL_MINIMUM_MTU;
		else if (mtu > ENET_PROTOCOL_MAXIMUM_MTU)
			mtu = ENET_PROTOCOL_MAXIMUM_MTU;

		if (mtu < peer->mtu)
			peer->mtu = mtu;

		windowSize = ENET_NET_TO_HOST_32(command->verifyConnect.windowSize);

		if (windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
			windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

		if (windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
			windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

		if (windowSize < peer->windowSize)
			peer->windowSize = windowSize;

		peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.incomingBandwidth);
		peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.outgoingBandwidth);

		enet_protocol_notify_connect(host, peer, event);

		return 0;
	}

	static int enet_protocol_handle_incoming_commands(ENetHost* host, ENetEvent* event) {
		ENetProtocolHeader* header;
		ENetProtocol* command;
		ENetPeer* peer;
		uint8_t* currentData;
		size_t headerSize;
		uint16_t peerID, flags;
		uint8_t sessionID;

		if (host->receivedDataLength < (size_t)&((ENetProtocolHeader*)0)->sentTime)
			return 0;

		header = (ENetProtocolHeader*)host->receivedData;
		peerID = ENET_NET_TO_HOST_16(header->peerID);
		sessionID = (peerID & ENET_PROTOCOL_HEADER_SESSION_MASK) >> ENET_PROTOCOL_HEADER_SESSION_SHIFT;
		flags = peerID & ENET_PROTOCOL_HEADER_FLAG_MASK;
		peerID &= ~(ENET_PROTOCOL_HEADER_FLAG_MASK | ENET_PROTOCOL_HEADER_SESSION_MASK);
		headerSize = (flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME ? sizeof(ENetProtocolHeader) : (size_t)&((ENetProtocolHeader*)0)->sentTime);

		if (host->checksumCallback != NULL)
			headerSize += sizeof(enet_checksum);

		if (peerID == ENET_PROTOCOL_MAXIMUM_PEER_ID) {
			peer = NULL;
		} else if (peerID >= host->peerCount) {
			return 0;
		} else {
			peer = &host->peers[peerID];

			if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE || ((!enet_in6_equal(host->receivedAddress.ipv6, peer->address.ipv6) || host->receivedAddress.port != peer->address.port) && peer->address.ipv4.ip.s_addr != INADDR_BROADCAST) || (peer->outgoingPeerID < ENET_PROTOCOL_MAXIMUM_PEER_ID && sessionID != peer->incomingSessionID))
				return 0;
		}

		if (host->checksumCallback != NULL) {
			enet_checksum* checksum = (enet_checksum*)&host->receivedData[headerSize - sizeof(enet_checksum)];
			enet_checksum desiredChecksum = *checksum;
			ENetBuffer buffer;
			*checksum = peer != NULL ? peer->connectID : 0;
			buffer.data = host->receivedData;
			buffer.dataLength = host->receivedDataLength;

			if (host->checksumCallback(&buffer, 1) != desiredChecksum)
				return 0;
		}

		if (peer != NULL) {
			peer->address.ipv6 = host->receivedAddress.ipv6;
			peer->address.port = host->receivedAddress.port;
			peer->incomingDataTotal += host->receivedDataLength;
			peer->totalDataReceived += host->receivedDataLength;
		}

		currentData = host->receivedData + headerSize;

		while (currentData < &host->receivedData[host->receivedDataLength]) {
			uint8_t commandNumber;
			size_t commandSize;
			command = (ENetProtocol*)currentData;

			if (currentData + sizeof(ENetProtocolCommandHeader) > &host->receivedData[host->receivedDataLength])
				break;

			commandNumber = command->header.command & ENET_PROTOCOL_COMMAND_MASK;

			if (commandNumber >= ENET_PROTOCOL_COMMAND_COUNT)
				break;

			commandSize = commandSizes[commandNumber];

			if (commandSize == 0 || currentData + commandSize > &host->receivedData[host->receivedDataLength])
				break;

			currentData += commandSize;

			if (peer == NULL && (commandNumber != ENET_PROTOCOL_COMMAND_CONNECT || currentData < &host->receivedData[host->receivedDataLength]))
				break;

			command->header.reliableSequenceNumber = ENET_NET_TO_HOST_16(command->header.reliableSequenceNumber);

			switch (commandNumber) {
				case ENET_PROTOCOL_COMMAND_ACKNOWLEDGE:
					if (enet_protocol_handle_acknowledge(host, event, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_CONNECT:
					if (peer != NULL)
						goto commandError;

					if (host->preventConnections == 0) {
						peer = enet_protocol_handle_connect(host, header, command);

						if (peer == NULL)
							goto commandError;
					}

					break;

				case ENET_PROTOCOL_COMMAND_VERIFY_CONNECT:
					if (enet_protocol_handle_verify_connect(host, event, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_DISCONNECT:
					if (enet_protocol_handle_disconnect(host, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_PING:
					if (enet_protocol_handle_ping(host, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
					if (enet_protocol_handle_send_reliable(host, peer, command, &currentData))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
					if (enet_protocol_handle_send_unreliable(host, peer, command, &currentData))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
					if (enet_protocol_handle_send_unsequenced(host, peer, command, &currentData))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
					if (enet_protocol_handle_send_fragment(host, peer, command, &currentData))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT:
					if (enet_protocol_handle_bandwidth_limit(host, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE:
					if (enet_protocol_handle_throttle_configure(host, peer, command))
						goto commandError;

					break;

				case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
					if (enet_protocol_handle_send_unreliable_fragment(host, peer, command, &currentData))
						goto commandError;

					break;

				default:
					goto commandError;
			}

			if (peer != NULL && (command->header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0) {
				uint16_t sentTime;

				if (!(flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME))
					break;

				sentTime = ENET_NET_TO_HOST_16(header->sentTime);

				switch (peer->state) {
					case ENET_PEER_STATE_DISCONNECTING:
					case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
					case ENET_PEER_STATE_DISCONNECTED:
					case ENET_PEER_STATE_ZOMBIE:
						break;

					case ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT:
						if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_DISCONNECT)
							enet_peer_queue_acknowledgement(peer, command, sentTime);

						break;

					default:
						enet_peer_queue_acknowledgement(peer, command, sentTime);

						break;
				}
			}
		}

		commandError:

		if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
			return 1;

		return 0;
	}

	static int enet_protocol_receive_incoming_commands(ENetHost* host, ENetEvent* event) {
		int packets;

		for (packets = 0; packets < 256; ++packets) {
			int receivedLength;
			ENetBuffer buffer;
			buffer.data = host->packetData[0];
			buffer.dataLength = host->mtu;
			receivedLength = enet_socket_receive(host->socket, &host->receivedAddress, &buffer, 1);

			if (receivedLength == -2)
				continue;

			if (receivedLength < 0)
				return -1;

			if (receivedLength == 0)
				return 0;

			host->receivedData = host->packetData[0];
			host->receivedDataLength = receivedLength;
			host->totalReceivedData += receivedLength;
			host->totalReceivedPackets++;

			if (host->interceptCallback != NULL) {
				switch (host->interceptCallback(event, &host->receivedAddress, host->receivedData, host->receivedDataLength)) {
					case 1:
						if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
							return 1;

						continue;

					case -1:
						return -1;

					default:
						break;
				}
			}

			switch (enet_protocol_handle_incoming_commands(host, event)) {
				case 1:
					return 1;

				case -1:
					return -1;

				default:
					break;
			}
		}

		return 0;
	}

	static void enet_protocol_send_acknowledgements(ENetHost* host, ENetPeer* peer) {
		ENetProtocol* command = &host->commands[host->commandCount];
		ENetBuffer* buffer = &host->buffers[host->bufferCount];
		ENetAcknowledgement* acknowledgement;
		ENetListIterator currentAcknowledgement;
		uint16_t reliableSequenceNumber;
		currentAcknowledgement = enet_list_begin(&peer->acknowledgements);

		while (currentAcknowledgement != enet_list_end(&peer->acknowledgements)) {
			if (command >= &host->commands[sizeof(host->commands) / sizeof(ENetProtocol)] || buffer >= &host->buffers[sizeof(host->buffers) / sizeof(ENetBuffer)] || peer->mtu - host->packetSize < sizeof(ENetProtocolAcknowledge)) {
				host->continueSending = 1;

				break;
			}

			acknowledgement = (ENetAcknowledgement*)currentAcknowledgement;
			currentAcknowledgement = enet_list_next(currentAcknowledgement);
			buffer->data = command;
			buffer->dataLength = sizeof(ENetProtocolAcknowledge);
			host->packetSize += buffer->dataLength;
			reliableSequenceNumber = ENET_HOST_TO_NET_16(acknowledgement->command.header.reliableSequenceNumber);
			command->header.command = ENET_PROTOCOL_COMMAND_ACKNOWLEDGE;
			command->header.channelID = acknowledgement->command.header.channelID;
			command->header.reliableSequenceNumber = reliableSequenceNumber;
			command->acknowledge.receivedReliableSequenceNumber = reliableSequenceNumber;
			command->acknowledge.receivedSentTime = ENET_HOST_TO_NET_16(acknowledgement->sentTime);

			if ((acknowledgement->command.header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_DISCONNECT)
				enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);

			enet_list_remove(&acknowledgement->acknowledgementList);
			enet_free(acknowledgement);

			++command;
			++buffer;
		}

		host->commandCount = command - host->commands;
		host->bufferCount = buffer - host->buffers;
	}

	static int enet_protocol_check_timeouts(ENetHost* host, ENetPeer* peer, ENetEvent* event) {
		ENetOutgoingCommand* outgoingCommand;
		ENetListIterator currentCommand, insertPosition;
		currentCommand = enet_list_begin(&peer->sentReliableCommands);
		insertPosition = enet_list_begin(&peer->outgoingCommands);

		while (currentCommand != enet_list_end(&peer->sentReliableCommands)) {
			outgoingCommand = (ENetOutgoingCommand*)currentCommand;
			currentCommand = enet_list_next(currentCommand);

			if (ENET_TIME_DIFFERENCE(host->serviceTime, outgoingCommand->sentTime) < outgoingCommand->roundTripTimeout)
				continue;

			if (peer->earliestTimeout == 0 || ENET_TIME_LESS(outgoingCommand->sentTime, peer->earliestTimeout))
				peer->earliestTimeout = outgoingCommand->sentTime;

			if (peer->earliestTimeout != 0 && (ENET_TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMaximum || (outgoingCommand->roundTripTimeout >= outgoingCommand->roundTripTimeoutLimit && ENET_TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMinimum))) {
				enet_protocol_notify_disconnect_timeout(host, peer, event);

				return 1;
			}

			if (outgoingCommand->packet != NULL)
				peer->reliableDataInTransit -= outgoingCommand->fragmentLength;

			++peer->totalPacketsLost;
			outgoingCommand->roundTripTimeout = peer->roundTripTime + 4 * peer->roundTripTimeVariance;
			outgoingCommand->roundTripTimeoutLimit = peer->timeoutLimit * outgoingCommand->roundTripTimeout;

			enet_list_insert(insertPosition, enet_list_remove(&outgoingCommand->outgoingCommandList));

			if (currentCommand == enet_list_begin(&peer->sentReliableCommands) && !enet_list_empty(&peer->sentReliableCommands)) {
				outgoingCommand = (ENetOutgoingCommand*)currentCommand;
				peer->nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;
			}
		}

		return 0;
	}

	static int enet_protocol_check_outgoing_commands(ENetHost* host, ENetPeer* peer) {
		ENetProtocol* command = &host->commands[host->commandCount];
		ENetBuffer* buffer = &host->buffers[host->bufferCount];
		ENetOutgoingCommand* outgoingCommand;
		ENetListIterator currentCommand;
		ENetChannel* channel = NULL;
		uint16_t reliableWindow = 0;
		size_t commandSize = 0;
		int windowExceeded = 0, windowWrap = 0, canPing = 1;
		currentCommand = enet_list_begin(&peer->outgoingCommands);

		while (currentCommand != enet_list_end(&peer->outgoingCommands)) {
			outgoingCommand = (ENetOutgoingCommand*)currentCommand;

			if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
				channel = outgoingCommand->command.header.channelID < peer->channelCount ? &peer->channels[outgoingCommand->command.header.channelID] : NULL;
				reliableWindow = outgoingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

				if (channel != NULL) {
					if (!windowWrap && outgoingCommand->sendAttempts < 1 && !(outgoingCommand->reliableSequenceNumber % ENET_PEER_RELIABLE_WINDOW_SIZE) && (channel->reliableWindows[(reliableWindow + ENET_PEER_RELIABLE_WINDOWS - 1) % ENET_PEER_RELIABLE_WINDOWS] >= ENET_PEER_RELIABLE_WINDOW_SIZE || channel->usedReliableWindows & ((((1 << (ENET_PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) << reliableWindow) | (((1 << (ENET_PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) >> (ENET_PEER_RELIABLE_WINDOWS - reliableWindow)))))
						windowWrap = 1;

					if (windowWrap) {
						currentCommand = enet_list_next(currentCommand);

						continue;
					}
				}

				if (outgoingCommand->packet != NULL) {
					if (!windowExceeded) {
						uint32_t windowSize = (peer->packetThrottle * peer->windowSize) / ENET_PEER_PACKET_THROTTLE_SCALE;

						if (peer->reliableDataInTransit + outgoingCommand->fragmentLength > ENET_MAX(windowSize, peer->mtu))
							windowExceeded = 1;
					}

					if (windowExceeded) {
						currentCommand = enet_list_next(currentCommand);

						continue;
					}
				}

				canPing = 0;
			}

			commandSize = commandSizes[outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK];

			if (command >= &host->commands[sizeof(host->commands) / sizeof(ENetProtocol)] || buffer + 1 >= &host->buffers[sizeof(host->buffers) / sizeof(ENetBuffer)] || peer->mtu - host->packetSize < commandSize || (outgoingCommand->packet != NULL && (uint16_t)(peer->mtu - host->packetSize) < (uint16_t)(commandSize + outgoingCommand->fragmentLength))) {
				host->continueSending = 1;

				break;
			}

			currentCommand = enet_list_next(currentCommand);

			if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
				if (channel != NULL && outgoingCommand->sendAttempts < 1) {
					channel->usedReliableWindows |= 1 << reliableWindow;
					++channel->reliableWindows[reliableWindow];
				}

				++outgoingCommand->sendAttempts;

				if (outgoingCommand->roundTripTimeout == 0) {
					outgoingCommand->roundTripTimeout = peer->roundTripTime + 4 * peer->roundTripTimeVariance;
					outgoingCommand->roundTripTimeoutLimit = peer->timeoutLimit * outgoingCommand->roundTripTimeout;
				}

				if (enet_list_empty(&peer->sentReliableCommands))
					peer->nextTimeout = host->serviceTime + outgoingCommand->roundTripTimeout;

				enet_list_insert(enet_list_end(&peer->sentReliableCommands),
				enet_list_remove(&outgoingCommand->outgoingCommandList));

				outgoingCommand->sentTime = host->serviceTime;
				host->headerFlags |= ENET_PROTOCOL_HEADER_FLAG_SENT_TIME;
				peer->reliableDataInTransit += outgoingCommand->fragmentLength;
			} else {
				if (outgoingCommand->packet != NULL && outgoingCommand->fragmentOffset == 0 && !(outgoingCommand->packet->flags & (ENET_PACKET_FLAG_UNTHROTTLED))) {
					peer->packetThrottleCounter += ENET_PEER_PACKET_THROTTLE_COUNTER;
					peer->packetThrottleCounter %= ENET_PEER_PACKET_THROTTLE_SCALE;

					if (peer->packetThrottleCounter > peer->packetThrottle) {
						uint16_t reliableSequenceNumber = outgoingCommand->reliableSequenceNumber,

						unreliableSequenceNumber = outgoingCommand->unreliableSequenceNumber;

						for (;;) {
							--outgoingCommand->packet->referenceCount;

							if (outgoingCommand->packet->referenceCount == 0)
								enet_packet_destroy(outgoingCommand->packet);

							enet_list_remove(&outgoingCommand->outgoingCommandList);
							enet_free(outgoingCommand);

							if (currentCommand == enet_list_end(&peer->outgoingCommands))
								break;

							outgoingCommand = (ENetOutgoingCommand*)currentCommand;

							if (outgoingCommand->reliableSequenceNumber != reliableSequenceNumber || outgoingCommand->unreliableSequenceNumber != unreliableSequenceNumber)
								break;

							currentCommand = enet_list_next(currentCommand);
						}

						continue;
					}
				}

				enet_list_remove(&outgoingCommand->outgoingCommandList);

				if (outgoingCommand->packet != NULL)
					enet_list_insert(enet_list_end(&peer->sentUnreliableCommands), outgoingCommand);
			}

			buffer->data = command;
			buffer->dataLength = commandSize;
			host->packetSize += buffer->dataLength;
			*command = outgoingCommand->command;

			if (outgoingCommand->packet != NULL) {
				++buffer;
				buffer->data = outgoingCommand->packet->data + outgoingCommand->fragmentOffset;
				buffer->dataLength = outgoingCommand->fragmentLength;
				host->packetSize += outgoingCommand->fragmentLength;
			} else if (!(outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)) {
				enet_free(outgoingCommand);
			}

			++peer->totalPacketsSent;
			++command;
			++buffer;
		}

		host->commandCount = command - host->commands;
		host->bufferCount = buffer - host->buffers;

		if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER && enet_list_empty(&peer->outgoingCommands) && enet_list_empty(&peer->sentReliableCommands) && enet_list_empty(&peer->sentUnreliableCommands))
			enet_peer_disconnect(peer, peer->eventData);

		return canPing;
	}

	static int enet_protocol_send_outgoing_commands(ENetHost* host, ENetEvent* event, int checkForTimeouts) {
		uint8_t headerData[sizeof(ENetProtocolHeader) + sizeof(enet_checksum)];
		ENetProtocolHeader* header = (ENetProtocolHeader*)headerData;
		ENetPeer* currentPeer;
		int sentLength;
		host->continueSending = 1;

		while (host->continueSending) {
			for (host->continueSending = 0, currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
				if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED || currentPeer->state == ENET_PEER_STATE_ZOMBIE)
					continue;

				host->headerFlags = 0;
				host->commandCount = 0;
				host->bufferCount = 1;
				host->packetSize = sizeof(ENetProtocolHeader);

				if (host->checksumCallback != NULL)
					host->packetSize += sizeof(enet_checksum);

				if (!enet_list_empty(&currentPeer->acknowledgements))
					enet_protocol_send_acknowledgements(host, currentPeer);

				if (checkForTimeouts != 0 && !enet_list_empty(&currentPeer->sentReliableCommands) && ENET_TIME_GREATER_EQUAL(host->serviceTime, currentPeer->nextTimeout) && enet_protocol_check_timeouts(host, currentPeer, event) == 1) {
					if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
						return 1;
					else
						continue;
				}

				if ((enet_list_empty(&currentPeer->outgoingCommands) || enet_protocol_check_outgoing_commands(host, currentPeer)) && enet_list_empty(&currentPeer->sentReliableCommands) && ENET_TIME_DIFFERENCE(host->serviceTime, currentPeer->lastReceiveTime) >= currentPeer->pingInterval && currentPeer->mtu - host->packetSize >= sizeof(ENetProtocolPing)) {
					enet_peer_ping(currentPeer);
					enet_protocol_check_outgoing_commands(host, currentPeer);
				}

				if (host->commandCount == 0)
					continue;

				host->buffers->data = headerData;

				if (host->headerFlags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME) {
					header->sentTime = ENET_HOST_TO_NET_16(host->serviceTime & 0xFFFF);
					host->buffers->dataLength = sizeof(ENetProtocolHeader);
				} else {
					host->buffers->dataLength = (size_t)&((ENetProtocolHeader*)0)->sentTime;
				}

				if (currentPeer->outgoingPeerID < ENET_PROTOCOL_MAXIMUM_PEER_ID)
					host->headerFlags |= currentPeer->outgoingSessionID << ENET_PROTOCOL_HEADER_SESSION_SHIFT;

				header->peerID = ENET_HOST_TO_NET_16(currentPeer->outgoingPeerID | host->headerFlags);

				if (host->checksumCallback != NULL) {
					enet_checksum* checksum = (enet_checksum*)&headerData[host->buffers->dataLength];
					*checksum = currentPeer->outgoingPeerID < ENET_PROTOCOL_MAXIMUM_PEER_ID ? currentPeer->connectID : 0;
					host->buffers->dataLength += sizeof(enet_checksum);
					*checksum = host->checksumCallback(host->buffers, host->bufferCount);
				}

				currentPeer->lastSendTime = host->serviceTime;
				sentLength = enet_socket_send(host->socket, &currentPeer->address, host->buffers, host->bufferCount);

				enet_protocol_remove_sent_unreliable_commands(currentPeer);

				if (sentLength < 0)
					return -1;

				host->totalSentData += sentLength;
				currentPeer->totalDataSent += sentLength;
				host->totalSentPackets++;
			}
		}

		return 0;
	}

	void enet_host_flush(ENetHost* host) {
		host->serviceTime = enet_time_get();

		enet_protocol_send_outgoing_commands(host, NULL, 0);
	}

	int enet_host_check_events(ENetHost* host, ENetEvent* event) {
		if (event == NULL)
			return -1;

		event->type = ENET_EVENT_TYPE_NONE;
		event->peer = NULL;
		event->packet = NULL;

		return enet_protocol_dispatch_incoming_commands(host, event);
	}

	int enet_host_service(ENetHost* host, ENetEvent* event, uint32_t timeout) {
		uint32_t waitCondition;

		if (event != NULL) {
			event->type = ENET_EVENT_TYPE_NONE;
			event->peer = NULL;
			event->packet = NULL;

			switch (enet_protocol_dispatch_incoming_commands(host, event)) {
				case 1:
					return 1;

				case -1:
					#ifdef ENET_DEBUG
						perror("Error dispatching incoming packets");
					#endif

					return -1;

				default:
					break;
			}
		}

		host->serviceTime = enet_time_get();
		timeout += host->serviceTime;

		do {
			if (ENET_TIME_DIFFERENCE(host->serviceTime, host->bandwidthThrottleEpoch) >= ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
				enet_host_bandwidth_throttle(host);

			switch (enet_protocol_send_outgoing_commands(host, event, 1)) {
				case 1:
					return 1;

				case -1:
					#ifdef ENET_DEBUG
						perror("Error sending outgoing packets");
					#endif

					return -1;

				default:
					break;
			}

			switch (enet_protocol_receive_incoming_commands(host, event)) {
				case 1:
					return 1;

				case -1:
					#ifdef ENET_DEBUG
						perror("Error receiving incoming packets");
					#endif

					return -1;

				default:
					break;
			}

			switch (enet_protocol_send_outgoing_commands(host, event, 1)) {
				case 1:
					return 1;

				case -1:
					#ifdef ENET_DEBUG
						perror("Error sending outgoing packets");
					#endif

					return -1;

				default:
					break;
			}

			if (event != NULL) {
				switch (enet_protocol_dispatch_incoming_commands(host, event)) {
					case 1:
						return 1;

					case -1:
						#ifdef ENET_DEBUG
							perror("Error dispatching incoming packets");
						#endif

						return -1;

					default:
						break;
				}
			}

			if (ENET_TIME_GREATER_EQUAL(host->serviceTime, timeout))
				return 0;

			do {
				host->serviceTime = enet_time_get();

				if (ENET_TIME_GREATER_EQUAL(host->serviceTime, timeout))
					return 0;

				waitCondition = ENET_SOCKET_WAIT_RECEIVE | ENET_SOCKET_WAIT_INTERRUPT;

				if (enet_socket_wait(host->socket, &waitCondition, ENET_TIME_DIFFERENCE(timeout, host->serviceTime)) != 0)
					return -1;
			}

			while (waitCondition & ENET_SOCKET_WAIT_INTERRUPT);

			host->serviceTime = enet_time_get();
		}

		while (waitCondition & ENET_SOCKET_WAIT_RECEIVE);

		return 0;
	}

/*
=======================================================================

	Peer

=======================================================================
*/

	void enet_peer_throttle_configure(ENetPeer* peer, uint32_t interval, uint32_t acceleration, uint32_t deceleration, uint32_t threshold) {
		ENetProtocol command;
		peer->packetThrottleThreshold = threshold;
		peer->packetThrottleInterval = interval;
		peer->packetThrottleAcceleration = acceleration;
		peer->packetThrottleDeceleration = deceleration;
		command.header.command = ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
		command.header.channelID = 0xFF;
		command.throttleConfigure.packetThrottleInterval = ENET_HOST_TO_NET_32(interval);
		command.throttleConfigure.packetThrottleAcceleration = ENET_HOST_TO_NET_32(acceleration);
		command.throttleConfigure.packetThrottleDeceleration = ENET_HOST_TO_NET_32(deceleration);

		enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
	}

	int enet_peer_throttle(ENetPeer* peer, uint32_t rtt) {
		if (peer->lastRoundTripTime <= peer->lastRoundTripTimeVariance) {
			peer->packetThrottle = peer->packetThrottleLimit;
		} else if (rtt < peer->lastRoundTripTime + (peer->lastRoundTripTimeVariance + 1) / 2) {
			peer->packetThrottle += peer->packetThrottleAcceleration;

			if (peer->packetThrottle > peer->packetThrottleLimit)
				peer->packetThrottle = peer->packetThrottleLimit;

			return 1;
		} else if (rtt > peer->lastRoundTripTime + peer->packetThrottleThreshold + 2 * peer->lastRoundTripTimeVariance) {
			if (peer->packetThrottle > peer->packetThrottleDeceleration)
				peer->packetThrottle -= peer->packetThrottleDeceleration;
			else
				peer->packetThrottle = 0;

			return -1;
		}

		return 0;
	}

	int enet_peer_send(ENetPeer* peer, uint8_t channelID, ENetPacket* packet) {
		ENetChannel* channel;
		ENetProtocol command;
		size_t fragmentLength;

		if (peer->state != ENET_PEER_STATE_CONNECTED || channelID >= peer->channelCount || packet->dataLength > peer->host->maximumPacketSize)
			return -1;

 		channel = &peer->channels[channelID];
		fragmentLength = peer->mtu - sizeof(ENetProtocolHeader) - sizeof(ENetProtocolSendFragment) - sizeof(ENetProtocolAcknowledge);

		if (peer->host->checksumCallback != NULL)
			fragmentLength -= sizeof(enet_checksum);

		if (packet->dataLength > fragmentLength) {
			uint32_t fragmentCount = (packet->dataLength + fragmentLength - 1) / fragmentLength, fragmentNumber, fragmentOffset;
			uint8_t commandNumber;
			uint16_t startSequenceNumber;
			ENetList fragments;
			ENetOutgoingCommand* fragment;

			if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
				return -1;

			if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENTED)) == ENET_PACKET_FLAG_UNRELIABLE_FRAGMENTED && channel->outgoingUnreliableSequenceNumber < 0xFFFF) {
				commandNumber = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
				startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingUnreliableSequenceNumber + 1);
			} else {
				commandNumber = ENET_PROTOCOL_COMMAND_SEND_FRAGMENT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
				startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingReliableSequenceNumber + 1);
			}

			enet_list_clear(&fragments);

			for (fragmentNumber = 0, fragmentOffset = 0; fragmentOffset < packet->dataLength; ++fragmentNumber, fragmentOffset += fragmentLength) {
				if (packet->dataLength - fragmentOffset < fragmentLength)
					fragmentLength = packet->dataLength - fragmentOffset;

				fragment = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));

				if (fragment == NULL) {
					while (!enet_list_empty(&fragments)) {
						fragment = (ENetOutgoingCommand*)enet_list_remove(enet_list_begin(&fragments));

						enet_free(fragment);
					}

					return -1;
				}

				fragment->fragmentOffset = fragmentOffset;
				fragment->fragmentLength = fragmentLength;
				fragment->packet = packet;
				fragment->command.header.command = commandNumber;
				fragment->command.header.channelID = channelID;
				fragment->command.sendFragment.startSequenceNumber = startSequenceNumber;
				fragment->command.sendFragment.dataLength = ENET_HOST_TO_NET_16(fragmentLength);
				fragment->command.sendFragment.fragmentCount = ENET_HOST_TO_NET_32(fragmentCount);
				fragment->command.sendFragment.fragmentNumber = ENET_HOST_TO_NET_32(fragmentNumber);
				fragment->command.sendFragment.totalLength = ENET_HOST_TO_NET_32(packet->dataLength);
				fragment->command.sendFragment.fragmentOffset = ENET_NET_TO_HOST_32(fragmentOffset);

				enet_list_insert(enet_list_end(&fragments), fragment);
			}

			packet->referenceCount += fragmentNumber;

			while (!enet_list_empty(&fragments)) {
				fragment = (ENetOutgoingCommand*)enet_list_remove(enet_list_begin(&fragments));

				enet_peer_setup_outgoing_command(peer, fragment);
			}

			return 0;
		}

		command.header.channelID = channelID;

		if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED)) == ENET_PACKET_FLAG_UNSEQUENCED) {
			command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
			command.sendUnsequenced.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
		} else if (packet->flags & ENET_PACKET_FLAG_RELIABLE || channel->outgoingUnreliableSequenceNumber >= 0xFFFF) {
			command.header.command = ENET_PROTOCOL_COMMAND_SEND_RELIABLE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
			command.sendReliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
		} else {
			command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE;
			command.sendUnreliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
		}

		if (enet_peer_queue_outgoing_command(peer, &command, packet, 0, packet->dataLength) == NULL)
			return -1;

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			enet_host_flush(peer->host);

		return 0;
	}

	ENetPacket* enet_peer_receive(ENetPeer* peer, uint8_t* channelID) {
		ENetIncomingCommand* incomingCommand;
		ENetPacket* packet;

		if (enet_list_empty(&peer->dispatchedCommands))
			return NULL;

		incomingCommand = (ENetIncomingCommand*)enet_list_remove(enet_list_begin(&peer->dispatchedCommands));

		if (channelID != NULL)
			*channelID = incomingCommand->command.header.channelID;

		packet = incomingCommand->packet;
		--packet->referenceCount;

		if (incomingCommand->fragments != NULL)
			enet_free(incomingCommand->fragments);

		enet_free(incomingCommand);

		peer->totalWaitingData -= packet->dataLength;

		return packet;
	}

	static void enet_peer_reset_outgoing_commands(ENetList* queue) {
		ENetOutgoingCommand* outgoingCommand;

		while (!enet_list_empty(queue)) {
			outgoingCommand = (ENetOutgoingCommand*)enet_list_remove(enet_list_begin(queue));

			if (outgoingCommand->packet != NULL) {
				--outgoingCommand->packet->referenceCount;

				if (outgoingCommand->packet->referenceCount == 0)
					enet_packet_destroy(outgoingCommand->packet);
			}

			enet_free(outgoingCommand);
		}
	}

	static void enet_peer_remove_incoming_commands(ENetList* queue, ENetListIterator startCommand, ENetListIterator endCommand, ENetIncomingCommand* excludeCommand) {
		ENetListIterator currentCommand;

		for (currentCommand = startCommand; currentCommand != endCommand;) {
			ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)currentCommand;
			currentCommand = enet_list_next(currentCommand);

			if (incomingCommand == excludeCommand)
				continue;

			enet_list_remove(&incomingCommand->incomingCommandList);

			if (incomingCommand->packet != NULL) {
				--incomingCommand->packet->referenceCount;

				if (incomingCommand->packet->referenceCount == 0)
					enet_packet_destroy(incomingCommand->packet);
			}

			if (incomingCommand->fragments != NULL)
				enet_free(incomingCommand->fragments);

			enet_free(incomingCommand);
		}
	}

	static void enet_peer_reset_incoming_commands(ENetList* queue) {
		enet_peer_remove_incoming_commands(queue, enet_list_begin(queue), enet_list_end(queue), NULL);
	}

	void enet_peer_reset_queues(ENetPeer* peer) {
		ENetChannel* channel;

		if (peer->needsDispatch) {
			enet_list_remove(&peer->dispatchList);

			peer->needsDispatch = 0;
		}

		while (!enet_list_empty(&peer->acknowledgements)) {
			enet_free(enet_list_remove(enet_list_begin(&peer->acknowledgements)));
		}

		enet_peer_reset_outgoing_commands(&peer->sentReliableCommands);
		enet_peer_reset_outgoing_commands(&peer->sentUnreliableCommands);
		enet_peer_reset_outgoing_commands(&peer->outgoingCommands);
		enet_peer_reset_incoming_commands(&peer->dispatchedCommands);

		if (peer->channels != NULL && peer->channelCount > 0) {
			for (channel = peer->channels; channel < &peer->channels[peer->channelCount]; ++channel) {
				enet_peer_reset_incoming_commands(&channel->incomingReliableCommands);
				enet_peer_reset_incoming_commands(&channel->incomingUnreliableCommands);
			}

			enet_free(peer->channels);
		}

		peer->channels = NULL;
		peer->channelCount = 0;
	}

	void enet_peer_on_connect(ENetPeer* peer) {
		if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) {
			if (peer->incomingBandwidth != 0)
				++peer->host->bandwidthLimitedPeers;

			++peer->host->connectedPeers;
		}
	}

	void enet_peer_on_disconnect(ENetPeer* peer) {
		if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER) {
			if (peer->incomingBandwidth != 0)
				--peer->host->bandwidthLimitedPeers;

			--peer->host->connectedPeers;
		}
	}

	void enet_peer_reset(ENetPeer* peer) {
		enet_peer_on_disconnect(peer);

		peer->outgoingPeerID = ENET_PROTOCOL_MAXIMUM_PEER_ID;
		peer->state = ENET_PEER_STATE_DISCONNECTED;
		peer->incomingBandwidth = 0;
		peer->outgoingBandwidth = 0;
		peer->incomingBandwidthThrottleEpoch = 0;
		peer->outgoingBandwidthThrottleEpoch = 0;
		peer->incomingDataTotal = 0;
		peer->totalDataReceived = 0;
		peer->outgoingDataTotal = 0;
		peer->totalDataSent = 0;
		peer->lastSendTime = 0;
		peer->lastReceiveTime = 0;
		peer->nextTimeout = 0;
		peer->earliestTimeout = 0;
		peer->totalPacketsSent = 0;
		peer->totalPacketsLost = 0;
		peer->packetThrottle = ENET_PEER_DEFAULT_PACKET_THROTTLE;
		peer->packetThrottleThreshold = ENET_PEER_PACKET_THROTTLE_THRESHOLD;
		peer->packetThrottleLimit = ENET_PEER_PACKET_THROTTLE_SCALE;
		peer->packetThrottleCounter = 0;
		peer->packetThrottleEpoch = 0;
		peer->packetThrottleAcceleration = ENET_PEER_PACKET_THROTTLE_ACCELERATION;
		peer->packetThrottleDeceleration = ENET_PEER_PACKET_THROTTLE_DECELERATION;
		peer->packetThrottleInterval = ENET_PEER_PACKET_THROTTLE_INTERVAL;
		peer->pingInterval = ENET_PEER_PING_INTERVAL;
		peer->timeoutLimit = ENET_PEER_TIMEOUT_LIMIT;
		peer->timeoutMinimum = ENET_PEER_TIMEOUT_MINIMUM;
		peer->timeoutMaximum = ENET_PEER_TIMEOUT_MAXIMUM;
		peer->lastRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
		peer->lowestRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
		peer->lastRoundTripTimeVariance = 0;
		peer->highestRoundTripTimeVariance = 0;
		peer->roundTripTime = 1;
		peer->roundTripTimeVariance = 0;
		peer->mtu = peer->host->mtu;
		peer->reliableDataInTransit = 0;
		peer->outgoingReliableSequenceNumber = 0;
		peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
		peer->incomingUnsequencedGroup = 0;
		peer->outgoingUnsequencedGroup = 0;
		peer->eventData = 0;
		peer->totalWaitingData = 0;

		memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));

		enet_peer_reset_queues(peer);
	}

	void enet_peer_ping(ENetPeer* peer) {
		ENetProtocol command;

		if (peer->state != ENET_PEER_STATE_CONNECTED)
			return;

		command.header.command = ENET_PROTOCOL_COMMAND_PING | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
		command.header.channelID = 0xFF;

		enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
	}

	void enet_peer_ping_interval(ENetPeer* peer, uint32_t pingInterval) {
		peer->pingInterval = pingInterval ? pingInterval : ENET_PEER_PING_INTERVAL;
	}

	void enet_peer_timeout(ENetPeer* peer, uint32_t timeoutLimit, uint32_t timeoutMinimum, uint32_t timeoutMaximum) {
		peer->timeoutLimit = timeoutLimit ? timeoutLimit : ENET_PEER_TIMEOUT_LIMIT;
		peer->timeoutMinimum = timeoutMinimum ? timeoutMinimum : ENET_PEER_TIMEOUT_MINIMUM;
		peer->timeoutMaximum = timeoutMaximum ? timeoutMaximum : ENET_PEER_TIMEOUT_MAXIMUM;
	}

	void enet_peer_disconnect_now(ENetPeer* peer, uint32_t data) {
		ENetProtocol command;

		if (peer->state == ENET_PEER_STATE_DISCONNECTED)
			return;

		if (peer->state != ENET_PEER_STATE_ZOMBIE && peer->state != ENET_PEER_STATE_DISCONNECTING) {
			enet_peer_reset_queues(peer);

			command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
			command.header.channelID = 0xFF;
			command.disconnect.data = ENET_HOST_TO_NET_32(data);

			enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
			enet_host_flush(peer->host);
		}

		enet_peer_reset(peer);
	}

	void enet_peer_disconnect(ENetPeer* peer, uint32_t data) {
		ENetProtocol command;

		if (peer->state == ENET_PEER_STATE_DISCONNECTING || peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT || peer->state == ENET_PEER_STATE_ZOMBIE)
			return;

		enet_peer_reset_queues(peer);

		command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT;
		command.header.channelID = 0xFF;
		command.disconnect.data = ENET_HOST_TO_NET_32(data);

		if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
			command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
		else
			command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;

		enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);

		if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER) {
			enet_peer_on_disconnect(peer);

			peer->state = ENET_PEER_STATE_DISCONNECTING;
		} else {
			enet_host_flush(peer->host);
			enet_peer_reset(peer);
		}
	}

	void enet_peer_disconnect_later(ENetPeer* peer, uint32_t data) {
		if ((peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER) && !(enet_list_empty(&peer->outgoingCommands) && enet_list_empty(&peer->sentReliableCommands))) {
			peer->state = ENET_PEER_STATE_DISCONNECT_LATER;
			peer->eventData = data;
		} else {
			enet_peer_disconnect(peer, data);
		}
	}

	ENetAcknowledgement* enet_peer_queue_acknowledgement(ENetPeer* peer, const ENetProtocol* command, uint16_t sentTime) {
		ENetAcknowledgement* acknowledgement;

		if (command->header.channelID < peer->channelCount) {
			ENetChannel* channel = &peer->channels[command->header.channelID];
			uint16_t reliableWindow = command->header.reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
			uint16_t currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

			if (command->header.reliableSequenceNumber < channel->incomingReliableSequenceNumber)
				reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

			if (reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1 && reliableWindow <= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS)
				return NULL;
		}

		acknowledgement = (ENetAcknowledgement*)enet_malloc(sizeof(ENetAcknowledgement));

		if (acknowledgement == NULL)
			return NULL;

		peer->outgoingDataTotal += sizeof(ENetProtocolAcknowledge);
		acknowledgement->sentTime = sentTime;
		acknowledgement->command = *command;

		enet_list_insert(enet_list_end(&peer->acknowledgements), acknowledgement);

		return acknowledgement;
	}

	void enet_peer_setup_outgoing_command(ENetPeer* peer, ENetOutgoingCommand* outgoingCommand) {
		ENetChannel* channel = &peer->channels[outgoingCommand->command.header.channelID];
		peer->outgoingDataTotal += enet_protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

		if (outgoingCommand->command.header.channelID == 0xFF) {
			++peer->outgoingReliableSequenceNumber;
			outgoingCommand->reliableSequenceNumber = peer->outgoingReliableSequenceNumber;
			outgoingCommand->unreliableSequenceNumber = 0;
		} else if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
			++channel->outgoingReliableSequenceNumber;
			channel->outgoingUnreliableSequenceNumber = 0;
			outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
			outgoingCommand->unreliableSequenceNumber = 0;
		} else if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED) {
			++peer->outgoingUnsequencedGroup;
			outgoingCommand->reliableSequenceNumber = 0;
			outgoingCommand->unreliableSequenceNumber = 0;
		} else {
			if (outgoingCommand->fragmentOffset == 0)
				++channel->outgoingUnreliableSequenceNumber;

			outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
			outgoingCommand->unreliableSequenceNumber = channel->outgoingUnreliableSequenceNumber;
		}

		outgoingCommand->sendAttempts = 0;
		outgoingCommand->sentTime = 0;
		outgoingCommand->roundTripTimeout = 0;
		outgoingCommand->roundTripTimeoutLimit = 0;
		outgoingCommand->command.header.reliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);

		switch (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) {
			case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
				outgoingCommand->command.sendUnreliable.unreliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);

				break;

			case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
				outgoingCommand->command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(peer->outgoingUnsequencedGroup);

				break;

			default:
				break;
		}

		enet_list_insert(enet_list_end(&peer->outgoingCommands), outgoingCommand);
	}

	ENetOutgoingCommand* enet_peer_queue_outgoing_command(ENetPeer* peer, const ENetProtocol* command, ENetPacket* packet, uint32_t offset, uint16_t length) {
		ENetOutgoingCommand* outgoingCommand = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));

		if (outgoingCommand == NULL)
			return NULL;

		outgoingCommand->command = *command;
		outgoingCommand->fragmentOffset = offset;
		outgoingCommand->fragmentLength = length;
		outgoingCommand->packet = packet;

		if (packet != NULL)
			++packet->referenceCount;

		enet_peer_setup_outgoing_command(peer, outgoingCommand);

		return outgoingCommand;
	}

	void enet_peer_dispatch_incoming_unreliable_commands(ENetPeer* peer, ENetChannel* channel, ENetIncomingCommand* queuedCommand) {
		ENetListIterator droppedCommand, startCommand, currentCommand;

		for (droppedCommand = startCommand = currentCommand = enet_list_begin(&channel->incomingUnreliableCommands); currentCommand != enet_list_end(&channel->incomingUnreliableCommands); currentCommand = enet_list_next(currentCommand)) {
			ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)currentCommand;

			if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
				continue;

			if (incomingCommand->reliableSequenceNumber == channel->incomingReliableSequenceNumber) {
				if (incomingCommand->fragmentsRemaining <= 0) {
					channel->incomingUnreliableSequenceNumber = incomingCommand->unreliableSequenceNumber;

					continue;
				}

				if (startCommand != currentCommand) {
					enet_list_move(enet_list_end(&peer->dispatchedCommands), startCommand, enet_list_previous(currentCommand));

					if (!peer->needsDispatch) {
						enet_list_insert(enet_list_end(&peer->host->dispatchQueue), &peer->dispatchList);

						peer->needsDispatch = 1;
					}

					droppedCommand = currentCommand;
				} else if (droppedCommand != currentCommand) {
					droppedCommand = enet_list_previous(currentCommand);
				}
			} else {
				uint16_t reliableWindow = incomingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
				uint16_t currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

				if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
					reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

				if (reliableWindow >= currentWindow && reliableWindow < currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
					break;

				droppedCommand = enet_list_next(currentCommand);

				if (startCommand != currentCommand) {
					enet_list_move(enet_list_end(&peer->dispatchedCommands), startCommand, enet_list_previous(currentCommand));

					if (!peer->needsDispatch) {
						enet_list_insert(enet_list_end(&peer->host->dispatchQueue), &peer->dispatchList);

						peer->needsDispatch = 1;
					}
				}
			}

			startCommand = enet_list_next(currentCommand);
		}

		if (startCommand != currentCommand) {
			enet_list_move(enet_list_end(&peer->dispatchedCommands), startCommand, enet_list_previous(currentCommand));

			if (!peer->needsDispatch) {
				enet_list_insert(enet_list_end(&peer->host->dispatchQueue), &peer->dispatchList);

				peer->needsDispatch = 1;
			}

			droppedCommand = currentCommand;
		}

		enet_peer_remove_incoming_commands(&channel->incomingUnreliableCommands, enet_list_begin(&channel->incomingUnreliableCommands), droppedCommand, queuedCommand);
	}

	void enet_peer_dispatch_incoming_reliable_commands(ENetPeer* peer, ENetChannel* channel, ENetIncomingCommand* queuedCommand) {
		ENetListIterator currentCommand;

		for (currentCommand = enet_list_begin(&channel->incomingReliableCommands); currentCommand != enet_list_end(&channel->incomingReliableCommands); currentCommand = enet_list_next(currentCommand)) {
			ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)currentCommand;

			if (incomingCommand->fragmentsRemaining > 0 || incomingCommand->reliableSequenceNumber != (uint16_t)(channel->incomingReliableSequenceNumber + 1))
				break;

			channel->incomingReliableSequenceNumber = incomingCommand->reliableSequenceNumber;

			if (incomingCommand->fragmentCount > 0)
				channel->incomingReliableSequenceNumber += incomingCommand->fragmentCount - 1;
		}

		if (currentCommand == enet_list_begin(&channel->incomingReliableCommands))
			return;

		channel->incomingUnreliableSequenceNumber = 0;

		enet_list_move(enet_list_end(&peer->dispatchedCommands), enet_list_begin(&channel->incomingReliableCommands), enet_list_previous(currentCommand));

		if (!peer->needsDispatch) {
			enet_list_insert(enet_list_end(&peer->host->dispatchQueue), &peer->dispatchList);

			peer->needsDispatch = 1;
		}

		if (!enet_list_empty(&channel->incomingUnreliableCommands))
			enet_peer_dispatch_incoming_unreliable_commands(peer, channel, queuedCommand);
	}

	ENetIncomingCommand* enet_peer_queue_incoming_command(ENetPeer* peer, const ENetProtocol* command, const void* data, size_t dataLength, uint32_t flags, uint32_t fragmentCount) {
		static ENetIncomingCommand dummyCommand;

		ENetChannel* channel = &peer->channels[command->header.channelID];
		uint32_t unreliableSequenceNumber = 0, reliableSequenceNumber = 0;
		uint16_t reliableWindow, currentWindow;
		ENetIncomingCommand* incomingCommand;
		ENetListIterator currentCommand;
		ENetPacket* packet = NULL;

		if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
			goto discardCommand;

		if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED) {
			reliableSequenceNumber = command->header.reliableSequenceNumber;
			reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
			currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

			if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
				reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

			if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
				goto discardCommand;
		}

		switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK) {
			case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
			case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
				if (reliableSequenceNumber == channel->incomingReliableSequenceNumber)
					goto discardCommand;

				for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingReliableCommands)); currentCommand != enet_list_end(&channel->incomingReliableCommands); currentCommand = enet_list_previous(currentCommand)) {
					incomingCommand = (ENetIncomingCommand*)currentCommand;

					if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
						if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
							continue;
					} else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
						break;
					}

					if (incomingCommand->reliableSequenceNumber <= reliableSequenceNumber) {
						if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
							break;

						goto discardCommand;
					}
				}

				break;

			case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
			case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
				unreliableSequenceNumber = ENET_NET_TO_HOST_16(command->sendUnreliable.unreliableSequenceNumber);

				if (reliableSequenceNumber == channel->incomingReliableSequenceNumber && unreliableSequenceNumber <= channel->incomingUnreliableSequenceNumber)
					goto discardCommand;

				for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingUnreliableCommands)); currentCommand != enet_list_end(&channel->incomingUnreliableCommands); currentCommand = enet_list_previous(currentCommand)) {
					incomingCommand = (ENetIncomingCommand*)currentCommand;

					if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
						continue;

					if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
						if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
							continue;
					} else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber) {
						break;
					}

					if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
						break;

					if (incomingCommand->reliableSequenceNumber > reliableSequenceNumber)
						continue;

					if (incomingCommand->unreliableSequenceNumber <= unreliableSequenceNumber) {
						if (incomingCommand->unreliableSequenceNumber < unreliableSequenceNumber)
							break;

						goto discardCommand;
					}
				}

				break;

			case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
				currentCommand = enet_list_end(&channel->incomingUnreliableCommands);

				break;

			default:
				goto discardCommand;
		}

		if (peer->totalWaitingData >= peer->host->maximumWaitingData)
			goto notifyError;

		packet = enet_packet_create(data, dataLength, flags);

		if (packet == NULL)
			goto notifyError;

		incomingCommand = (ENetIncomingCommand*)enet_malloc(sizeof(ENetIncomingCommand));

		if (incomingCommand == NULL)
			goto notifyError;

		incomingCommand->reliableSequenceNumber = command->header.reliableSequenceNumber;
		incomingCommand->unreliableSequenceNumber = unreliableSequenceNumber & 0xFFFF;
		incomingCommand->command = *command;
		incomingCommand->fragmentCount = fragmentCount;
		incomingCommand->fragmentsRemaining = fragmentCount;
		incomingCommand->packet = packet;
		incomingCommand->fragments = NULL;

		if (fragmentCount > 0) {
			if (fragmentCount <= ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
				incomingCommand->fragments = (uint32_t*)enet_malloc((fragmentCount + 31) / 32 * sizeof(uint32_t));

			if (incomingCommand->fragments == NULL) {
				enet_free(incomingCommand);

				goto notifyError;
			}

			memset(incomingCommand->fragments, 0, (fragmentCount + 31) / 32 * sizeof(uint32_t));
		}

		if (packet != NULL) {
			++packet->referenceCount;
			peer->totalWaitingData += packet->dataLength;
		}

		enet_list_insert(enet_list_next(currentCommand), incomingCommand);

		switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK) {
			case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
			case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
				enet_peer_dispatch_incoming_reliable_commands(peer, channel, incomingCommand);

				break;

			default:
				enet_peer_dispatch_incoming_unreliable_commands(peer, channel, incomingCommand);

				break;
		}

		return incomingCommand;

		discardCommand:

		if (fragmentCount > 0)
			goto notifyError;

		if (packet != NULL && packet->referenceCount == 0)
			enet_packet_destroy(packet);

		return &dummyCommand;

		notifyError:

		if (packet != NULL && packet->referenceCount == 0)
			enet_packet_destroy(packet);

		return NULL;
	}

/*
=======================================================================

	Host

=======================================================================
*/

	ENetHost* enet_host_create(const ENetAddress* address, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth, int bufferSize) {
		ENetHost* host;
		ENetPeer* currentPeer;

		if (peerCount > ENET_PROTOCOL_MAXIMUM_PEER_ID)
			return NULL;

		host = (ENetHost*)enet_malloc(sizeof(ENetHost));

		if (host == NULL)
			return NULL;

		memset(host, 0, sizeof(ENetHost));

		host->peers = (ENetPeer*)enet_malloc(peerCount * sizeof(ENetPeer));

		if (host->peers == NULL) {
			enet_free(host);

			return NULL;
		}

		memset(host->peers, 0, peerCount * sizeof(ENetPeer));

		host->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);

		if (host->socket != ENET_SOCKET_NULL)
			enet_socket_set_option(host->socket, ENET_SOCKOPT_IPV6_V6ONLY, 0);

		if (host->socket == ENET_SOCKET_NULL || (address != NULL && enet_socket_bind(host->socket, address) < 0)) {
			if (host->socket != ENET_SOCKET_NULL)
				enet_socket_destroy(host->socket);

			enet_free(host->peers);
			enet_free(host);

			return NULL;
		}

		if (bufferSize > ENET_HOST_BUFFER_SIZE_MAX)
			bufferSize = ENET_HOST_BUFFER_SIZE_MAX;
		else if (bufferSize < ENET_HOST_BUFFER_SIZE_MIN)
			bufferSize = ENET_HOST_BUFFER_SIZE_MIN;

		enet_socket_set_option(host->socket, ENET_SOCKOPT_NONBLOCK, 1);
		enet_socket_set_option(host->socket, ENET_SOCKOPT_BROADCAST, 1);
		enet_socket_set_option(host->socket, ENET_SOCKOPT_RCVBUF, bufferSize);
		enet_socket_set_option(host->socket, ENET_SOCKOPT_SNDBUF, bufferSize);

		if (address != NULL && enet_socket_get_address(host->socket, &host->address) < 0)
			host->address = *address;

		if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
			channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
		else if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
			channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

		host->randomSeed = (uint32_t)(size_t)host;
		host->randomSeed += enet_host_random_seed();
		host->randomSeed = (host->randomSeed << 16) | (host->randomSeed >> 16);
		host->channelLimit = channelLimit;
		host->incomingBandwidth = incomingBandwidth;
		host->outgoingBandwidth = outgoingBandwidth;
		host->bandwidthThrottleEpoch = 0;
		host->recalculateBandwidthLimits = 0;
		host->preventConnections = 0;
		host->mtu = ENET_HOST_DEFAULT_MTU;
		host->peerCount = peerCount;
		host->commandCount = 0;
		host->bufferCount = 0;
		host->checksumCallback = NULL;
		host->receivedAddress.ipv6 = ENET_HOST_ANY;
		host->receivedAddress.port = 0;
		host->receivedData = NULL;
		host->receivedDataLength = 0;
		host->totalSentData = 0;
		host->totalSentPackets = 0;
		host->totalReceivedData = 0;
		host->totalReceivedPackets = 0;
		host->connectedPeers = 0;
		host->bandwidthLimitedPeers = 0;
		host->duplicatePeers = ENET_PROTOCOL_MAXIMUM_PEER_ID;
		host->maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
		host->maximumWaitingData = ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA;
		host->interceptCallback = NULL;

		enet_list_clear(&host->dispatchQueue);

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			currentPeer->host = host;
			currentPeer->incomingPeerID = currentPeer - host->peers;
			currentPeer->outgoingSessionID = currentPeer->incomingSessionID = 0xFF;
			currentPeer->data = NULL;

			enet_list_clear(&currentPeer->acknowledgements);
			enet_list_clear(&currentPeer->sentReliableCommands);
			enet_list_clear(&currentPeer->sentUnreliableCommands);
			enet_list_clear(&currentPeer->outgoingCommands);
			enet_list_clear(&currentPeer->dispatchedCommands);
			enet_peer_reset(currentPeer);
		}

		return host;
	}

	void enet_host_destroy(ENetHost* host) {
		ENetPeer* currentPeer;

		if (host == NULL)
			return;

		enet_socket_destroy(host->socket);

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			enet_peer_reset(currentPeer);
		}

		enet_free(host->peers);
		enet_free(host);
	}

	void enet_host_prevent_connections(ENetHost* host, uint8_t state) {
		if (host == NULL)
			return;

		host->preventConnections = state;
	}

	ENetPeer* enet_host_connect(ENetHost* host, const ENetAddress* address, size_t channelCount, uint32_t data) {
		ENetPeer* currentPeer;
		ENetChannel* channel;
		ENetProtocol command;

		if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
			channelCount = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;
		else if (channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
			channelCount = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED)
				break;
		}

		if (currentPeer >= &host->peers[host->peerCount])
			return NULL;

		currentPeer->channels = (ENetChannel*)enet_malloc(channelCount * sizeof(ENetChannel));

		if (currentPeer->channels == NULL)
			return NULL;

		currentPeer->channelCount = channelCount;
		currentPeer->state = ENET_PEER_STATE_CONNECTING;
		currentPeer->address = *address;
		currentPeer->connectID = ++host->randomSeed;

		if (host->outgoingBandwidth == 0)
			currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
		else
			currentPeer->windowSize = (host->outgoingBandwidth / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

		if (currentPeer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
			currentPeer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
		else if (currentPeer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
			currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

		for (channel = currentPeer->channels; channel < &currentPeer->channels[channelCount]; ++channel) {
			channel->outgoingReliableSequenceNumber = 0;
			channel->outgoingUnreliableSequenceNumber = 0;
			channel->incomingReliableSequenceNumber = 0;
			channel->incomingUnreliableSequenceNumber = 0;

			enet_list_clear(&channel->incomingReliableCommands);
			enet_list_clear(&channel->incomingUnreliableCommands);

			channel->usedReliableWindows = 0;

			memset(channel->reliableWindows, 0, sizeof(channel->reliableWindows));
		}

		command.header.command = ENET_PROTOCOL_COMMAND_CONNECT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
		command.header.channelID = 0xFF;
		command.connect.outgoingPeerID = ENET_HOST_TO_NET_16(currentPeer->incomingPeerID);
		command.connect.incomingSessionID = currentPeer->incomingSessionID;
		command.connect.outgoingSessionID = currentPeer->outgoingSessionID;
		command.connect.mtu = ENET_HOST_TO_NET_32(currentPeer->mtu);
		command.connect.windowSize = ENET_HOST_TO_NET_32(currentPeer->windowSize);
		command.connect.channelCount = ENET_HOST_TO_NET_32(channelCount);
		command.connect.incomingBandwidth = ENET_HOST_TO_NET_32(host->incomingBandwidth);
		command.connect.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);
		command.connect.packetThrottleInterval = ENET_HOST_TO_NET_32(currentPeer->packetThrottleInterval);
		command.connect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleAcceleration);
		command.connect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleDeceleration);
		command.connect.connectID = currentPeer->connectID;
		command.connect.data = ENET_HOST_TO_NET_32(data);

		enet_peer_queue_outgoing_command(currentPeer, &command, NULL, 0, 0);

		return currentPeer;
	}

	void enet_host_broadcast(ENetHost* host, uint8_t channelID, ENetPacket* packet) {
		ENetPeer* currentPeer;

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			++packet->referenceCount;

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
				continue;

			enet_peer_send(currentPeer, channelID, packet);
		}

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			--packet->referenceCount;

		if (packet->referenceCount == 0)
			enet_packet_destroy(packet);
	}

	void enet_host_broadcast_exclude(ENetHost* host, uint8_t channelID, ENetPacket* packet, ENetPeer* excludedPeer) {
		ENetPeer* currentPeer;

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			++packet->referenceCount;

		for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer) {
			if (currentPeer->state != ENET_PEER_STATE_CONNECTED || currentPeer == excludedPeer)
				continue;

			enet_peer_send(currentPeer, channelID, packet);
		}

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			--packet->referenceCount;

		if (packet->referenceCount == 0)
			enet_packet_destroy(packet);
	}

	void enet_host_broadcast_selective(ENetHost* host, uint8_t channelID, ENetPacket* packet, ENetPeer** peers, size_t length) {
		ENetPeer* currentPeer;
		size_t i;

		if (host == NULL)
			return;

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			++packet->referenceCount;

		for (i = 0; i < length; i++) {
			currentPeer = peers[i];

			if (currentPeer == NULL || currentPeer->state != ENET_PEER_STATE_CONNECTED)
				continue;

			enet_peer_send(currentPeer, channelID, packet);
		}

		if (packet->flags & ENET_PACKET_FLAG_INSTANT)
			--packet->referenceCount;

		if (packet->referenceCount == 0)
			enet_packet_destroy(packet);
	}

	void enet_host_channel_limit(ENetHost* host, size_t channelLimit) {
		if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
			channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
		else if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
			channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

		host->channelLimit = channelLimit;
	}

	void enet_host_bandwidth_limit(ENetHost* host, uint32_t incomingBandwidth, uint32_t outgoingBandwidth) {
		host->incomingBandwidth = incomingBandwidth;
		host->outgoingBandwidth = outgoingBandwidth;
		host->recalculateBandwidthLimits = 1;
	}

	void enet_host_bandwidth_throttle(ENetHost* host) {
		uint32_t timeCurrent = enet_time_get();
		uint32_t elapsedTime = timeCurrent - host->bandwidthThrottleEpoch;
		uint32_t peersRemaining = (uint32_t)host->connectedPeers;
		uint32_t dataTotal = ~0;
		uint32_t bandwidth = ~0;
		uint32_t throttle = 0;
		uint32_t bandwidthLimit = 0;

		int needsAdjustment = host->bandwidthLimitedPeers > 0 ? 1 : 0;
		ENetPeer* peer;
		ENetProtocol command;

		if (elapsedTime < ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
			return;

		if (host->outgoingBandwidth == 0 && host->incomingBandwidth == 0)
			return;

		host->bandwidthThrottleEpoch = timeCurrent;

		if (peersRemaining == 0)
			return;

		if (host->outgoingBandwidth != 0) {
			dataTotal = 0;
			bandwidth = (host->outgoingBandwidth * elapsedTime) / 1000;

			for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer) {
				if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
					continue;

				dataTotal += peer->outgoingDataTotal;
			}
		}

		while (peersRemaining > 0 && needsAdjustment != 0) {
			needsAdjustment = 0;

			if (dataTotal <= bandwidth)
				throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
			else
				throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

			for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer) {
				uint32_t peerBandwidth;

				if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) || peer->incomingBandwidth == 0 || peer->outgoingBandwidthThrottleEpoch == timeCurrent)
					continue;

				peerBandwidth = (peer->incomingBandwidth * elapsedTime) / 1000;

				if ((throttle * peer->outgoingDataTotal) / ENET_PEER_PACKET_THROTTLE_SCALE <= peerBandwidth)
					continue;

				peer->packetThrottleLimit = (peerBandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / peer->outgoingDataTotal;

				if (peer->packetThrottleLimit == 0)
					peer->packetThrottleLimit = 1;

				if (peer->packetThrottle > peer->packetThrottleLimit)
					peer->packetThrottle = peer->packetThrottleLimit;

				peer->outgoingBandwidthThrottleEpoch = timeCurrent;
				peer->incomingDataTotal = 0;
				peer->outgoingDataTotal = 0;
				needsAdjustment = 1;
				--peersRemaining;
				bandwidth -= peerBandwidth;
				dataTotal -= peerBandwidth;
			}
		}

		if (peersRemaining > 0) {
			if (dataTotal <= bandwidth)
				throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
			else
				throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

			for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer) {
				if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) || peer->outgoingBandwidthThrottleEpoch == timeCurrent)
					continue;

				peer->packetThrottleLimit = throttle;

				if (peer->packetThrottle > peer->packetThrottleLimit)
					peer->packetThrottle = peer->packetThrottleLimit;

				peer->incomingDataTotal = 0;
				peer->outgoingDataTotal = 0;
			}
		}

		if (host->recalculateBandwidthLimits) {
			host->recalculateBandwidthLimits = 0;
			peersRemaining = (uint32_t)host->connectedPeers;
			bandwidth = host->incomingBandwidth;
			needsAdjustment = 1;

			if (bandwidth == 0) {
				bandwidthLimit = 0;
			} else {
				while (peersRemaining > 0 && needsAdjustment != 0) {
					needsAdjustment = 0;
					bandwidthLimit = bandwidth / peersRemaining;

					for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer) {
						if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) || peer->incomingBandwidthThrottleEpoch == timeCurrent)
							continue;

						if (peer->outgoingBandwidth > 0 && peer->outgoingBandwidth >= bandwidthLimit)
							continue;

						peer->incomingBandwidthThrottleEpoch = timeCurrent;
						needsAdjustment = 1;
						--peersRemaining;
						bandwidth -= peer->outgoingBandwidth;
					}
				}
			}

			for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer) {
				if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
					continue;

				command.header.command = ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
				command.header.channelID = 0xFF;
				command.bandwidthLimit.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);

				if (peer->incomingBandwidthThrottleEpoch == timeCurrent)
					command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(peer->outgoingBandwidth);
				else
					command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(bandwidthLimit);

				enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
			}
		}
	}

/*
=======================================================================

	Address

=======================================================================
*/

	int enet_address_set_ip(ENetAddress* address, const char* ip) {
		int type = AF_INET6;
		void* destination = &address->ipv6;

		if (strchr(ip, ':') == NULL) {
			type = AF_INET;

			memset(address, 0, sizeof(address->ipv4.zeros));

			address->ipv4.ffff = 0xFFFF;
			destination = &address->ipv4.ip;
		}

		if (!inet_pton(type, ip, destination))
			return -1;

		return 0;
	}

	int enet_address_set_hostname(ENetAddress* address, const char* name) {
		struct addrinfo hints, *resultList = NULL, *result = NULL;

		memset(&hints, 0, sizeof(hints));

		hints.ai_family = AF_UNSPEC;

		if (getaddrinfo(name, NULL, &hints, &resultList) != 0)
			return -1;

		for (result = resultList; result != NULL; result = result->ai_next) {
			if (result->ai_addr != NULL && result->ai_addrlen >= sizeof(struct sockaddr_in)) {
				if (result->ai_family == AF_INET) {
					struct sockaddr_in* sin = (struct sockaddr_in*)result->ai_addr;

					memset(address, 0, sizeof(address->ipv4.zeros));

					address->ipv4.ffff = 0xFFFF;
					address->ipv4.ip.s_addr = sin->sin_addr.s_addr;

					freeaddrinfo(resultList);

					return 0;
				} else if (result->ai_family == AF_INET6) {
					struct sockaddr_in6* sin = (struct sockaddr_in6*)result->ai_addr;

					address->ipv6 = sin->sin6_addr;

					freeaddrinfo(resultList);

					return 0;
				}
			}
		}

		if (resultList != NULL)
			freeaddrinfo(resultList);

		return enet_address_set_ip(address, name);
	}

	int enet_address_get_ip(const ENetAddress* address, char* ip, size_t ipLength) {
		if (address->ipv4.ffff == 0xFFFF && enet_array_is_zeroed(address->ipv4.zeros, sizeof(address->ipv4.zeros)) == 0) {
			if (inet_ntop(AF_INET, &address->ipv4.ip, ip, ipLength) == NULL)
				return -1;
		} else if (inet_ntop(AF_INET6, &address->ipv6, ip, ipLength) == NULL) {
			return -1;
		}

		return 0;
	}

	int enet_address_get_hostname(const ENetAddress* address, char* name, size_t nameLength) {
		struct sockaddr_in6 sin;
		int err;

		memset(&sin, 0, sizeof(struct sockaddr_in6));

		sin.sin6_family = AF_INET6;
		sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
		sin.sin6_addr = address->ipv6;

		err = getnameinfo((struct sockaddr*)&sin, sizeof(sin), name, nameLength, NULL, 0, NI_NAMEREQD);

		if (!err) {
			if (name != NULL && nameLength > 0 && !memchr(name, '\0', nameLength))
				return -1;

			return 0;
		}

		if (err != EAI_NONAME)
			return -1;

		return enet_address_get_ip(address, name, nameLength);
	}

/*
=======================================================================

	Platform-specific (Unix)

=======================================================================
*/

	#ifndef _WIN32
		int enet_initialize(void) {
			return 0;
		}

		void enet_deinitialize(void) { }

		uint64_t enet_host_random_seed(void) {
			struct timeval timeVal;

			gettimeofday(&timeVal, NULL);

			return (timeVal.tv_sec * 1000) ^ (timeVal.tv_usec / 1000);
		}

		int enet_socket_bind(ENetSocket socket, const ENetAddress* address) {
			struct sockaddr_in6 sin;

			memset(&sin, 0, sizeof(struct sockaddr_in6));

			sin.sin6_family = AF_INET6;

			if (address != NULL) {
				sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
				sin.sin6_addr = address->ipv6;
			} else {
				sin.sin6_port = 0;
				sin.sin6_addr = ENET_HOST_ANY;
			}

			return bind(socket, (struct sockaddr*)&sin, sizeof(struct sockaddr_in6));
		}

		int enet_socket_get_address(ENetSocket socket, ENetAddress* address) {
			struct sockaddr_storage ss;
			socklen_t saLength = sizeof(ss);

			if (getsockname(socket, (struct sockaddr*)&ss, &saLength) == -1)
				return -1;

			if (ss.ss_family == AF_INET) {
				struct sockaddr_in* sin = (struct sockaddr_in*)&ss;

				memset(address, 0, sizeof(address->ipv4.zeros));

				address->ipv4.ffff = 0xFFFF;
				address->ipv4.ip = sin->sin_addr;
				address->port = ENET_NET_TO_HOST_16(sin->sin_port);
			} else if (ss.ss_family == AF_INET6) {
				struct sockaddr_in6* sin = (struct sockaddr_in6*)&ss;

				address->ipv6 = sin->sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin->sin6_port);
			}

			return 0;
		}

		int enet_socket_listen(ENetSocket socket, int backlog) {
			return listen(socket, backlog < 0 ? SOMAXCONN : backlog);
		}

		ENetSocket enet_socket_create(ENetSocketType type) {
			int socketType = (type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM);

			#ifdef SOCK_CLOEXEC
				socketType |= SOCK_CLOEXEC;
			#endif

			return socket(PF_INET6, socketType, 0);
		}

		int enet_socket_set_option(ENetSocket socket, ENetSocketOption option, int value) {
			int result = -1;

			switch (option) {
				case ENET_SOCKOPT_NONBLOCK:
					result = fcntl(socket, F_SETFL, (value ? O_NONBLOCK : 0) | (fcntl(socket, F_GETFL) & ~O_NONBLOCK));

					break;

				case ENET_SOCKOPT_BROADCAST:
					result = setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_REUSEADDR:
					result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_RCVBUF:
					result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_SNDBUF:
					result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_RCVTIMEO: {
					struct timeval timeVal;

					timeVal.tv_sec = value / 1000;
					timeVal.tv_usec = (value % 1000) * 1000;
					result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeVal, sizeof(struct timeval));

					break;
				}

				case ENET_SOCKOPT_SNDTIMEO: {
					struct timeval timeVal;

					timeVal.tv_sec = value / 1000;
					timeVal.tv_usec = (value % 1000) * 1000;
					result = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeVal, sizeof(struct timeval));

					break;
				}

				case ENET_SOCKOPT_NODELAY:
					result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_IPV6_V6ONLY:
					result = setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&value, sizeof(int));

					break;

				default:
					break;
			}

			return result == -1 ? -1 : 0;
		}

		int enet_socket_get_option(ENetSocket socket, ENetSocketOption option, int* value) {
			int result = -1;
			socklen_t len;

			switch (option) {
				case ENET_SOCKOPT_ERROR:
					len = sizeof(int);
					result = getsockopt(socket, SOL_SOCKET, SO_ERROR, value, &len);

					break;

				default:
					break;
			}

			return result == -1 ? -1 : 0;
		}

		int enet_socket_connect(ENetSocket socket, const ENetAddress* address) {
			int result = -1;
			struct sockaddr_in6 sin;

			memset(&sin, 0, sizeof(struct sockaddr_in6));

			sin.sin6_family = AF_INET6;
			sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
			sin.sin6_addr = address->ipv6;

			result = connect(socket, (struct sockaddr*)&sin, sizeof(struct sockaddr_in6));

			if (result == -1 && errno == EINPROGRESS)
				return 0;

			return result;
		}

		ENetSocket enet_socket_accept(ENetSocket socket, ENetAddress* address) {
			int result = -1;
			struct sockaddr_in6 sin;
			socklen_t sinLength = sizeof(struct sockaddr_in6);

			result = accept(socket, address != NULL ? (struct sockaddr*)&sin : NULL, address != NULL ? &sinLength : NULL);

			if (result == -1)
				return ENET_SOCKET_NULL;

			if (address != NULL) {
				address->ipv6 = sin.sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin.sin6_port);
			}

			return result;
		}

		int enet_socket_shutdown(ENetSocket socket, ENetSocketShutdown how) {
			return shutdown(socket, (int)how);
		}

		void enet_socket_destroy(ENetSocket socket) {
			if (socket != ENET_SOCKET_NULL)
				close(socket);
		}

		int enet_socket_send(ENetSocket socket, const ENetAddress* address, const ENetBuffer* buffers, size_t bufferCount) {
			struct msghdr msgHdr;
			struct sockaddr_in6 sin;
			int sentLength;

			memset(&msgHdr, 0, sizeof(struct msghdr));

			if (address != NULL) {
				memset(&sin, 0, sizeof(struct sockaddr_in6));

				sin.sin6_family = AF_INET6;
				sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
				sin.sin6_addr = address->ipv6;
				msgHdr.msg_name = &sin;
				msgHdr.msg_namelen = sizeof(struct sockaddr_in6);
			}

			msgHdr.msg_iov = (struct iovec*)buffers;
			msgHdr.msg_iovlen = bufferCount;
			sentLength = sendmsg(socket, &msgHdr, MSG_NOSIGNAL);

			if (sentLength == -1) {
				if (errno == EWOULDBLOCK)
					return 0;

				return -1;
			}

			return sentLength;
		}

		int enet_socket_receive(ENetSocket socket, ENetAddress* address, ENetBuffer* buffers, size_t bufferCount) {
			struct msghdr msgHdr;
			struct sockaddr_in6 sin;
			int recvLength;

			memset(&msgHdr, 0, sizeof(struct msghdr));

			if (address != NULL) {
				msgHdr.msg_name = &sin;
				msgHdr.msg_namelen = sizeof(struct sockaddr_in6);
			}

			msgHdr.msg_iov = (struct iovec*)buffers;
			msgHdr.msg_iovlen = bufferCount;
			recvLength = recvmsg(socket, &msgHdr, MSG_NOSIGNAL);

			if (recvLength == -1) {
				if (errno == EWOULDBLOCK)
					return 0;

				return -1;
			}

			if (msgHdr.msg_flags & MSG_TRUNC)
				return -2;

			if (address != NULL) {
				address->ipv6 = sin.sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin.sin6_port);
			}

			return recvLength;
		}

		int enet_socket_set_select(ENetSocket maxSocket, ENetSocketSet* readSet, ENetSocketSet* writeSet, uint32_t timeout) {
			struct timeval timeVal;

			timeVal.tv_sec = timeout / 1000;
			timeVal.tv_usec = (timeout % 1000) * 1000;

			return select(maxSocket + 1, readSet, writeSet, NULL, &timeVal);
		}

		int enet_socket_wait(ENetSocket socket, uint32_t* condition, uint64_t timeout) {
			struct pollfd pollSocket;
			int pollCount;

			pollSocket.fd = socket;
			pollSocket.events = 0;

			if (*condition & ENET_SOCKET_WAIT_SEND)
				pollSocket.events |= POLLOUT;

			if (*condition & ENET_SOCKET_WAIT_RECEIVE)
				pollSocket.events |= POLLIN;

			pollCount = poll(&pollSocket, 1, timeout);

			if (pollCount < 0) {
				if (errno == EINTR && *condition & ENET_SOCKET_WAIT_INTERRUPT) {
					*condition = ENET_SOCKET_WAIT_INTERRUPT;

					return 0;
				}

				return -1;
			}

			*condition = ENET_SOCKET_WAIT_NONE;

			if (pollCount == 0)
				return 0;

			if (pollSocket.revents & POLLOUT)
				*condition |= ENET_SOCKET_WAIT_SEND;

			if (pollSocket.revents & POLLIN)
				*condition |= ENET_SOCKET_WAIT_RECEIVE;

			return 0;
		}
	#endif

/*
=======================================================================

	Platform-specific (Windows)

=======================================================================
*/

	#ifdef _WIN32
		int enet_initialize(void) {
			WORD versionRequested = MAKEWORD(2, 2);
			WSADATA wsaData;

			if (WSAStartup(versionRequested, &wsaData))
				return -1;

			if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
				WSACleanup();

				return -1;
			}

			timeBeginPeriod(1);

			return 0;
		}

		void enet_deinitialize(void) {
			timeEndPeriod(1);
			WSACleanup();
		}

		uint64_t enet_host_random_seed(void) {
			return (uint64_t)timeGetTime();
		}

		int enet_socket_bind(ENetSocket socket, const ENetAddress* address) {
			struct sockaddr_in6 sin;

			memset(&sin, 0, sizeof(struct sockaddr_in6));

			sin.sin6_family = AF_INET6;

			if (address != NULL) {
				sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
				sin.sin6_addr = address->ipv6;
			} else {
				sin.sin6_port = 0;
				sin.sin6_addr = in6addr_any;
			}

			return bind(socket, (struct sockaddr*)&sin, sizeof(struct sockaddr_in6)) == SOCKET_ERROR ? -1 : 0;
		}

		int enet_socket_get_address(ENetSocket socket, ENetAddress* address) {
			struct sockaddr_storage ss;
			int saLength = sizeof(ss);

			if (getsockname(socket, (struct sockaddr*)&ss, &saLength) == -1)
				return -1;

			if (ss.ss_family == AF_INET) {
				struct sockaddr_in* sin = (struct sockaddr_in*)&ss;

				memset(address, 0, sizeof(address->ipv4.zeros));

				address->ipv4.ffff = 0xFFFF;
				address->ipv4.ip = sin->sin_addr;
				address->port = ENET_NET_TO_HOST_16(sin->sin_port);
			} else if (ss.ss_family == AF_INET6) {
				struct sockaddr_in6* sin = (struct sockaddr_in6*)&ss;

				address->ipv6 = sin->sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin->sin6_port);
			}

			return 0;
		}

		int enet_socket_listen(ENetSocket socket, int backlog) {
			return listen(socket, backlog < 0 ? SOMAXCONN : backlog) == SOCKET_ERROR ? -1 : 0;
		}

		ENetSocket enet_socket_create(ENetSocketType type) {
			return socket(PF_INET6, type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
		}

		int enet_socket_set_option(ENetSocket socket, ENetSocketOption option, int value) {
			int result = SOCKET_ERROR;

			switch (option) {
				case ENET_SOCKOPT_NONBLOCK: {
					u_long nonBlocking = (u_long)value;

					result = ioctlsocket(socket, FIONBIO, &nonBlocking);

					break;
				}

				case ENET_SOCKOPT_BROADCAST:
					result = setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_REUSEADDR:
					result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_RCVBUF:
					result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_SNDBUF:
					result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_RCVTIMEO:
					result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_SNDTIMEO:
					result = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_NODELAY:
					result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&value, sizeof(int));

					break;

				case ENET_SOCKOPT_IPV6_V6ONLY:
					result = setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&value, sizeof(int));

					break;

				default:
					break;
			}

			return result == SOCKET_ERROR ? -1 : 0;
		}

		int enet_socket_get_option(ENetSocket socket, ENetSocketOption option, int* value) {
			int result = SOCKET_ERROR, len;

			switch (option) {
				case ENET_SOCKOPT_ERROR:
					len = sizeof(int);
					result = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*)value, &len);

					break;

				default:
					break;
			}

			return result == SOCKET_ERROR ? -1 : 0;
		}

		int enet_socket_connect(ENetSocket socket, const ENetAddress* address) {
			int result = -1;
			struct sockaddr_in6 sin;

			memset(&sin, 0, sizeof(struct sockaddr_in6));

			sin.sin6_family = AF_INET6;
			sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
			sin.sin6_addr = address->ipv6;
			result = connect(socket, (struct sockaddr*)&sin, sizeof(struct sockaddr_in6));

			if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
				return -1;

			return 0;
		}

		ENetSocket enet_socket_accept(ENetSocket socket, ENetAddress* address) {
			SOCKET result;
			struct sockaddr_in6 sin;
			int sinLength = sizeof(struct sockaddr_in6);

			result = accept(socket, address != NULL ? (struct sockaddr*)&sin : NULL, address != NULL ? &sinLength : NULL);

			if (result == INVALID_SOCKET)
				return ENET_SOCKET_NULL;

			if (address != NULL) {
				address->ipv6 = sin.sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin.sin6_port);
			}

			return result;
		}

		int enet_socket_shutdown(ENetSocket socket, ENetSocketShutdown how) {
			return shutdown(socket, (int)how) == SOCKET_ERROR ? -1 : 0;
		}

		void enet_socket_destroy(ENetSocket socket) {
			if (socket != INVALID_SOCKET)
				closesocket(socket);
		}

		int enet_socket_send(ENetSocket socket, const ENetAddress* address, const ENetBuffer* buffers, size_t bufferCount) {
			struct sockaddr_in6 sin;
			DWORD sentLength = 0;

			if (address != NULL) {
				memset(&sin, 0, sizeof(struct sockaddr_in6));

				sin.sin6_family = AF_INET6;
				sin.sin6_port = ENET_HOST_TO_NET_16(address->port);
				sin.sin6_addr = address->ipv6;
			}

			if (WSASendTo(socket, (LPWSABUF)buffers, (DWORD)bufferCount, &sentLength, 0, address != NULL ? (struct sockaddr*)&sin : NULL, address != NULL ? sizeof(struct sockaddr_in6) : 0, NULL, NULL) == SOCKET_ERROR)
				return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;

			return (int)sentLength;
		}

		int enet_socket_receive(ENetSocket socket, ENetAddress* address, ENetBuffer* buffers, size_t bufferCount) {
			INT sinLength = sizeof(struct sockaddr_in6);
			DWORD flags = 0, recvLength = 0;
			struct sockaddr_in6 sin;

			if (WSARecvFrom(socket, (LPWSABUF)buffers, (DWORD)bufferCount, &recvLength, &flags, address != NULL ? (struct sockaddr*)&sin : NULL, address != NULL ? &sinLength : NULL, NULL, NULL) == SOCKET_ERROR) {
				switch (WSAGetLastError()) {
					case WSAEWOULDBLOCK:
					case WSAECONNRESET:
						return 0;
				}

				return -1;
			}

			if (flags & MSG_PARTIAL)
				return -2;

			if (address != NULL) {
				address->ipv6 = sin.sin6_addr;
				address->port = ENET_NET_TO_HOST_16(sin.sin6_port);
			}

			return (int)recvLength;
		}

		int enet_socket_set_select(ENetSocket maxSocket, ENetSocketSet* readSet, ENetSocketSet* writeSet, uint32_t timeout) {
			struct timeval timeVal;

			timeVal.tv_sec = timeout / 1000;
			timeVal.tv_usec = (timeout % 1000) * 1000;

			return select(maxSocket + 1, readSet, writeSet, NULL, &timeVal);
		}

		int enet_socket_wait(ENetSocket socket, uint32_t* condition, uint64_t timeout) {
			fd_set readSet, writeSet;
			struct timeval timeVal;
			int selectCount;

			timeVal.tv_sec = timeout / 1000;
			timeVal.tv_usec = (timeout % 1000) * 1000;

			FD_ZERO(&readSet);
			FD_ZERO(&writeSet);

			if (*condition & ENET_SOCKET_WAIT_SEND)
				FD_SET(socket, &writeSet);

			if (*condition & ENET_SOCKET_WAIT_RECEIVE)
				FD_SET(socket, &readSet);

			selectCount = select(socket + 1, &readSet, &writeSet, NULL, &timeVal);

			if (selectCount < 0)
				return -1;

			*condition = ENET_SOCKET_WAIT_NONE;

			if (selectCount == 0)
				return 0;

			if (FD_ISSET(socket, &writeSet))
				*condition |= ENET_SOCKET_WAIT_SEND;

			if (FD_ISSET(socket, &readSet))
				*condition |= ENET_SOCKET_WAIT_RECEIVE;

			return 0;
		}
	#endif

/*
=======================================================================

	Extended functionality

=======================================================================
*/

	void* enet_packet_get_data(const ENetPacket* packet) {
		return (void*)packet->data;
	}

	void* enet_packet_get_user_data(const ENetPacket* packet) {
		return packet->userData;
	}

	void enet_packet_set_user_data(ENetPacket* packet, void* userData) {
		packet->userData = userData;
	}

	int enet_packet_get_length(const ENetPacket* packet) {
		return packet->dataLength;
	}

	void enet_packet_set_free_callback(ENetPacket* packet, ENetPacketFreeCallback callback) {
		packet->freeCallback = callback;
	}

	int enet_packet_check_references(const ENetPacket* packet) {
		return (int)packet->referenceCount;
	}

	void enet_packet_dispose(ENetPacket* packet) {
		if (packet->referenceCount == 0)
			enet_packet_destroy(packet);
	}

	uint32_t enet_host_get_peers_count(const ENetHost* host) {
		return host->connectedPeers;
	}

	uint32_t enet_host_get_packets_sent(const ENetHost* host) {
		return host->totalSentPackets;
	}

	uint32_t enet_host_get_packets_received(const ENetHost* host) {
		return host->totalReceivedPackets;
	}

	uint32_t enet_host_get_bytes_sent(const ENetHost* host) {
		return host->totalSentData;
	}

	uint32_t enet_host_get_bytes_received(const ENetHost* host) {
		return host->totalReceivedData;
	}

	void enet_host_set_max_duplicate_peers(ENetHost* host, uint16_t number) {
		if (number < 1)
			number = 1;

		if (number > ENET_PROTOCOL_MAXIMUM_PEER_ID)
			number = ENET_PROTOCOL_MAXIMUM_PEER_ID;

		host->duplicatePeers = number;
	}

	void enet_host_set_intercept_callback(ENetHost* host, ENetInterceptCallback callback) {
		host->interceptCallback = callback;
	}

	void enet_host_set_checksum_callback(ENetHost* host, ENetChecksumCallback callback) {
		host->checksumCallback = callback;
	}

	uint32_t enet_peer_get_id(const ENetPeer* peer) {
		return peer->incomingPeerID;
	}

	int enet_peer_get_ip(const ENetPeer* peer, char* ip, size_t ipLength) {
		return enet_address_get_ip(&peer->address, ip, ipLength);
	}

	uint16_t enet_peer_get_port(const ENetPeer* peer) {
		return peer->address.port;
	}

	uint32_t enet_peer_get_mtu(const ENetPeer* peer) {
		return peer->mtu;
	}

	ENetPeerState enet_peer_get_state(const ENetPeer* peer) {
		return peer->state;
	}

	uint32_t enet_peer_get_rtt(const ENetPeer* peer) {
		return peer->roundTripTime;
	}

	uint32_t enet_peer_get_last_rtt(const ENetPeer* peer) {
		return peer->lastRoundTripTime;
	}

	uint32_t enet_peer_get_lastsendtime(const ENetPeer* peer) {
		return peer->lastSendTime;
	}

	uint32_t enet_peer_get_lastreceivetime(const ENetPeer* peer) {
		return peer->lastReceiveTime;
	}

	uint64_t enet_peer_get_packets_sent(const ENetPeer* peer) {
		return peer->totalPacketsSent;
	}

	uint64_t enet_peer_get_packets_lost(const ENetPeer* peer) {
		return peer->totalPacketsLost;
	}

	float enet_peer_get_packets_throttle(const ENetPeer* peer) {
		return peer->packetThrottle / (float)ENET_PEER_PACKET_THROTTLE_SCALE * 100.0f;
	}

	uint64_t enet_peer_get_bytes_sent(const ENetPeer* peer) {
		return peer->totalDataSent;
	}

	uint64_t enet_peer_get_bytes_received(const ENetPeer* peer) {
		return peer->totalDataReceived;
	}

	void* enet_peer_get_data(const ENetPeer* peer) {
		return (void*)peer->data;
	}

	void enet_peer_set_data(ENetPeer* peer, const void* data) {
		peer->data = (uint32_t*)data;
	}

	#ifdef _MSC_VER
		#pragma warning(pop)
	#endif

#endif // ENET_IMPLEMENTATION

#endif // ENET_H

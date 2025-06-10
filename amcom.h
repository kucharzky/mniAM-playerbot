#ifndef AMCOM_H_
#define AMCOM_H_

/**
 * This header file defines the API for the AMCOM library that is responsible for sending and receiving AMCOM packets.
 *
 * Each AM packet consists of the following fields:
 *
 * +--------+--------+--------+--------+--------+--------------------------------------------------+
 * | SOP    | TYPE   | LENGTH | CRC             | PAYLOAD                                          |
 * | 1B     | 1B     | 1B     | 2B              | 0..200B                                          |
 * +--------+--------+--------+--------+--------+--------------------------------------------------+
 * <----- size of header is 5 bytes ----------->
 *
 * SOP (Start Of Packet) - indicates the start of new packet. One byte. Always 0xA1.
 * TYPE - byte defining the type of the packet. Valid values are from 0 to 255.
 * LENGTH - number of bytes in the payload. Can range from 0 to 200. 200 is the maximum packet payload length.
 * CRC - a two-byte field (uint16_t) containing the checksum of the packet. Encoding: little-endian (LSB first)
 *
 */


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#if defined __ARMCC_VERSION
// Definitions for KEIL:

/// Indicates that the structure shall be packed
#define AMPACKED							__packed

#elif defined __GNUC__
// Definitions for GCC:

/// Indicates that the structure shall be packed
#define AMPACKED 							__attribute__((packed))

#endif


/** Structure defining the packet header */
typedef struct AMPACKED {
	uint8_t sop;        ///< Start-Of-Packet field (always 0xA1)
	uint8_t type;       ///< Packet type (0..255)
	uint8_t length;     ///< Packet payload length (0..200)
	uint16_t crc;       ///< Cyclic Redundancy Check (CRC) field
} AMCOM_PacketHeader;

// static assertion to check that the header structure is indeed packed
static_assert(5 == sizeof(AMCOM_PacketHeader), "5 != sizeof(AMCOM_PacketHeader)");

enum {
	/// Maximum size of packet payload
	AMCOM_MAX_PAYLOAD_SIZE = 200,
	/// Maximum size of the whole packet
	AMCOM_MAX_PACKET_SIZE = (200 + sizeof(AMCOM_PacketHeader))
};

/** Structure defining the packet */
typedef struct AMPACKED {
	AMCOM_PacketHeader header;                ///< packet header
	uint8_t payload[AMCOM_MAX_PAYLOAD_SIZE];  ///< packet payload
} AMCOM_Packet;

// static assertion to check that the packet structure is indeed packed
static_assert(205 == sizeof(AMCOM_Packet), "205 != sizeof(AMCOM_Packet)");

/**
 * Type describing a callback function that will be called when a packet is received.
 *
 * @param packet packet that is received
 * @param userContext user defined context associated with the protocol receiver instance
 */
typedef void (*AMCOM_PacketHandler)(const AMCOM_Packet* packet, void* userContext);

/** Possible states of the packet reception. */
typedef enum {
	/// Packet was not started yet
	AMCOM_PACKET_STATE_EMPTY = 0,
	/// Got SOP field
	AMCOM_PACKET_STATE_GOT_SOP = 1,
	/// Got TYPE field
	AMCOM_PACKET_STATE_GOT_TYPE = 2,
	/// Got LENGTH field
	AMCOM_PACKET_STATE_GOT_LENGTH = 3,
	/// Got first byte of CRC
	AMCOM_PACKET_STATE_GOT_CRC_LO = 4,
	/// Getting payload data
	AMCOM_PACKET_STATE_GETTING_PAYLOAD = 6,
	/// Got whole packet
	AMCOM_PACKET_STATE_GOT_WHOLE_PACKET = 7
} AMCOM_PacketState;

/** Structure describing the AM packet receiver */
typedef struct {
	/// Place to store the received packet
	AMCOM_Packet receivedPacket;
	/// Counter that will be used to count the number of received payload bytes
	size_t payloadCounter;
	/// State of the packet reception
	AMCOM_PacketState receivedPacketState;
	/// User-defined packet handler (callback)
	AMCOM_PacketHandler packetHandler;
	/// User-defined context (universal, general-purpose pointer)
	void* userContext;
} AMCOM_Receiver;


/**
 * @brief Initializes the AMCOM packet receiver.
 *
 * This function shall initialize the AMCOM receiver.
 * @param receiver pointer to the AMCOM receiver structure
 * @param packetHandlerCallback callback function that will be called each time a packet is received
 * @param userContext user defined, general purpose context, that will be fed back to the callback function
 */
void AMCOM_InitReceiver(AMCOM_Receiver* receiver, AMCOM_PacketHandler packetHandlerCallback, void* userContext);

/**
 * @brief Serializes the packet
 *
 * This function should serialize the AM packet according to the given packet type and payload and store it
 * in the destination buffer as an array of bytes. The number of bytes written to the destination buffer
 * (length of the whole packet) shall be returned. In case of invalid input arguments, this function shall
 * not write anything to the destinationBuffer and return 0.
 * @param packetType type of packet
 * @param payload pointer to the payload data or NULL if the packet has no payload
 * @param payloadSize number of bytes in the payload or 0 if the packet has no payload
 * @param destinationBuffer place to store the packet bytes (must be large enough!)
 *
 * @return number of bytes written to the destinationBuffer
 *
 */
size_t AMCOM_Serialize(uint8_t packetType, const void* payload, size_t payloadSize, uint8_t* destinationBuffer);

/**
 * @brief Deserializes the chunk of data, searching for valid AMCOM packets
 *
 * This function is supposed to be fed with an incoming stream of data and it's job is to try to find a valid
 * AMCOM packet in this stream. The state of the packet reception shall be stored within the receiver structure.
 * If a valid packet is found and buffered, this function shall call the packetHandlerCallback function defined
 * through a previous call to @ref AMCOM_InitReceiver.
 * @param receiver pointer to the AMCOM receiver structure
 * @param data incoming data
 * @param dataSize number of bytes in the incoming data
 */
void AMCOM_Deserialize(AMCOM_Receiver* receiver, const void* data, size_t dataSize);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* AMCOM_H_ */

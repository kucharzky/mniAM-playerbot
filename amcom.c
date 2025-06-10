#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "amcom.h"

/// Start of packet character
const uint8_t  AMCOM_SOP         = 0xA1;
const uint16_t AMCOM_INITIAL_CRC = 0xFFFF;

static uint16_t AMCOM_UpdateCRC(uint8_t byte, uint16_t crc)
{
	byte ^= (uint8_t)(crc & 0x00ff);
	byte ^= (uint8_t)(byte << 4);
	return ((((uint16_t)byte << 8) | (uint8_t)(crc >> 8)) ^ (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3));
}


void AMCOM_InitReceiver(AMCOM_Receiver* receiver, AMCOM_PacketHandler packetHandlerCallback, void* userContext) {
	// TODO
	if(receiver==NULL){
	    return;
	}
	receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
	receiver->payloadCounter=0;
	receiver->packetHandler = packetHandlerCallback;
	receiver->userContext = userContext;
	memset(&receiver->receivedPacket, 0, sizeof(AMCOM_Packet));
}

size_t AMCOM_Serialize(uint8_t packetType, const void* payload, size_t payloadSize, uint8_t* destinationBuffer) {
	// TODO
	if(destinationBuffer == NULL){
	    return 0;
	}
	if(payloadSize>AMCOM_MAX_PAYLOAD_SIZE){
	    return 0;
	}
	if((payload==NULL) && (payloadSize>0)){
	    return 0;
	}
	AMCOM_PacketHeader* header = (AMCOM_PacketHeader*)destinationBuffer;
	//nagłowek
	header->sop = AMCOM_SOP;
	header->type = packetType;
	header->length = (uint8_t)payloadSize;
	//crc
	uint16_t crc = AMCOM_INITIAL_CRC;
	crc = AMCOM_UpdateCRC(header->type,crc);
	crc = AMCOM_UpdateCRC(header->length,crc);
	//jesli jest payload
	if(payloadSize>0){
	    const uint8_t* payloadBytes = (const uint8_t*)payload;
	    for(size_t i=0;i<payloadSize;++i){
	        crc = AMCOM_UpdateCRC(payloadBytes[i],crc);
	    }
	}
	
	header->crc = crc;
	if(payloadSize>0){
	    uint8_t* payloadDestination = destinationBuffer + sizeof(AMCOM_PacketHeader);
	    memcpy(payloadDestination,payload, payloadSize);
	}
	return sizeof(AMCOM_PacketHeader)+payloadSize;
}

void AMCOM_Deserialize(AMCOM_Receiver* receiver, const void* data, size_t dataSize) {
    // TODO
    if(receiver==NULL || data==NULL){
        return;
    }
    const uint8_t* dataBytes = (const uint8_t*)data;
    for(size_t i=0;i<dataSize;++i){
        uint8_t currentByte = dataBytes[i];
        bool packetComplete = false;
        //maszyna stanów
        switch(receiver->receivedPacketState){
            case AMCOM_PACKET_STATE_EMPTY:
                if(currentByte==AMCOM_SOP){
                    receiver->receivedPacket.header.sop=currentByte;
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_SOP;
                }
                break;
            case AMCOM_PACKET_STATE_GOT_SOP:
                receiver->receivedPacket.header.type=currentByte;
                receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_TYPE;
                break;
            case AMCOM_PACKET_STATE_GOT_TYPE:
                if(currentByte<=AMCOM_MAX_PAYLOAD_SIZE){
                    receiver->receivedPacket.header.length=currentByte;
                    receiver->payloadCounter=0;
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_LENGTH;
                }else{
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_EMPTY;
                }
                break;
            case AMCOM_PACKET_STATE_GOT_LENGTH:
                *((uint8_t*)&receiver->receivedPacket.header.crc)=currentByte;
                // if(receiver->receivedPacket.header.length==0){
                //     receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                //     packetComplete=true;
                // }else{
                //     receiver->receivedPacketState=AMCOM_PACKET_STATE_GETTING_PAYLOAD;
                // }
                receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_CRC_LO;
                break;
            case AMCOM_PACKET_STATE_GOT_CRC_LO:
                *(((uint8_t*)&receiver->receivedPacket.header.crc) + 1) = currentByte;
                if (receiver->receivedPacket.header.length == 0) {
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                    packetComplete = true;
                }else{
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GETTING_PAYLOAD;
                }
                break;
            case AMCOM_PACKET_STATE_GETTING_PAYLOAD:
                receiver->receivedPacket.payload[receiver->payloadCounter]=currentByte;
                receiver->payloadCounter++;
                if(receiver->payloadCounter==receiver->receivedPacket.header.length){
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                    packetComplete=true;
                }
                break;
            case AMCOM_PACKET_STATE_GOT_WHOLE_PACKET:
            default:
                if(currentByte==AMCOM_SOP){
                    receiver->receivedPacket.header.sop=currentByte;
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_GOT_SOP;
                }else{
                    receiver->receivedPacketState=AMCOM_PACKET_STATE_EMPTY;
                }
                break;
        }
        if(packetComplete){
            uint16_t calculated_crc = AMCOM_INITIAL_CRC;
            calculated_crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.type,calculated_crc);
            calculated_crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.length,calculated_crc);
            for(size_t j = 0;j<receiver->receivedPacket.header.length;++j){
                calculated_crc = AMCOM_UpdateCRC(receiver->receivedPacket.payload[j],calculated_crc);
            }
            if(calculated_crc==receiver->receivedPacket.header.crc){
                if(receiver->packetHandler!=NULL){
                    receiver->packetHandler(&receiver->receivedPacket,receiver->userContext);
                }
            }
            receiver->receivedPacketState=AMCOM_PACKET_STATE_EMPTY;
            receiver->payloadCounter=0;
        }
    }
}

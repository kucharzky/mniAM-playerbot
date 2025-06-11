#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "amcom.h"
#include "amcom_packets.h"
// My includes
#include <float.h>
#include <math.h>
#include <time.h>

typedef struct {
    float x, y;
} SparkPosition;

// Structure for managing game state
typedef struct {
    AMCOM_ObjectState objects[AMCOM_MAX_OBJECT_UPDATES];
    uint8_t objectCount;
    float currentTargetAngle;
    float currentTargetScore;
    uint32_t currentGameTime;
    uint8_t myPlayerNumber;
    float mapWidth, mapHeight;
    bool gameActive;
    SparkPosition storedSparks[8];
    uint8_t sparkCount;
} GameState;

// Global game state variable
GameState gameState = {0};

void removeDeadObjects(){
    for(uint8_t i = 0; i < gameState.objectCount; i++) {
        if (gameState.objects[i].hp <= 0) {
            printf("Removing dead object %d of type %d\n", gameState.objects[i].objectNo, gameState.objects[i].objectType);
            // Shift remaining objects down
            for (uint8_t j = i; j < gameState.objectCount - 1; j++) {
                gameState.objects[j] = gameState.objects[j + 1];
            }
            gameState.objectCount--;
            i--; // Adjust index after removal
        }
    }
}

void storeNearestSparks() {
    gameState.sparkCount = 0;
    for(int i = 0; i < gameState.objectCount && gameState.sparkCount < 5; i++) {
        if(gameState.objects[i].objectType == 2) { // Spark
            gameState.storedSparks[gameState.sparkCount].x = gameState.objects[i].x;
            gameState.storedSparks[gameState.sparkCount].y = gameState.objects[i].y;
            gameState.sparkCount++;
            printf("Stored spark at (%.1f, %.1f)\n", 
                   gameState.objects[i].x, gameState.objects[i].y);
        }
    }
    printf("Total sparks stored: %d\n", gameState.sparkCount);
}

float calculateThreatScore(float targetX, float targetY) {
    float threat = 0.0f;
    for(int i = 0; i < gameState.objectCount; i++) {
        if(gameState.objects[i].objectType == 0 && 
           gameState.objects[i].objectNo != gameState.myPlayerNumber &&
           gameState.objects[i].hp > 0) {
            
            float dx = targetX - gameState.objects[i].x;
            float dy = targetY - gameState.objects[i].y;
            float distToThreat = sqrtf(dx*dx + dy*dy);
            
            if(distToThreat < 100.0f) {
                threat += (gameState.objects[i].hp / 10.0f) * (1.0f - (distToThreat / 100.0f));
                printf("Threat from player %d at distance %.1f: +%.2f\n", 
                       gameState.objects[i].objectNo, distToThreat, threat);
            }
        }
    }
    return threat;
}

float avoidSparkTrajectory(float targetX, float targetY, float myX, float myY) {
    float baseAngle = atan2f(targetY - myY, targetX - myX);
    
    for(int i = 0; i < gameState.sparkCount; i++) {
        float sparkX = gameState.storedSparks[i].x;
        float sparkY = gameState.storedSparks[i].y;
        
        float dx = sparkX - myX;
        float dy = sparkY - myY;
        float distToSpark = sqrtf(dx*dx + dy*dy);
        
        if(distToSpark < 100.0f) {
            float sparkAngle = atan2f(dy, dx);
            float angleDiff = fabsf(sparkAngle - baseAngle);
            
            if(angleDiff < M_PI/3) { // 60 stopni tolerancji
                printf("AVOIDING SPARK at (%.1f, %.1f), distance=%.1f!\n", 
                       sparkX, sparkY, distToSpark);
                
                // Omijaj iskrę
                if(sparkAngle > baseAngle) {
                    baseAngle -= M_PI/2; // 90 stopni w lewo
                } else {
                    baseAngle += M_PI/2; // 90 stopni w prawo
                }
                
                printf("Adjusted angle to: %.2f rad (%.1f degrees)\n", 
                       baseAngle, baseAngle * 180.0f / M_PI);
                break;
            }
        }
    }
    
    return baseAngle;
}

void processObjectUpdate(const AMCOM_Packet* packet) {
    uint8_t objectCount = packet->header.length / sizeof(AMCOM_ObjectState);
    if(objectCount == 0) return;

    AMCOM_ObjectUpdateRequestPayload* updatePayload = (AMCOM_ObjectUpdateRequestPayload*)packet->payload;
    printf("Processing OBJECT_UPDATE.request with %d objects:\n", objectCount);
    for(uint8_t i = 0; i < objectCount; i++) {
        AMCOM_ObjectState* obj = &updatePayload->objectState[i];
        printf("  Object %d: Type=%d, No=%d, HP=%d, Pos=(%.1f,%.1f)\n", i, obj->objectType, obj->objectNo, obj->hp, obj->x, obj->y);
        bool found = false;
        // Check if this object already exists in the game state
        for (uint8_t j = 0; j < gameState.objectCount; j++) {
            if (gameState.objects[j].objectNo == obj->objectNo && 
                gameState.objects[j].objectType == obj->objectType) {
                // Update existing object
                gameState.objects[j] = *obj;
                found = true;
                printf("  Updated existing object %d\n", obj->objectNo);
                break;
            }
        }
        // Update game state
        if(!found && gameState.objectCount < AMCOM_MAX_OBJECT_UPDATES){
            gameState.objects[gameState.objectCount] = *obj;
            gameState.objectCount++;
            printf("  Added new object %d\n", obj->objectNo);
        }
    }
    printf("Total objects in game state: %d\n", gameState.objectCount);
    // Print all objects in game state
    for (uint8_t i = 0; i < gameState.objectCount; i++) {
        printf("  Object %d: Type=%d, No=%d, HP=%d, Pos=(%.1f,%.1f)\n",
               i, gameState.objects[i].objectType, gameState.objects[i].objectNo,
               gameState.objects[i].hp, gameState.objects[i].x, gameState.objects[i].y);
    }
    storeNearestSparks();
    removeDeadObjects();
}

float calculateMovement() {
    if (!gameState.gameActive || gameState.objectCount == 0) {
        return 0.0f; // Stay in place
    }
    storeNearestSparks();
    
    // Simple random movement for testing
    static bool seedInitialized = false;
    
    // Initialize random seed only once
    if (!seedInitialized) {
        srand((unsigned int)time(NULL));
        seedInitialized = true;
    }    
    
    // My player position (objectType == 0, objectNo == myPlayerNumber)
    float myX = 0.0f, myY = 0.0f;
    bool foundMyself = false;
    
    printf("Looking for my player (number %d) among %d objects:\n", 
           gameState.myPlayerNumber, gameState.objectCount);
    
    for (uint8_t i = 0; i < gameState.objectCount; i++) {
        printf("  Object %d: Type=%d, ObjectNo=%d, HP=%d, Pos=(%.1f,%.1f)\n",
               i, gameState.objects[i].objectType, gameState.objects[i].objectNo,
               gameState.objects[i].hp, gameState.objects[i].x, gameState.objects[i].y);
               
        if (gameState.objects[i].objectType == 0 && 
            gameState.objects[i].objectNo == gameState.myPlayerNumber) {
            myX = gameState.objects[i].x;
            myY = gameState.objects[i].y;
            foundMyself = true;
            printf("Found myself at position (%.1f, %.1f)\n", myX, myY);
        }
    }
    
    if (!foundMyself) {
        printf("Could not find my player!\n");
        // // Generate random angle between 0 and 2*PI radians
        // float randomAngle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
        // printf("Random movement angle: %.2f rad (%.1f degrees)\n", randomAngle, randomAngle * 180.0f / M_PI);
        // return randomAngle;
    }
    
    // Find nearest transistor (objectType == 1)
    float bestAngle = 0.0f;
    float minDistance = FLT_MAX;
    bool foundTransistor = false;
    for (uint8_t i = 0; i < gameState.objectCount; i++) {
        if (gameState.objects[i].objectType == 1) { // Transistor
            float dx = gameState.objects[i].x - myX;
            float dy = gameState.objects[i].y - myY;
            float distance = sqrtf(dx*dx + dy*dy);
            
            printf("Transistor at (%.1f, %.1f), distance: %.1f\n", 
                   gameState.objects[i].x, gameState.objects[i].y, distance);
            
            if (distance < minDistance) {
                minDistance = distance;
                bestAngle = avoidSparkTrajectory(gameState.objects[i].x, gameState.objects[i].y, myX, myY);
                foundTransistor = true;
            }
        }
    }
    
    if (foundTransistor) {
        printf("Moving towards nearest transistor: angle %.2f rad (%.1f degrees), distance: %.1f\n", bestAngle, bestAngle * 180.0f / M_PI, minDistance);
        return bestAngle;
    } else {
        printf("No transistors found!\n");
        // Generate random angle if no transistors available
        // float randomAngle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
        // printf("Random movement angle: %.2f rad (%.1f degrees)\n", randomAngle, randomAngle * 180.0f / M_PI);
        // return randomAngle;
    }
}

void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t buf[AMCOM_MAX_PACKET_SIZE];              // buffer used to serialize outgoing packets
    size_t toSend = 0;                               // size of the outgoing packet
    SOCKET ConnectSocket  = *((SOCKET*)userContext); // socket used for communication with the server

    switch (packet->header.type) {
    case AMCOM_IDENTIFY_REQUEST:
        printf("Got IDENTIFY.request. Responding with IDENTIFY.response\n");
        AMCOM_IdentifyResponsePayload identifyResponse;
        sprintf(identifyResponse.playerName, "Tata Gimpera");
        toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
        break;
    case AMCOM_NEW_GAME_REQUEST:
        printf("Got NEW_GAME.request.\n");
        // Extract game parameters
        AMCOM_NewGameRequestPayload* newGameReq = (AMCOM_NewGameRequestPayload*)packet->payload;
        gameState.myPlayerNumber = newGameReq->playerNumber;
        gameState.mapWidth = newGameReq->mapWidth;
        gameState.mapHeight = newGameReq->mapHeight;
        gameState.gameActive = true;
        printf("Player number: %d, Map: %.1fx%.1f\n", gameState.myPlayerNumber, gameState.mapWidth, gameState.mapHeight);

        AMCOM_NewGameResponsePayload newGameResponse;
        sprintf(newGameResponse.helloMessage,"Czesc cwelu!");
        toSend = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newGameResponse, sizeof(newGameResponse), buf);
        break;
    case AMCOM_OBJECT_UPDATE_REQUEST:
        printf("Got OBJECT_UPDATE.request\n");
        // procces object
        processObjectUpdate(packet);
        break;
    case AMCOM_MOVE_REQUEST:
        printf("Got MOVE.request\n");
        AMCOM_MoveRequestPayload* moveReq = (AMCOM_MoveRequestPayload*)packet->payload;
        gameState.currentGameTime = moveReq->gameTime;
        AMCOM_MoveResponsePayload moveResponse;
        moveResponse.angle = calculateMovement();
        toSend = AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &moveResponse, sizeof(moveResponse), buf);
        break;
    case AMCOM_GAME_OVER_REQUEST:
        printf("Got GAME_OVER.request\n");
        gameState.gameActive = false;
        AMCOM_GameOverResponsePayload gameOverResponse;
        sprintf(gameOverResponse.endMessage, "HO HO HO HOP MNIE RUCHA W DUPE");
        toSend = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &gameOverResponse, sizeof(gameOverResponse), buf);
        break;
    default:
        printf("Unknown packet type: %d\n", packet->header.type);
        break;
    }

	// if there is something to send back - do it
	if (toSend > 0) {
		int bytesSent = send(ConnectSocket, (const char*)buf, toSend, 0 );
		if (bytesSent == SOCKET_ERROR) {
			printf("Socket send failed with error: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return;
		}
	}
}


#define GAME_SERVER "localhost"
#define GAME_SERVER_PORT "2001"
//#define GAME_SERVER "87.207.94.83"


int main(int argc, char **argv) {
    printf("This is mniAM player. Let's eat some transistors! \n");

    WSADATA wsaData;
    int iResult;

    // Initialize Winsock library (windows sockets)
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    // Prepare temporary data
    SOCKET ConnectSocket  = INVALID_SOCKET;
    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;
    int iSendResult;
    char recvbuf[512];
    int recvbuflen = sizeof(recvbuf);

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the game server address and port
    iResult = getaddrinfo(GAME_SERVER, GAME_SERVER_PORT, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    printf("Connecting to game server...\n");
    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
                ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }
    // Free some used resources
    freeaddrinfo(result);

    // Check if we connected to the game server
    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to the game server!\n");
        WSACleanup();
        return 1;
    } else {
        printf("Connected to game server\n");
    }

    AMCOM_Receiver amReceiver;
    AMCOM_InitReceiver(&amReceiver, amPacketHandler, &ConnectSocket);

    // Receive until the peer closes the connection
    do {

        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if ( iResult > 0 ) {
            AMCOM_Deserialize(&amReceiver, recvbuf, iResult);
        } else if ( iResult == 0 ) {
            printf("Connection closed\n");
        } else {
            printf("recv failed with error: %d\n", WSAGetLastError());
        }

    } while( iResult > 0 );

    // No longer need the socket
    closesocket(ConnectSocket);
    // Clean up
    WSACleanup();

    return 0;
}

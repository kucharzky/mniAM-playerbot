#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "amcom.h"
#include "amcom_packets.h"

// My includes
#include <time.h>
#include <float.h>
#include <stdbool.h>

// Structure for managing game state
typedef struct {
    // Player data
    AMCOM_ObjectState players[10];
    uint8_t playerCount;
    
    // Transistors (food)
    AMCOM_ObjectState transistors[100];
    uint8_t transistorCount;
    
    // Sparks (moving threats)
    AMCOM_ObjectState sparks[20];
    uint8_t sparkCount;
    
    // Glue (static obstacles)
    AMCOM_ObjectState glue[10];
    uint8_t glueCount;
    
    // Game state
    uint32_t currentGameTime;
    uint8_t myPlayerNumber;
    float mapWidth, mapHeight;
    bool gameActive;
    
    // My player cache
    float myX, myY, myHP;
    bool myPlayerFound;

    uint8_t konamiIndex;
} GameState;

// Global game state variable
GameState gameState = {0};

float normalizeAngle(float angle) {
    // Normalize angle to [0, 2PI)
    while(angle < 0) angle += 2 * M_PI;
    while(angle >= 2 * M_PI) angle -= 2 * M_PI;
    return angle;
}

void updatePlayerList(AMCOM_ObjectState* newPlayer) {
    bool found = false;
    
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].objectNo == newPlayer->objectNo) {
            gameState.players[i] = *newPlayer;
            found = true;
            break;
        }
    }
    
    if(!found && gameState.playerCount < 10) {
        gameState.players[gameState.playerCount] = *newPlayer;
        gameState.playerCount++;
    }
    
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].hp <= 0) {
            for(uint8_t j = i; j < gameState.playerCount - 1; j++) {
                gameState.players[j] = gameState.players[j + 1];
            }
            gameState.playerCount--;
            i--;
        }
    }
}

void updateTransistorList(AMCOM_ObjectState* newTransistor) {
    bool found = false;
    
    for(uint8_t i = 0; i < gameState.transistorCount; i++) {
        if(gameState.transistors[i].objectNo == newTransistor->objectNo) {
            gameState.transistors[i] = *newTransistor;
            found = true;
            break;
        }
    }
    
    if(!found && gameState.transistorCount < 100) {
        gameState.transistors[gameState.transistorCount] = *newTransistor;
        gameState.transistorCount++;
    }
}

void updateSparkList(AMCOM_ObjectState* newSpark) {
    bool found = false;
    
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        if(gameState.sparks[i].objectNo == newSpark->objectNo) {
            gameState.sparks[i] = *newSpark;
            found = true;
            break;
        }
    }
    
    if(!found && gameState.sparkCount < 20) {
        gameState.sparks[gameState.sparkCount] = *newSpark;
        gameState.sparkCount++;
    }
}

void updateGlueList(AMCOM_ObjectState* newGlue) {
    bool found = false;
    
    for(uint8_t i = 0; i < gameState.glueCount; i++) {
        if(gameState.glue[i].objectNo == newGlue->objectNo) {
            gameState.glue[i] = *newGlue;
            found = true;
            break;
        }
    }
    
    if(!found && gameState.glueCount < 10) {
        gameState.glue[gameState.glueCount] = *newGlue;
        gameState.glueCount++;
    }
}

void updateMyPlayerCache() {
    gameState.myPlayerFound = false;
    
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].objectNo == gameState.myPlayerNumber) {
            gameState.myX = gameState.players[i].x;
            gameState.myY = gameState.players[i].y;
            gameState.myHP = gameState.players[i].hp;
            gameState.myPlayerFound = true;
            break;
        }
    }
}

float avoidSparkTrajectory(float targetX, float targetY) {
    float baseAngle = atan2f(targetY - gameState.myY, targetX - gameState.myX);
    
    // Check sparks from dedicated spark storage
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        float sparkX = gameState.sparks[i].x;
        float sparkY = gameState.sparks[i].y;
        float dx = sparkX - gameState.myX;
        float dy = sparkY - gameState.myY;
        float distToSpark = sqrtf(dx*dx + dy*dy);
        
        if(distToSpark < 50+25+gameState.myHP) {
            float sparkAngle = atan2f(dy, dx);
            float angleDiff = fabsf(sparkAngle - baseAngle);
            
            if(angleDiff < M_PI/3) { // 60 degrees tolerance
                printf("AVOIDING SPARK at (%.1f, %.1f), distance=%.1f!\n", 
                       sparkX, sparkY, distToSpark);
                
                // Avoid spark
                if(sparkAngle > baseAngle) {
                    baseAngle -= M_PI/2; // 90 degrees left
                } else {
                    baseAngle += M_PI/2; // 90 degrees right
                }
                
                printf("Adjusted angle to: %.2f rad (%.1f degrees)\n", 
                       baseAngle, baseAngle * 180.0f / M_PI);
                break;
            }
        }
    }
    
    return baseAngle;
}

float getDanceAngle() {
    
    // konami Code sequence: Up, Up, Down, Down, Left, Right, Left, Right
    float konamiSequence[8] = {
        3 * M_PI / 2,  // Up
        3 * M_PI / 2,  // Up
        M_PI / 2,      // Down
        M_PI / 2,      // Down
        M_PI,          // Left
        0,             // Right
        M_PI,          // Left
        0              // Right
    };
    
    float angle = konamiSequence[gameState.konamiIndex];
    gameState.konamiIndex = (gameState.konamiIndex + 1) % 8;
    
    printf("KONAMI DANCE: Step %d, angle %.2f rad (%.1f degrees)\n", 
           gameState.konamiIndex, angle, angle * 180.0f / M_PI);
    
    return angle;
}

float calculateMovement() {
    if (!gameState.gameActive || !gameState.myPlayerFound) {
        return 0.0f;
    }
    
    const float distance_scale = sqrtf(pow(gameState.mapHeight,2) + pow(gameState.mapWidth,2));

    float dangerX = 0, dangerY = 0, dangerScore = 0;
    float foodX = 0, foodY = 0, foodScore = 0;
    float huntX = 0, huntY = 0, huntScore = 0;
    float sparkX = 0, sparkY = 0, sparkScore = 0;
    float attackX = 0, attackY = 0, attackScore = 0;
    
    printf("My position: (%.1f, %.1f), HP: %.1f\n", gameState.myX, gameState.myY, gameState.myHP);
    
    // --- Players ---
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].objectNo == gameState.myPlayerNumber || gameState.players[i].hp <= 0) continue;
        
        float dx = gameState.players[i].x - gameState.myX;
        float dy = gameState.players[i].y - gameState.myY;
        float distance = sqrtf(dx*dx + dy*dy);
        
        if(gameState.players[i].hp > gameState.myHP) {
            if(distance > 100+25+gameState.myHP) continue;
            
            // score for dangerous player
            float score = gameState.players[i].hp / (distance / distance_scale);
            
            if(score > dangerScore) {
                dangerX = gameState.players[i].x;
                dangerY = gameState.players[i].y;
                dangerScore = score;
            }
        } else if(gameState.myHP > gameState.players[i].hp) {
            // check for attack opportunity within 150 distance
            if(distance <= 150.0f) {
                // attack target found - higher priority than hunting
                float score = gameState.myHP / (distance / distance_scale);
                
                if(score > attackScore) {
                    attackX = gameState.players[i].x;
                    attackY = gameState.players[i].y;
                    attackScore = score;
                }
            }
            
            // potential hunt - is glue on path (for longer distances)
            float adjustedDistance = distance;
            
            // glue check
            for(uint8_t j = 0; j < gameState.glueCount; j++) {
                if(gameState.glue[j].hp <= 0) continue;
                
                // angle to glue
                float glueX = gameState.glue[j].x - gameState.myX;
                float glueY = gameState.glue[j].y - gameState.myY;
                float glueDistance = sqrtf(glueX*glueX + glueY*glueY) - 100.0f; // 100 is glue radius
                
                if(glueDistance < distance) {
                    float glueAngle = atan2f(100.0f, glueDistance);
                    float targetAngle = atan2f(dy, dx);
                    float glueTargetAngle = atan2f(glueY, glueX);
                    
                    if(targetAngle < glueTargetAngle + glueAngle && 
                       targetAngle > glueTargetAngle - glueAngle) {
                        adjustedDistance = distance * 20.0f; // glue penalty
                        break;
                    }
                }
            }
            
            // score for hunt
            float score = gameState.myHP / (adjustedDistance / distance_scale);
            
            if(score > huntScore) {
                huntX = gameState.players[i].x;
                huntY = gameState.players[i].y;
                huntScore = score;
            }
        }
    }
    
    // --- Sparks ---
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        if(gameState.sparks[i].hp <= 0) continue;
        
        float dx = gameState.sparks[i].x - gameState.myX;
        float dy = gameState.sparks[i].y - gameState.myY;
        float distance = sqrtf(dx*dx + dy*dy);
        
        if(distance > 20+25+gameState.myHP) continue;
        
        // score for spark
        float score = gameState.sparks[i].hp / (distance / distance_scale);
        
        if(score > sparkScore) {
            sparkX = gameState.sparks[i].x;
            sparkY = gameState.sparks[i].y;
            sparkScore = score;
        }
    }
    
    // --- Transistors ---
    for(uint8_t i = 0; i < gameState.transistorCount; i++) {
        if(gameState.transistors[i].hp <= 0) continue;
        
        float dx = gameState.transistors[i].x - gameState.myX;
        float dy = gameState.transistors[i].y - gameState.myY;
        float distance = sqrtf(dx*dx + dy*dy);
        float adjustedDistance = distance;
        
        // check glue when search for food
        for(uint8_t j = 0; j < gameState.glueCount; j++) {
            if(gameState.glue[j].hp <= 0) continue;
            
            // angle to glue
            float glueX = gameState.glue[j].x - gameState.myX;
            float glueY = gameState.glue[j].y - gameState.myY;
            float glueDistance = sqrtf(glueX*glueX + glueY*glueY) - 100.0f; // 100 is glue radius
            
            if(glueDistance < distance) {
                float glueAngle = atan2f(100.0f, glueDistance);
                float targetAngle = atan2f(dy, dx);
                float glueTargetAngle = atan2f(glueY, glueX);
                
                if(targetAngle < glueTargetAngle + glueAngle && 
                   targetAngle > glueTargetAngle - glueAngle) {
                    adjustedDistance = distance * 20.0f; // glue penalty
                    break;
                }
            }
        }
        
        // score for food
        float score = gameState.transistors[i].hp / (adjustedDistance / distance_scale);
        
        if(score > foodScore) {
            foodX = gameState.transistors[i].x;
            foodY = gameState.transistors[i].y;
            foodScore = score;
        }
    }
    
    float angle = 0.0f;
    
    if(dangerScore > 0) {
        float escapeX = -dangerY + gameState.myY + gameState.myX;
        float escapeY = dangerX - gameState.myX + gameState.myY;
        angle = avoidSparkTrajectory(escapeX, escapeY);
        printf("ESCAPING from dangerous player at (%.1f, %.1f)\n", dangerX, dangerY);
    }else if(sparkScore > 0) {
        angle = atan2f(-(sparkY - gameState.myY), -(sparkX - gameState.myX));
        printf("AVOIDING spark at (%.1f, %.1f)\n", sparkX, sparkY);
    }else if(attackScore > 0) {
        // attack nearby weak player
        angle = avoidSparkTrajectory(attackX, attackY);
        printf("ATTACKING weak player at (%.1f, %.1f), score=%.2f\n", attackX, attackY, attackScore);
    }else if(foodScore > 0) {
        angle = avoidSparkTrajectory(foodX, foodY);
        printf("COLLECTING food at (%.1f, %.1f), score=%.2f\n", foodX, foodY, foodScore);
    }else if(huntScore > 0) {
        angle = avoidSparkTrajectory(huntX, huntY);
        printf("HUNTING at (%.1f, %.1f), score=%.2f\n", huntX, huntY, huntScore);
    }else {
        angle = getDanceAngle();
        printf("NO TARGETS - Performing Konami Code dance!\n");
    }
    
    angle = normalizeAngle(angle);
    
    printf("Final angle: %.2f rad (%.1f degrees)\n", angle, angle * 180.0f / M_PI);
    return angle;
}

void processObjectUpdate(const AMCOM_Packet* packet) {
    uint8_t objectCount = packet->header.length / sizeof(AMCOM_ObjectState);
    if(objectCount == 0) return;
    
    AMCOM_ObjectUpdateRequestPayload* updatePayload = (AMCOM_ObjectUpdateRequestPayload*)packet->payload;
    
    for(uint8_t i = 0; i < objectCount; i++) {
        AMCOM_ObjectState* obj = &updatePayload->objectState[i];
        
        switch(obj->objectType) {
            case 0: // Players
                updatePlayerList(obj);
                break;
            case 1: // Transistors
                updateTransistorList(obj);
                break;
            case 2: // Sparks
                updateSparkList(obj);
                break;
            case 3: // Glue
                updateGlueList(obj);
                break;
        }
    }
    updateMyPlayerCache();
}

void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t buf[AMCOM_MAX_PACKET_SIZE];
    size_t toSend = 0;
    SOCKET ConnectSocket = *((SOCKET*)userContext);

    switch (packet->header.type) {
    case AMCOM_IDENTIFY_REQUEST:
        printf("Got IDENTIFY.request. Responding with IDENTIFY.response\n");
        AMCOM_IdentifyResponsePayload identifyResponse;
        sprintf(identifyResponse.playerName, "sAMobujca");
        toSend = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, sizeof(identifyResponse), buf);
        break;
        
    case AMCOM_NEW_GAME_REQUEST:
        printf("Got NEW_GAME.request.\n");
        AMCOM_NewGameRequestPayload* newGameReq = (AMCOM_NewGameRequestPayload*)packet->payload;
        gameState.myPlayerNumber = newGameReq->playerNumber;
        gameState.mapWidth = newGameReq->mapWidth;
        gameState.mapHeight = newGameReq->mapHeight;
        gameState.gameActive = true;
        
        printf("Player number: %d, Map: %.1fx%.1f\n", 
               gameState.myPlayerNumber, gameState.mapWidth, gameState.mapHeight);
        
        AMCOM_NewGameResponsePayload newGameResponse;
        sprintf(newGameResponse.helloMessage, "Bedzie magik i to za dwa lata");
        toSend = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newGameResponse, sizeof(newGameResponse), buf);
        break;
        
    case AMCOM_OBJECT_UPDATE_REQUEST:
        processObjectUpdate(packet);
        break;
        
    case AMCOM_MOVE_REQUEST:
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
        sprintf(gameOverResponse.endMessage, "GG WP!");
        toSend = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &gameOverResponse, sizeof(gameOverResponse), buf);
        break;
        
    default:
        printf("Unknown packet type: %d\n", packet->header.type);
        break;
    }

    if (toSend > 0) {
        int bytesSent = send(ConnectSocket, (const char*)buf, toSend, 0);
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
    
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;
    char recvbuf[512];
    int recvbuflen = sizeof(recvbuf);

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    iResult = getaddrinfo(GAME_SERVER, GAME_SERVER_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    printf("Connecting to game server...\n");
    
    for(ptr=result; ptr != NULL; ptr=ptr->ai_next) {
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);
    
    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to the game server!\n");
        WSACleanup();
        return 1;
    } else {
        printf("Connected to game server\n");
    }

    AMCOM_Receiver amReceiver;
    AMCOM_InitReceiver(&amReceiver, amPacketHandler, &ConnectSocket);
    
    do {
        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
            AMCOM_Deserialize(&amReceiver, recvbuf, iResult);
        } else if (iResult == 0) {
            printf("Connection closed\n");
        } else {
            printf("recv failed with error: %d\n", WSAGetLastError());
        }
    } while(iResult > 0);

    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}

#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include "amcom.h"
#include "amcom_packets.h"

// Game configuration constants
#define MAX_PLAYERS 10
#define MAX_TRANSISTORS 100
#define MAX_SPARKS 20
#define MAX_GLUE_SPOTS 10

// Game mechanics constants
#define PLAYER_BASE_RADIUS 25          // Base player collision radius
#define SPARK_BASE_RADIUS 25           // Base spark collision radius  
#define GLUE_RADIUS 100                // Glue area effect radius
#define DANGER_DETECTION_RANGE 100     // Range to detect dangerous players
#define ATTACK_RANGE 150               // Range for attacking weaker players
#define SPARK_DETECTION_RANGE 20       // Range to detect threatening sparks
#define SPARK_AVOIDANCE_RADIUS 50      // Safety distance from sparks
#define GLUE_MOVEMENT_PENALTY 20.0f    // Movement speed penalty in glue
#define SPARK_AVOIDANCE_ANGLE (M_PI/3) // 60 degrees tolerance for spark detection
#define EVASION_ANGLE (M_PI/2)         // 90 degrees turn for evasion

/**
 * Game state structure containing all game objects and player information
 */
typedef struct {
    // Game objects storage - separated by type for efficient access
    AMCOM_ObjectState players[MAX_PLAYERS];        // All players on the map
    uint8_t playerCount;                           // Current number of active players
    
    AMCOM_ObjectState transistors[MAX_TRANSISTORS]; // Food objects (+HP when collected)
    uint8_t transistorCount;                       // Current number of transistors
    
    AMCOM_ObjectState sparks[MAX_SPARKS];          // Dangerous moving objects (-3 HP)
    uint8_t sparkCount;                            // Current number of sparks
    
    AMCOM_ObjectState glue[MAX_GLUE_SPOTS];        // Slow zones (20x movement penalty)
    uint8_t glueCount;                             // Current number of glue spots
    
    // Game session information
    uint32_t currentGameTime;                      // Server game time
    uint8_t myPlayerNumber;                        // Our player identifier
    float mapWidth, mapHeight;                     // Map dimensions
    bool gameActive;                               // Game session status
    
    // Cached player data for performance optimization
    float myX, myY, myHP;                         // Our current position and health
    bool myPlayerFound;                           // Flag indicating if we found ourselves
    
    // Entertainment feature
    uint8_t konamiIndex;                          // Current step in Konami Code dance
} GameState;

// Global game state
GameState gameState = {0};

/**
 * Normalizes angle to range [0, 2π) for consistent direction calculations
 * @param angle Input angle in radians
 * @return Normalized angle in range [0, 2π)
 */
float normalizeAngle(float angle) {
    while(angle < 0) angle += 2 * M_PI;
    while(angle >= 2 * M_PI) angle -= 2 * M_PI;
    return angle;
}

/**
 * Updates player list with new player data, removing dead players
 * @param newPlayer Pointer to new player data from server
 */
void updatePlayerList(AMCOM_ObjectState* newPlayer) {
    bool playerExists = false;
    
    // Search for existing player to update
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].objectNo == newPlayer->objectNo) {
            gameState.players[i] = *newPlayer;
            playerExists = true;
            break;
        }
    }
    
    // Add new player if not found and space available
    if(!playerExists && gameState.playerCount < MAX_PLAYERS) {
        gameState.players[gameState.playerCount] = *newPlayer;
        gameState.playerCount++;
    }
    
    // Remove dead players (HP <= 0) from active list
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        if(gameState.players[i].hp <= 0) {
            // Shift remaining players to fill gap
            for(uint8_t j = i; j < gameState.playerCount - 1; j++) {
                gameState.players[j] = gameState.players[j + 1];
            }
            gameState.playerCount--;
            i--; // Recheck current index after shift
        }
    }
}

/**
 * Updates transistor list with new data
 * @param newTransistor Pointer to transistor data from server
 */
void updateTransistorList(AMCOM_ObjectState* newTransistor) {
    bool transistorExists = false;
    
    // Search for existing transistor to update
    for(uint8_t i = 0; i < gameState.transistorCount; i++) {
        if(gameState.transistors[i].objectNo == newTransistor->objectNo) {
            gameState.transistors[i] = *newTransistor;
            transistorExists = true;
            break;
        }
    }
    
    // Add new transistor if not found
    if(!transistorExists && gameState.transistorCount < MAX_TRANSISTORS) {
        gameState.transistors[gameState.transistorCount] = *newTransistor;
        gameState.transistorCount++;
    }
}

/**
 * Updates spark list with current spark positions
 * @param newSpark Pointer to spark data from server
 */
void updateSparkList(AMCOM_ObjectState* newSpark) {
    bool sparkExists = false;
    
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        if(gameState.sparks[i].objectNo == newSpark->objectNo) {
            gameState.sparks[i] = *newSpark;
            sparkExists = true;
            break;
        }
    }
    
    if(!sparkExists && gameState.sparkCount < MAX_SPARKS) {
        gameState.sparks[gameState.sparkCount] = *newSpark;
        gameState.sparkCount++;
    }
}

/**
 * Updates glue spot list with current glue positions
 * @param newGlue Pointer to glue data from server
 */
void updateGlueList(AMCOM_ObjectState* newGlue) {
    bool glueExists = false;
    
    for(uint8_t i = 0; i < gameState.glueCount; i++) {
        if(gameState.glue[i].objectNo == newGlue->objectNo) {
            gameState.glue[i] = *newGlue;
            glueExists = true;
            break;
        }
    }
    
    if(!glueExists && gameState.glueCount < MAX_GLUE_SPOTS) {
        gameState.glue[gameState.glueCount] = *newGlue;
        gameState.glueCount++;
    }
}

/**
 * Caches our player's current position and HP for quick access
 * Called after each object update to maintain current state
 */
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

/**
 * Calculates safe movement angle avoiding sparks on trajectory
 * @param targetX Target X coordinate
 * @param targetY Target Y coordinate
 * @return Safe movement angle in radians, adjusted to avoid sparks
 */
float avoidSparkTrajectory(float targetX, float targetY) {
    // Calculate direct angle to target
    float baseAngle = atan2f(targetY - gameState.myY, targetX - gameState.myX);
    
    // Check each spark for collision risk
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        float sparkX = gameState.sparks[i].x;
        float sparkY = gameState.sparks[i].y;
        float dx = sparkX - gameState.myX;
        float dy = sparkY - gameState.myY;
        float distanceToSpark = sqrtf(dx*dx + dy*dy);
        
        // Check if spark is within danger zone (spark radius + player radius + safety margin)
        float dangerRadius = SPARK_AVOIDANCE_RADIUS + PLAYER_BASE_RADIUS + gameState.myHP;
        if(distanceToSpark < dangerRadius) {
            float sparkAngle = atan2f(dy, dx);
            float angleDifference = fabsf(sparkAngle - baseAngle);
            
            // If spark is roughly in our path (within 60 degrees)
            if(angleDifference < SPARK_AVOIDANCE_ANGLE) {
                printf("AVOIDING SPARK at (%.1f, %.1f), distance=%.1f!\n", 
                       sparkX, sparkY, distanceToSpark);
                
                // Turn 90 degrees away from spark
                if(sparkAngle > baseAngle) {
                    baseAngle -= EVASION_ANGLE; // Turn left
                } else {
                    baseAngle += EVASION_ANGLE; // Turn right
                }
                
                printf("Adjusted angle to: %.2f rad (%.1f degrees)\n", 
                       baseAngle, baseAngle * 180.0f / M_PI);
                break; // Only avoid first detected spark
            }
        }
    }
    
    return baseAngle;
}

/**
 * Provides entertainment movement when no targets are available
 * @return Next angle in the dance sequence
 */
float getDanceAngle() {
    // Konami Code directions mapped to angles
    float konamiSequence[8] = {
        3 * M_PI / 2,  // Up (270°)
        3 * M_PI / 2,  // Up (270°)
        M_PI / 2,      // Down (90°)
        M_PI / 2,      // Down (90°)
        M_PI,          // Left (180°)
        0,             // Right (0°)
        M_PI,          // Left (180°)
        0              // Right (0°)
    };
    
    float angle = konamiSequence[gameState.konamiIndex];
    gameState.konamiIndex = (gameState.konamiIndex + 1) % 8; // Cycle through sequence
    
    return angle;
}

/**
 * Main decision-making function
 * Analyzes game state and determines optimal movement direction
 * Priority order: Escape > Avoid Sparks > Attack > Collect Food > Hunt > Dance
 * @return Movement angle in radians
 */
float calculateMovement() {
    if (!gameState.gameActive || !gameState.myPlayerFound) {
        return 0.0f; // Stay still if game inactive or position unknown
    }
    
    // Calculate map diagonal for distance normalization
    const float mapDiagonal = sqrtf(pow(gameState.mapHeight,2) + pow(gameState.mapWidth,2));
    
    // Target tracking variables (position, score for prioritization)
    float dangerX = 0, dangerY = 0, dangerScore = 0;           // Dangerous players
    float foodX = 0, foodY = 0, foodScore = 0;                 // Transistors to collect
    float huntX = 0, huntY = 0, huntScore = 0;                 // Distant weak players
    float sparkX = 0, sparkY = 0, sparkScore = 0;             // Threatening sparks
    float attackX = 0, attackY = 0, attackScore = 0;          // Nearby weak players
    
    printf("My position: (%.1f, %.1f), HP: %.1f\n", gameState.myX, gameState.myY, gameState.myHP);
    
    // === PLAYER ANALYSIS ===
    for(uint8_t i = 0; i < gameState.playerCount; i++) {
        // Skip self and dead players
        if(gameState.players[i].objectNo == gameState.myPlayerNumber || gameState.players[i].hp <= 0) 
            continue;
        
        float dx = gameState.players[i].x - gameState.myX;
        float dy = gameState.players[i].y - gameState.myY;
        float distance = sqrtf(dx*dx + dy*dy);
        
        if(gameState.players[i].hp > gameState.myHP) {
            // DANGEROUS PLAYER DETECTION
            float detectionRange = DANGER_DETECTION_RANGE + PLAYER_BASE_RADIUS + gameState.myHP;
            if(distance > detectionRange) continue;
            
            // Score: higher HP and closer distance = higher threat
            float threatScore = gameState.players[i].hp / (distance / mapDiagonal);
            
            if(threatScore > dangerScore) {
                dangerX = gameState.players[i].x;
                dangerY = gameState.players[i].y;
                dangerScore = threatScore;
            }
            
        } else if(gameState.myHP > gameState.players[i].hp) {
            // WEAK PLAYER DETECTION
            
            // Check for immediate attack opportunity
            if(distance <= ATTACK_RANGE) {
                float attackScore_temp = gameState.myHP / (distance / mapDiagonal);
                if(attackScore_temp > attackScore) {
                    attackX = gameState.players[i].x;
                    attackY = gameState.players[i].y;
                    attackScore = attackScore_temp;
                }
            }
            
            // Check for hunting opportunity (longer distance, consider glue)
            float adjustedDistance = distance;
            
            // GLUE PENALTY CALCULATION
            for(uint8_t j = 0; j < gameState.glueCount; j++) {
                if(gameState.glue[j].hp <= 0) continue;
                
                float glueX = gameState.glue[j].x - gameState.myX;
                float glueY = gameState.glue[j].y - gameState.myY;
                float glueDistance = sqrtf(glueX*glueX + glueY*glueY) - GLUE_RADIUS;
                
                // Check if glue blocks path to target
                if(glueDistance < distance) {
                    float glueAngle = atan2f(GLUE_RADIUS, glueDistance);
                    float targetAngle = atan2f(dy, dx);
                    float glueTargetAngle = atan2f(glueY, glueX);
                    
                    // If target is behind glue area
                    if(targetAngle < glueTargetAngle + glueAngle && 
                       targetAngle > glueTargetAngle - glueAngle) {
                        adjustedDistance = distance * GLUE_MOVEMENT_PENALTY;
                        break;
                    }
                }
            }
            
            // Calculate hunt score with glue penalty
            float huntScore_temp = gameState.myHP / (adjustedDistance / mapDiagonal);
            if(huntScore_temp > huntScore) {
                huntX = gameState.players[i].x;
                huntY = gameState.players[i].y;
                huntScore = huntScore_temp;
            }
        }
    }
    
    // === SPARK ANALYSIS ===
    for(uint8_t i = 0; i < gameState.sparkCount; i++) {
        if(gameState.sparks[i].hp <= 0) continue;
        
        float dx = gameState.sparks[i].x - gameState.myX;
        float dy = gameState.sparks[i].y - gameState.myY;
        float distance = sqrtf(pow(dx,2) + pow(dy,2));
        
        // Only consider close sparks as immediate threats
        float sparkThreatRange = SPARK_DETECTION_RANGE + PLAYER_BASE_RADIUS + gameState.myHP;
        if(distance > sparkThreatRange) continue;
        
        // Score: closer sparks are more dangerous
        float sparkThreatScore = gameState.sparks[i].hp / (distance / mapDiagonal);
        if(sparkThreatScore > sparkScore) {
            sparkX = gameState.sparks[i].x;
            sparkY = gameState.sparks[i].y;
            sparkScore = sparkThreatScore;
        }
    }
    
    // === FOOD ANALYSIS ===
    for(uint8_t i = 0; i < gameState.transistorCount; i++) {
        if(gameState.transistors[i].hp <= 0) continue; // Skip eaten transistors
        
        float dx = gameState.transistors[i].x - gameState.myX;
        float dy = gameState.transistors[i].y - gameState.myY;
        float distance = sqrtf(dx*dx + dy*dy);
        float adjustedDistance = distance;
        
        // GLUE PENALTY FOR FOOD COLLECTION
        for(uint8_t j = 0; j < gameState.glueCount; j++) {
            if(gameState.glue[j].hp <= 0) continue;
            
            float glueX = gameState.glue[j].x - gameState.myX;
            float glueY = gameState.glue[j].y - gameState.myY;
            float glueDistance = sqrtf(glueX*glueX + glueY*glueY) - GLUE_RADIUS;
            
            if(glueDistance < distance) {
                float glueAngle = atan2f(GLUE_RADIUS, glueDistance);
                float targetAngle = atan2f(dy, dx);
                float glueTargetAngle = atan2f(glueY, glueX);
                
                if(targetAngle < glueTargetAngle + glueAngle && 
                   targetAngle > glueTargetAngle - glueAngle) {
                    adjustedDistance = distance * GLUE_MOVEMENT_PENALTY;
                    break;
                }
            }
        }
        
        // Score: higher HP food and closer distance = better target
        float foodValue = gameState.transistors[i].hp / (adjustedDistance / mapDiagonal);
        if(foodValue > foodScore) {
            foodX = gameState.transistors[i].x;
            foodY = gameState.transistors[i].y;
            foodScore = foodValue;
        }
    }
    
    // === DECISION MAKING (Priority Order) ===
    float movementAngle = 0.0f;
    
    if(dangerScore > 0) {
        // HIGHEST PRIORITY: Escape from dangerous players
        // Calculate perpendicular escape vector (90° from threat direction)
        float escapeX = -dangerY + gameState.myY + gameState.myX;
        float escapeY = dangerX - gameState.myX + gameState.myY;
        movementAngle = avoidSparkTrajectory(escapeX, escapeY);
        printf("ESCAPING from dangerous player at (%.1f, %.1f)\n", dangerX, dangerY);
        
    } else if(sparkScore > 0) {
        // HIGH PRIORITY: Avoid immediate spark threats
        // Move directly away from spark (180° opposite)
        movementAngle = atan2f(-(sparkY - gameState.myY), -(sparkX - gameState.myX));
        printf("AVOIDING spark at (%.1f, %.1f)\n", sparkX, sparkY);
        
    } else if(attackScore > 0) {
        // MEDIUM-HIGH PRIORITY: Attack nearby weak players
        movementAngle = avoidSparkTrajectory(attackX, attackY);
        printf("ATTACKING weak player at (%.1f, %.1f), score=%.2f\n", attackX, attackY, attackScore);
        
    } else if(foodScore > 0) {
        // MEDIUM PRIORITY: Collect food (transistors)
        movementAngle = avoidSparkTrajectory(foodX, foodY);
        printf("COLLECTING food at (%.1f, %.1f), score=%.2f\n", foodX, foodY, foodScore);
        
    } else if(huntScore > 0) {
        // LOW PRIORITY: Hunt distant weak players
        movementAngle = avoidSparkTrajectory(huntX, huntY);
        printf("HUNTING at (%.1f, %.1f), score=%.2f\n", huntX, huntY, huntScore);
        
    } else {
        // LOWEST PRIORITY: Entertainment when no targets available
        movementAngle = getDanceAngle();
        printf("NO TARGETS - Performing Konami Code dance!\n");
    }
    
    // Ensure angle is in valid range [0, 2π)
    movementAngle = normalizeAngle(movementAngle);
    
    return movementAngle;
}

/**
 * Processes object update packets from server
 * Routes different object types to appropriate update functions
 * @param packet Received AMCOM packet containing object data
 */
void processObjectUpdate(const AMCOM_Packet* packet) {
    uint8_t objectCount = packet->header.length / sizeof(AMCOM_ObjectState);
    if(objectCount == 0) return;
    
    AMCOM_ObjectUpdateRequestPayload* updatePayload = (AMCOM_ObjectUpdateRequestPayload*)packet->payload;
    
    // Process each object in the packet
    for(uint8_t i = 0; i < objectCount; i++) {
        AMCOM_ObjectState* obj = &updatePayload->objectState[i];
        
        // Route to appropriate handler based on object type
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
    
    // Update our cached position after processing all objects
    updateMyPlayerCache();
}

void amPacketHandler(const AMCOM_Packet* packet, void* userContext) {
    uint8_t responseBuffer[AMCOM_MAX_PACKET_SIZE];
    size_t responseSize = 0;
    SOCKET ConnectSocket = *((SOCKET*)userContext);

    switch (packet->header.type) {
        case AMCOM_IDENTIFY_REQUEST:
            printf("Got IDENTIFY.request. Responding with IDENTIFY.response\n");
            AMCOM_IdentifyResponsePayload identifyResponse;
            sprintf(identifyResponse.playerName, "sAMobujca");
            responseSize = AMCOM_Serialize(AMCOM_IDENTIFY_RESPONSE, &identifyResponse, 
                                         sizeof(identifyResponse), responseBuffer);
            break;
            
        case AMCOM_NEW_GAME_REQUEST:
            printf("Got NEW_GAME.request.\n");
            AMCOM_NewGameRequestPayload* newGameReq = (AMCOM_NewGameRequestPayload*)packet->payload;
            
            // Initialize game state
            gameState.myPlayerNumber = newGameReq->playerNumber;
            gameState.mapWidth = newGameReq->mapWidth;
            gameState.mapHeight = newGameReq->mapHeight;
            gameState.gameActive = true;
            gameState.konamiIndex = 0; // Reset dance sequence
            
            printf("Player number: %d, Map: %.1fx%.1f\n",
                   gameState.myPlayerNumber, gameState.mapWidth, gameState.mapHeight);
            
            AMCOM_NewGameResponsePayload newGameResponse;
            sprintf(newGameResponse.helloMessage, "Bedzie magik i to za dwa lata");
            responseSize = AMCOM_Serialize(AMCOM_NEW_GAME_RESPONSE, &newGameResponse, 
                                         sizeof(newGameResponse), responseBuffer);
            break;
            
        case AMCOM_OBJECT_UPDATE_REQUEST:
            processObjectUpdate(packet);
            break;
            
        case AMCOM_MOVE_REQUEST:
            AMCOM_MoveRequestPayload* moveReq = (AMCOM_MoveRequestPayload*)packet->payload;
            gameState.currentGameTime = moveReq->gameTime;
            
            AMCOM_MoveResponsePayload moveResponse;
            moveResponse.angle = calculateMovement();
            responseSize = AMCOM_Serialize(AMCOM_MOVE_RESPONSE, &moveResponse, 
                                         sizeof(moveResponse), responseBuffer);
            break;
            
        case AMCOM_GAME_OVER_REQUEST:
            printf("Got GAME_OVER.request\n");
            gameState.gameActive = false;
            
            AMCOM_GameOverResponsePayload gameOverResponse;
            sprintf(gameOverResponse.endMessage, "GG WP!");
            responseSize = AMCOM_Serialize(AMCOM_GAME_OVER_RESPONSE, &gameOverResponse, 
                                         sizeof(gameOverResponse), responseBuffer);
            break;
            
        default:
            printf("Unknown packet type: %d\n", packet->header.type);
            break;
    }

    if (responseSize > 0) {
        int bytesSent = send(ConnectSocket, (const char*)responseBuffer, responseSize, 0);
        if (bytesSent == SOCKET_ERROR) {
            printf("Socket send failed with error: %d\n", WSAGetLastError());
            closesocket(ConnectSocket);
            return;
        }
    }
}

#define GAME_SERVER "localhost"
#define GAME_SERVER_PORT "2001"

int main(int argc, char **argv) {
    printf("This is mniAM player. Let's eat some transistors! \n");
    
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo *result = NULL, *ptr = NULL, hints;
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

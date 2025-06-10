#ifndef AMCOM_PACKETS_H_
#define AMCOM_PACKETS_H_

#include "amcom.h"

/// Maximum length of the player name including trailing '\0'
#define AMCOM_MAX_PLAYER_NAME_LEN 24
/// Maximum length of the player message including trailing '\0'
#define AMCOM_MAX_PLAYER_MESSAGE_LEN 127
/// Maximum number of @ref AMCOM_PlayerState structures in a GAME_OVER.request packet
#define AMCOM_MAX_PLAYER_UPDATES 8
/// Maximum number of @ref AMCOM_ObjectState structures in a OBJECT_UPDATE.request packet
#define AMCOM_MAX_OBJECT_UPDATES 16

/// Possible packet types
typedef enum {
    AMCOM_NO_PACKET             = 0,
    AMCOM_IDENTIFY_REQUEST      = 1,
    AMCOM_IDENTIFY_RESPONSE     = 2,
    AMCOM_NEW_GAME_REQUEST      = 3,
    AMCOM_NEW_GAME_RESPONSE     = 4,
    AMCOM_OBJECT_UPDATE_REQUEST = 5,
    AMCOM_MOVE_REQUEST          = 6,
    AMCOM_MOVE_RESPONSE         = 7,
    AMCOM_GAME_OVER_REQUEST     = 8,
    AMCOM_GAME_OVER_RESPONSE    = 9,
} AMCOM_PacketType;

/// Structure of the IDENTIFY.request packet payload
typedef struct AMPACKED {
    /// Game version - higher version number
    uint8_t gameVerHi;
    /// Game version - lower version number
    uint8_t gameVerLo;
    /// Game revision
    uint16_t gameRevision;
} AMCOM_IdentifyRequestPayload;
// static assertion to check that the structure is indeed packed
static_assert(4 == sizeof(AMCOM_IdentifyRequestPayload), "4 != sizeof(AMCOM_IdentifyRequestPayload)");

/// Structure of the IDENTIFY.response packet payload
typedef struct AMPACKED {
    /// Player name (including trailing '\0')
    char playerName[AMCOM_MAX_PLAYER_NAME_LEN];
} AMCOM_IdentifyResponsePayload;
// static assertion to check that the structure is indeed packed
static_assert(24 == sizeof(AMCOM_IdentifyResponsePayload), "24 != sizeof(AMCOM_IdentifyResponsePayload)");


/// Structure of the NEW_GAME.request packet payload
typedef struct AMPACKED {
    uint8_t playerNumber;
    uint8_t numberOfPlayers;
    float   mapWidth;
    float   mapHeight;
} AMCOM_NewGameRequestPayload;
// static assertion to check that the structure is indeed packed
static_assert(10 == sizeof(AMCOM_NewGameRequestPayload), "10 != sizeof(AMCOM_NewGameRequestPayload)");

/// Structure of the NEW_GAME.response packet payload
typedef struct AMPACKED {
    /// Player greeting message (including trailing '\0')
    char helloMessage[AMCOM_MAX_PLAYER_MESSAGE_LEN];
} AMCOM_NewGameResponsePayload;
// static assertion to check that the structure is indeed packed
static_assert(127 == sizeof(AMCOM_NewGameResponsePayload), "127 != sizeof(AMCOM_NewGameResponsePayload)");


/// Structure describing the state of a single game object
typedef struct AMPACKED {
    // object type (0 = player, 1 = food, 2 = spark, 3 = glue)
    uint8_t objectType;
    // object number (each object type is numbered separately)
    uint16_t objectNo;
    // object hp
    int8_t hp;
    /// X position on map
    float x;
    /// Y position on map
    float y;
} AMCOM_ObjectState;
// static assertion to check that the structure is indeed packed
static_assert(12 == sizeof(AMCOM_ObjectState), "11 != sizeof(AMCOM_ObjectState)");

/// Structure of the FOOD_UPDATE.request packet payload
typedef struct AMPACKED {
    /// array of food states - the actual number of items in this array depends on the packet length
    AMCOM_ObjectState objectState[AMCOM_MAX_OBJECT_UPDATES];
} AMCOM_ObjectUpdateRequestPayload;
// static assertion to check that the structure is indeed packed
static_assert(192 == sizeof(AMCOM_ObjectUpdateRequestPayload), "192 != sizeof(AMCOM_ObjectUpdateRequestPayload)");


/// Structure of the MOVE.request packet payload
typedef struct AMPACKED {
    /// current game time
    uint32_t gameTime;
} AMCOM_MoveRequestPayload;
// static assertion to check that the structure is indeed packed
static_assert(4 == sizeof(AMCOM_MoveRequestPayload), "4 != sizeof(AMCOM_MoveRequestPayload)");

/// Structure of the MOVE.response packet payload
typedef struct AMPACKED {
    /// angle at which the player should move (in radians)
    float angle;
} AMCOM_MoveResponsePayload;
// static assertion to check that the structure is indeed packed
static_assert(4 == sizeof(AMCOM_MoveResponsePayload), "4 != sizeof(AMCOM_MoveResponsePayload)");


/// Structure of the GAME_OVER.request packet payload
typedef struct AMPACKED {
    /// array of player states - the actual number of items in this array depends on the packet length
    AMCOM_ObjectState playerState[AMCOM_MAX_PLAYER_UPDATES];
} AMCOM_GameOverRequestPayload;
// static assertion to check that the structure is indeed packed
static_assert(96 == sizeof(AMCOM_GameOverRequestPayload), "96 != sizeof(AMCOM_GameOverRequestPayload)");

/// Structure of the GAME_OVER.response packet payload
typedef struct AMPACKED {
    /// Player end message (including trailing '\0')
    char endMessage[AMCOM_MAX_PLAYER_MESSAGE_LEN];
} AMCOM_GameOverResponsePayload;
// static assertion to check that the structure is indeed packed
static_assert(127 == sizeof(AMCOM_GameOverResponsePayload), "127 != sizeof(AMCOM_GameOverResponsePayload)");


#endif /* AMCOM_PACKETS_H_ */

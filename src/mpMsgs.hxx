// mpMsgs.hxx
#ifndef _MPMSGS_HXX_
#define _MPMSGS_HXX_
#ifdef _MSC_VER
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif // _MSC_VER

typedef uint32_t    xdr_data_t;      /* 4 Bytes */
typedef uint64_t    xdr_data2_t;     /* 8 Bytes */

#ifndef USE_SIMGEAR

inline uint16_t sg_bswap_16(uint16_t x) {
    x = (x >> 8) | (x << 8);
    return x;
}

inline uint32_t sg_bswap_32(uint32_t x) {
    x = ((x >>  8) & 0x00FF00FFL) | ((x <<  8) & 0xFF00FF00L);
    x = (x >> 16) | (x << 16);
    return x;
}

inline uint64_t sg_bswap_64(uint64_t x) {
    x = ((x >>  8) & 0x00FF00FF00FF00FFLL) | ((x <<  8) & 0xFF00FF00FF00FF00LL);
    x = ((x >> 16) & 0x0000FFFF0000FFFFLL) | ((x << 16) & 0xFFFF0000FFFF0000LL);
    x = (x >> 32) | (x << 32);
    return x;
}


inline bool sgIsLittleEndian() {
    static const int sgEndianTest = 1;
    return (*((char *) &sgEndianTest ) != 0);
}

inline bool sgIsBigEndian() {
    static const int sgEndianTest = 1;
    return (*((char *) &sgEndianTest ) == 0);
}

inline void sgEndianSwap(uint16_t *x) { *x = sg_bswap_16(*x); }
inline void sgEndianSwap(uint32_t *x) { *x = sg_bswap_32(*x); }
inline void sgEndianSwap(uint64_t *x) { *x = sg_bswap_64(*x); }

#endif // USE_SIMGEAR

#define SWAP16(arg) sgIsLittleEndian() ? sg_bswap_16(arg) : arg
#define SWAP32(arg) sgIsLittleEndian() ? sg_bswap_32(arg) : arg
#define SWAP64(arg) sgIsLittleEndian() ? sg_bswap_64(arg) : arg

#define XDR_BYTES_PER_UNIT  4

// magic value for messages
const uint32_t MSG_MAGIC = 0x46474653;  // "FGFS"
// relay magic value
const uint32_t RELAY_MAGIC = 0x53464746;    // GSGF
// protocoll version
const uint32_t PROTO_VER = 0x00010001;  // 1.1

// Message identifiers
#define CHAT_MSG_ID             1
#define RESET_DATA_ID           6
#define POS_DATA_ID             7

// XDR demands 4 byte alignment, but some compilers use8 byte alignment
// so it's safe to let the overall size of a network message be a 
// multiple of 8!
#define MAX_CALLSIGN_LEN        8
#define MAX_CHAT_MSG_LEN        256
#define MAX_MODEL_NAME_LEN      96
#define MAX_PROPERTY_LEN        52

// Header for use with all messages sent 
typedef struct tagT_MsgHdr {
    xdr_data_t  Magic;                  // Magic Value
    xdr_data_t  Version;                // Protocoll version
    xdr_data_t  MsgId;                  // Message identifier 
    xdr_data_t  MsgLen;                 // absolute length of message
    xdr_data_t  ReplyAddress;           // (player's receiver address
    xdr_data_t  ReplyPort;              // player's receiver port
    char Callsign[MAX_CALLSIGN_LEN];    // Callsign used by the player
}T_MsgHdr, *PT_MsgHdr;

// Chat message 
struct T_ChatMsg {
    char Text[MAX_CHAT_MSG_LEN];       // Text of chat message
};

// Position message
struct T_PositionMsg {
    char Model[MAX_MODEL_NAME_LEN];    // Name of the aircraft model

    // Time when this packet was generated
    xdr_data2_t time;
    xdr_data2_t lag;

    // position wrt the earth centered frame
    xdr_data2_t position[3];
    // orientation wrt the earth centered frame, stored in the angle axis
    // representation where the angle is coded into the axis length
    xdr_data_t orientation[3];

    // linear velocity wrt the earth centered frame measured in
    // the earth centered frame
    xdr_data_t linearVel[3];
    // angular velocity wrt the earth centered frame measured in
    // the earth centered frame
    xdr_data_t angularVel[3];

    // linear acceleration wrt the earth centered frame measured in
    // the earth centered frame
    xdr_data_t linearAccel[3];
    // angular acceleration wrt the earth centered frame measured in
    // the earth centered frame
    xdr_data_t angularAccel[3];
};

// Property message
struct T_PropertyMsg {
    xdr_data_t id;
    xdr_data_t value;
};


#endif // #ifndef _MPMSGS_HXX_
// eof - mpMsgs.hxx

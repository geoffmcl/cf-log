/*\
 * raw-log.cxx
 *
 * Copyright (c) 2015 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/
/*\
 * Read and show details of a raw MP udp receive log, from a FGFS instance
\*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h> // for malloc(), ...
#include <string.h> // for strdup(), ...
#include <algorithm> // for std::equal_range, ...
#include <vector>
#include <string>
#include <map>
#include <time.h>
#ifndef _MSC_VER
#include <string.h> // for strcpy(), ...
#include <stdlib.h> // for abs(), ...
#include <stdint.h> // for int32_t, ...
#endif // !_MSC_VER
#ifdef USE_SIMGEAR
// so use SGMath.hxx
#include <simgear/compiler.h>
#include <simgear/constants.h>
#include <simgear/math/SGMath.hxx>
#include <simgear/misc/stdint.hxx>
#else
#ifdef USE_GeographicLib
#include <geod.hxx>
#endif
#endif
// use substitute maths
#include "fg_geometry.hxx"
#include "cf_euler.hxx"
#include "sprtf.hxx"
#include "mpMsgs.hxx"
#ifdef USE_SIMGEAR
#include "xdr_lib/tiny_xdr.hxx"
#else
#include "tiny_xdr.hxx"
#endif
#include "cf_misc.hxx"

#ifndef SPRTF
#define SPRTF printf
#endif

typedef std::vector<std::string> vSTG;
typedef std::vector<uint32_t>   vUINT;
typedef std::map<int, int> mINTINT;
typedef mINTINT::iterator iINTINT;

static const char *module = "raw-log";
#define M2F 3.28084
#ifndef MEOL
#ifdef WIN32
#define MEOL "\r\n"
#else
#define MEOL "\n"
#endif
#endif
#define MAX_PACKET_SIZE 1200
#define MAX_TEXT_SIZE 128

static int verbosity = 1;
#define VERB1 (verbosity >= 1)
#define VERB2 (verbosity >= 2)
#define VERB5 (verbosity >= 5)
#define VERB9 (verbosity >= 9)

static const char *def_log = "tempraw.txt";
static const char *usr_input = 0;
static struct stat sbuf;
static FILE *raw_log_fp = 0;
static char *raw_log_buffer = 0;
static size_t raw_block_size = 0;
static size_t raw_data_size = 0;
static double raw_bgn_secs = 0.0;   // start of raw log reading
static size_t raw_log_size = 0;
static size_t raw_log_remaining = 0;
static size_t packet_cnt = 0;
static int show_consumed_bytes = 0;
#if !defined(NDEBUG) && defined(_MSC_VER)
static const char *sample = "F:\\Projects\\cf-log\\data\\sampleudp01.log";
#endif
static int do_packet_test = 0;

static vSTG vWarnings;
int add_2_list(char *msg)
{
    std::string m(msg);
    std::string s;
    size_t ii, len = vWarnings.size();
    for (ii = 0; ii < len; ii++) {
        s = vWarnings[ii];
        if (m == s)
            return 0;
    }
    vWarnings.push_back(m);
    return 1;   // signal it is new and added
}

static vUINT vIdsUsed;
void add_id_count(unsigned int id);
int add_2_ids(uint32_t id)
{
    add_id_count(id);
    size_t ii, len = vIdsUsed.size();
    uint32_t ui;
    for (ii = 0; ii < len; ii++) {
        ui = vIdsUsed[ii];
        if (ui == id)
            return 0;
    }
    vIdsUsed.push_back(id);
    return 1;
}

void show_warnings()
{
    std::string s;
    size_t ii, len = vWarnings.size();
    if (len || VERB1)
    {
        SPRTF("Repeating %d warnings...\n", (int)len);
    }
    for (ii = 0; ii < len; ii++) {
        s = vWarnings[ii];
        SPRTF("%s", s.c_str());
    }
}


enum Packet_Type {
    pkt_Invalid,    // not used
    pkt_InvLen1,    // too short for header
    pkt_InvLen2,    // too short for position
    pkt_InvMag,     // Magic value error
    pkt_InvProto,   // not right protocol
    pkt_InvPos,     // linear value all zero - never seen one!
    pkt_InvHgt,     // alt <= -9990 feet - always when fgs starts
    pkt_InvStg1,    // no callsign after filtering
    pkt_InvStg2,    // no aircraft
                    // all ABOVE are INVALID packets
                    pkt_First,      // first pos packet
                    pkt_Pos,        // pilot position packets
                    pkt_Chat,       // chat packets
                    pkt_Other,      // should be NONE!!!
                    pkt_Discards,   // valid, but due to no time/dist...
                    pkt_Max
};

typedef struct tagPKTSTR {
    Packet_Type pt;
    const char *desc;
    int count;
    int totals;
    void *vp;
}PKTSTR, *PPKTSTR;

static PKTSTR sPktStr[pkt_Max] = {
    { pkt_Invalid, "Invalid",     0, 0, 0 },
    { pkt_InvLen1, "InvLen1",     0, 0, 0 },
    { pkt_InvLen2, "InvLen2",     0, 0, 0 },
    { pkt_InvMag, "InvMag",       0, 0, 0 },
    { pkt_InvProto, "InvPoto",    0, 0, 0 },
    { pkt_InvPos, "InvPos",       0, 0, 0 },
    { pkt_InvHgt, "InvHgt",       0, 0, 0 },
    { pkt_InvStg1, "InvCallsign", 0, 0, 0 },
    { pkt_InvStg2, "InvAircraft", 0, 0, 0 },
    { pkt_First, "FirstPos",      0, 0, 0 },
    { pkt_Pos, "Position",        0, 0, 0 },
    { pkt_Chat, "Chat",           0, 0, 0 },
    { pkt_Other, "Other",         0, 0, 0 },
    { pkt_Discards, "Discards",   0, 0, 0 }
};

PPKTSTR Get_Pkt_Str() { return &sPktStr[0]; }

static void show_packet_stats()
{
    //if (VERB1)
    //{
        int i, cnt, PacketCount, Bad_Packets, DiscardCount;
        PPKTSTR pps = Get_Pkt_Str();

        PacketCount = 0;
        Bad_Packets = 0;
        DiscardCount = pps[pkt_Discards].count;
        for (i = 0; i < pkt_Max; i++) {
            cnt = pps[i].count;
            PacketCount += cnt;
            if (i < pkt_First)
                Bad_Packets += cnt;
            if (cnt) {
                SPRTF("%s %d ", pps[i].desc, cnt);
            }
        }
        SPRTF("Total %d, Discarded %d, Bad %d\n", PacketCount, DiscardCount, Bad_Packets);
    //}
}


void give_help( char *name )
{
    SPRTF("\n");
    SPRTF("Usage: version " DOTVERSION "\n");
    SPRTF(" %s [options] usr_input\n", module);
    SPRTF("\n");
    SPRTF("Options:\n");
    SPRTF(" --help  (-h or -?) = This help and exit(0)\n");
    SPRTF(" --verb[n]     (-v) = Bump or set verbosity to n. Values 0,1,2,5,9 (def=%d)\n", verbosity);
    SPRTF(" --log <file>  (-l) = Set name of output log. (def=%s)\n", def_log);
    SPRTF(" --test        (-t) = Do packet test, and exit(1) (def=%d)\n", do_packet_test);
    SPRTF("\n");
    SPRTF("Description:\n");
    SPRTF(" Read and decode a raw log of FGFS mp packets, and output information found.\n");
    SPRTF(" To be fully effective, this utility needs to link with SimGearCore.lib, but has some\n");
    SPRTF(" not so well tested alterative maths and xdr decoding available.\n");
    SPRTF("\n");
    SPRTF("Note:\n");
    SPRTF(" The udp-recv, from the https://github.com/geoffmcl/tcp-tests/ repository can be used\n");
    SPRTF(" as an easy way to capture a raw log of FGFS mp packets, as well as probably many other\n");
    SPRTF(" tools and utilities.\n");
    SPRTF("\n");
}

#ifndef ISDIGIT
#define ISDIGIT(a) ((a >= '0') && (a <= '9'))
#endif

int parse_args( int argc, char **argv )
{
    int i,i2,c;
    char *arg, *sarg;
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        i2 = i + 1;
        if (*arg == '-') {
            sarg = &arg[1];
            while (*sarg == '-')
                sarg++;
            c = *sarg;
            switch (c) {
            case 'h':
            case '?':
                give_help(argv[0]);
                return 2;
                break;
            case 'v':
                verbosity++;
                sarg++;
                while (*sarg)
                {
                    if (ISDIGIT(*sarg)) {
                        verbosity = atoi(sarg);
                        break;
                    }
                    if (*sarg == 'v')
                        verbosity++;
                    sarg++;
                }
                break;
            case 'l':
                if (i2 < argc)
                    i++;    // already handled
                break;
            case 't':
                do_packet_test = 1;
                break;
                // TODO: Other arguments
            default:
                SPRTF("%s: Unknown argument '%s'. Try -? for help...\n", module, arg);
                return 1;
            }
        } else {
            // bear argument
            if (usr_input) {
                SPRTF("%s: Already have input '%s'! What is this '%s'?\n", module, usr_input, arg );
                return 1;
            }
            usr_input = strdup(arg);
        }
    }
#if !defined(NDEBUG) && defined(_MSC_VER)
    // just a DEBUG sample
    if (!usr_input) {
        usr_input = strdup(sample);
    }
#endif
    if (!usr_input) {
        SPRTF("%s: No user input found in command!\n", module);
        return 1;
    }
    return 0;
}

#define MX_BUF_SIZE 2048
#define MAX_RAW_LOG MX_BUF_SIZE
//static char tmp_buffer[MX_BUF_SIZE + 8];

//////////////////////////////////////////////////////////////////////////////
enum Pilot_Type {
    pt_Unknown,
    pt_New,
    pt_Revived,
    pt_Pos,
    pt_Expired,
    pt_Stat
};

#ifdef USE_SIMGEAR
//enum { X, Y, Z };
//enum { Lat, Lon, Alt };
#endif

///////////////////////////////////////////////////////////////////////////////
// Pilot information kept in vector list
// =====================================
// NOTE: From 'simgear' onwards, ALL members MUST be value type
// That is NO classes, ie no reference types
// This is updated in the vector by a copy through a pointer - *pp2 = *pp;
// If kept as 'value' members, this is a blindingly FAST rep movsb esi edi;
// *** Please KEEP it that way! ***
typedef struct tagCF_Pilot {
    uint64_t flight_id; // unique flight ID = epoch*1000000+tv_usec
    Pilot_Type      pt;
    bool            expired;
    time_t          curr_time, prev_time, first_time;    // rough seconds
    double          sim_time, prev_sim_time, first_sim_time; // sim time from packet
    char            callsign[MAX_CALLSIGN_LEN];
    char            aircraft[MAX_MODEL_NAME_LEN];
    double          lat, lon;    // degrees
    double          alt;         // feet
    float           ox, oy, oz;
#ifdef USE_SIMGEAR  // TOCHECK - new structure members
    double          px, py, pz;
    double          ppx, ppy, ppz;
//#else // #ifdef USE_SIMGEAR
#endif // #ifdef USE_SIMGEAR y/n
    Point3D         SenderPosition;
    Point3D         PrevPos;
    Point3D         SenderOrientation;
    Point3D         GeodPoint;
    Point3D  linearVel, angularVel, linearAccel, angularAccel;
    int SenderAddress, SenderPort;
    int             packetCount, packetsDiscarded;
    double          heading, pitch, roll, speed;
    double          dist_m; // vector length since last - meters
    double          total_nm, cumm_nm;   // total distance since start
    time_t          exp_time;    // time expired - epoch secs
    time_t          last_seen;  // last packet seen - epoch secs
}CF_Pilot, *PCF_Pilot;

typedef std::vector<CF_Pilot> vCFP;

vCFP vPilots;

static int failed_cnt = 0;
static int pos_cnt = 0;
static int chat_cnt = 0;
static int same_count = 0;
///////////////////////////////////////////////////////////////////////////
// Essentially just a DEBUG service
#define ADD_ORIENTATION
// show_pilot 
void print_pilot(PCF_Pilot pp, char *pm, Pilot_Type pt)
{
    if (!VERB1)
        return;
    char * cp = GetNxtBuf();
    //struct in_addr in;
    int ialt;
    double dlat, dlon, dalt;

    //in.s_addr = pp->SenderAddress;
    dlat = pp->lat;
    dlon = pp->lon;
    dalt = pp->alt;

    ialt = (int)(dalt + 0.5);
#ifdef ADD_ORIENTATION  // TOCHECK -add orientation to output
    sprintf(cp, "%s %s at %f,%f,%d, orien %f,%f,%f, in %s hdg=%d spd=%d t=%lf s.", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->ox, pp->oy, pp->oz,
        pp->aircraft,
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5),
        pp->sim_time);
    // inet_ntoa(in), pp->SenderPort);
#else
    sprintf(cp, "%s %s at %f,%f,%d, %s pkts=%d/%d hdg=%d spd=%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->aircraft,
        pp->packetCount, pp->packetsDiscarded,
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5));
#endif
    //if ((pp->pt == pt_Pos) && ((pm[0] == 'S') || (pp->dist_m > 0.0))) {
    if ((pt == pt_Pos) && ((pm[0] == 'S') || (pp->dist_m > 0.0))) {
        double secs = pp->sim_time - pp->prev_sim_time;
        double calc_spd = 0.0;
        if (secs > 0) {
            calc_spd = pp->dist_m * 1.94384 / secs; // m/s to knots
        }
        int ispd = (int)(calc_spd + 0.5);
        int enm = (int)(pp->total_nm + 0.5);
        if (calc_spd > 0.0) {
            sprintf(EndBuf(cp), " Est=%d kt %d nm ", ispd, enm);
        }
        else {
            sprintf(EndBuf(cp), " Est=%d/%f/%f/%f ", ispd, pp->dist_m, secs, pp->total_nm);
        }
    }
        
    if (pp->expired)
        strcat(cp, "EXPIRED");
    strcat(cp, MEOL);
    SPRTF(cp);
}


//////////////////////////////////////////////////////////////////////
// Rather rough service to remove leading PATH
// and remove trailing file extension
char *get_Model(char *pm)
{
    static char _s_buf[MAX_MODEL_NAME_LEN + 4];
    int i, c, len;
    char *cp = _s_buf;
    char *model = pm;
    len = MAX_MODEL_NAME_LEN;
    for (i = 0; i < len; i++) {
        c = pm[i];
        if (c == '/')
            model = &pm[i + 1];
        else if (c == 0)
            break;
    }
    strcpy(cp, model);
    len = (int)strlen(cp);
    model = 0;
    for (i = 0; i < len; i++) {
        c = cp[i];
        if (c == '.')
            model = &cp[i];
    }
    if (model)
        *model = 0;
    return cp;
}

enum sgp_Type {
    sgp_UNKNOW,
    sgp_FLOAT,
    sgp_STRING,
    sgp_BOOL,
    sgp_INT
};

#ifndef USE_SIMGEAR
typedef struct tagT2STG {
    sgp_Type t;
    const char *stg;
}T2STG, *PT2STG;

static T2STG t2stg[]{
    { sgp_FLOAT, "FLOAT"},
    { sgp_STRING, "STRING" },
    { sgp_BOOL, "BOOL" },
    { sgp_INT, "INT" },
    // end
    { sgp_UNKNOW, 0 }
};

static const char *type2stg(sgp_Type t)
{
    PT2STG pts = t2stg;
    while (pts->stg) {
        if (pts->t == t)
            return pts->stg;
        pts++;
    }
    return "UNKNOWN";
}
#endif 

////////////////////////////////////////////////////////////////////////////////////
// *********************************************************************************
#define USE_PROTO_2

#ifdef USE_PROTO_2

namespace simgear
{
    namespace props
    {
        /**
        * The possible types of an SGPropertyNode. Types that appear after
        * EXTENDED are not stored in the SGPropertyNode itself.
        */
        enum Type {
            NONE = 0, /**< The node hasn't been assigned a value yet. */
            ALIAS, /**< The node "points" to another node. */
            BOOL,
            INT,
            LONG,
            FLOAT,
            DOUBLE,
            STRING,
            UNSPECIFIED,
            EXTENDED, /**< The node's value is not stored in the property;
                      * the actual value and type is retrieved from an
                      * SGRawValue node. This type is never returned by @see
                      * SGPropertyNode::getType.
                      */
                      // Extended properties
                      VEC3D,
                      VEC4D
        };

        template<typename T> struct PropertyTraits;

#define DEFINTERNALPROP(TYPE, PROP) \
template<> \
struct PropertyTraits<TYPE> \
{ \
    static const Type type_tag = PROP; \
    enum  { Internal = 1 }; \
}

        DEFINTERNALPROP(bool, BOOL);
        DEFINTERNALPROP(int, INT);
        DEFINTERNALPROP(long, LONG);
        DEFINTERNALPROP(float, FLOAT);
        DEFINTERNALPROP(double, DOUBLE);
        DEFINTERNALPROP(const char *, STRING);
        DEFINTERNALPROP(const char[], STRING);
#undef DEFINTERNALPROP

    };

};

// try to avoid gcc warning about missing enums...
static const char *type2stg(simgear::props::Type t)
{
    const char *pt = "UNKNOWN";
    switch (t)
    {
    case simgear::props::INT:
        pt = "INT";
        break;
    case simgear::props::BOOL:
        pt = "BOOL";
        break;
    case simgear::props::LONG:
        pt = "LONG";
        break;
    case simgear::props::FLOAT:
        pt = "FLOAT";
        break;
    case simgear::props::DOUBLE:
        pt = "DOUBLE";
        break;
    case simgear::props::STRING:
        pt = "STRING";
        break;
    case simgear::props::UNSPECIFIED:
        pt = "UNSPCIFIED";
        break;
    default:
        pt = "NOT IN SWITCH";
        break;
    }
    return pt;
}

struct FGPropertyData {
    unsigned id;

    // While the type isn't transmitted, it is needed for the destructor
    simgear::props::Type type;
    union {
        int int_value;
        float float_value;
        char* string_value;
    };

    ~FGPropertyData() {
        if ((type == simgear::props::STRING) || (type == simgear::props::UNSPECIFIED))
        {
            delete[] string_value;
        }
    }
};



/*
* With the MP2017(V2) protocol it should be possible to transmit using a different type/encoding than the property has,
* so it should be possible to transmit a bool as
*/
enum TransmissionType {
    TT_ASIS = 0, // transmit as defined in the property. This is the default
    TT_BOOL = simgear::props::BOOL,
    TT_INT = simgear::props::INT,
    TT_FLOAT = simgear::props::FLOAT,
    TT_STRING = simgear::props::STRING,
    TT_SHORTINT = 0x100,
    TT_SHORT_FLOAT_NORM = 0x101, // -1 .. 1 encoded into a short int (16 bit)
    TT_SHORT_FLOAT_1 = 0x102, //range -3276.7 .. 3276.7  float encoded into a short int (16 bit) 
    TT_SHORT_FLOAT_2 = 0x103, //range -327.67 .. 327.67  float encoded into a short int (16 bit) 
    TT_SHORT_FLOAT_3 = 0x104, //range -32.767 .. 32.767  float encoded into a short int (16 bit) 
    TT_SHORT_FLOAT_4 = 0x105, //range -3.2767 .. 3.2767  float encoded into a short int (16 bit) 
    TT_BOOLARRAY,
    TT_CHAR,
};
/*
* Definitions for the version of the protocol to use to transmit the items defined in the IdPropertyList
*
* The MP2017(V2) protocol allows for much better packing of strings, new types that are transmitted in 4bytes by transmitting
* with short int (sometimes scaled) for the values (a lot of the properties that are transmitted will pack nicely into 16bits).
* The MP2017(V2) protocol also allows for properties to be transmitted automatically as a different type and the encode/decode will
* take this into consideration.
* The pad magic is used to force older clients to use verifyProperties and as the first property transmitted is short int encoded it
* will cause the rest of the packet to be discarded. This is the section of the packet that contains the properties defined in the list
* here - the basic motion properties remain compatible, so the older client will see just the model, not chat, not animations etc.
*/
const int V1_1_PROP_ID = 1;
const int V1_1_2_PROP_ID = 2;
const int V2_PAD_MAGIC = 0x1face002;

/*
* definition of properties that are to be transmitted.
* New for 2017.2:
* 1. TransmitAs - this causes the property to be transmitted on the wire using the
*    specified format transparently.
* 2. version - the minimum version of the protocol that is required to transmit a property.
*    Does not apply to incoming properties - as these will be decoded correctly when received
* 3. Convert; not implemented. Planned to allow property specific conversion rules to be applied
*/
struct IdPropertyList {
    unsigned id;
    const char* name;
    simgear::props::Type type;
    TransmissionType TransmitAs;
    int version;
    int(*convert)(int direction, xdr_data_t*, FGPropertyData*);
};

static const IdPropertyList* findProperty(unsigned id);

#if 0 // 0000000000000000000000000000000000000000
/*
* not yet used method to avoid transmitting a string for something that should always have been
* an integer
*/
static int convert_launchbar_state(int direction, xdr_data_t*, FGPropertyData*)
{
    return 0; // no conversion performed
}
#endif // 000000000000000000000000000000000000000

// A static map of protocol property id values to property paths,
// This should be extendable dynamically for every specific aircraft ...
// For now only that static list
static const IdPropertyList sIdPropertyList[] = {
    { 10,  "sim/multiplay/protocol-version",          simgear::props::INT,   TT_SHORTINT,  V1_1_PROP_ID, NULL },
    { 100, "surface-positions/left-aileron-pos-norm",  simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 101, "surface-positions/right-aileron-pos-norm", simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 102, "surface-positions/elevator-pos-norm",      simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 103, "surface-positions/rudder-pos-norm",        simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 104, "surface-positions/flap-pos-norm",          simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 105, "surface-positions/speedbrake-pos-norm",    simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 106, "gear/tailhook/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 107, "gear/launchbar/position-norm",             simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 108, "gear/launchbar/state",                     simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 109, "gear/launchbar/holdback-position-norm",    simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 110, "canopy/position-norm",                     simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 111, "surface-positions/wing-pos-norm",          simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 112, "surface-positions/wing-fold-pos-norm",     simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },

    { 200, "gear/gear[0]/compression-norm",           simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 201, "gear/gear[0]/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 210, "gear/gear[1]/compression-norm",           simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 211, "gear/gear[1]/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 220, "gear/gear[2]/compression-norm",           simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 221, "gear/gear[2]/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 230, "gear/gear[3]/compression-norm",           simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 231, "gear/gear[3]/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 240, "gear/gear[4]/compression-norm",           simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },
    { 241, "gear/gear[4]/position-norm",              simgear::props::FLOAT, TT_SHORT_FLOAT_NORM,  V1_1_PROP_ID, NULL },

    { 300, "engines/engine[0]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 301, "engines/engine[0]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 302, "engines/engine[0]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 310, "engines/engine[1]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 311, "engines/engine[1]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 312, "engines/engine[1]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 320, "engines/engine[2]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 321, "engines/engine[2]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 322, "engines/engine[2]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 330, "engines/engine[3]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 331, "engines/engine[3]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 332, "engines/engine[3]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 340, "engines/engine[4]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 341, "engines/engine[4]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 342, "engines/engine[4]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 350, "engines/engine[5]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 351, "engines/engine[5]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 352, "engines/engine[5]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 360, "engines/engine[6]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 361, "engines/engine[6]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 362, "engines/engine[6]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 370, "engines/engine[7]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 371, "engines/engine[7]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 372, "engines/engine[7]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 380, "engines/engine[8]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 381, "engines/engine[8]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 382, "engines/engine[8]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 390, "engines/engine[9]/n1",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 391, "engines/engine[9]/n2",  simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 392, "engines/engine[9]/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },

    { 800, "rotors/main/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 801, "rotors/tail/rpm", simgear::props::FLOAT, TT_SHORT_FLOAT_1,  V1_1_PROP_ID, NULL },
    { 810, "rotors/main/blade[0]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 811, "rotors/main/blade[1]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 812, "rotors/main/blade[2]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 813, "rotors/main/blade[3]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 820, "rotors/main/blade[0]/flap-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 821, "rotors/main/blade[1]/flap-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 822, "rotors/main/blade[2]/flap-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 823, "rotors/main/blade[3]/flap-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 830, "rotors/tail/blade[0]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 831, "rotors/tail/blade[1]/position-deg",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },

    { 900, "sim/hitches/aerotow/tow/length",                       simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 901, "sim/hitches/aerotow/tow/elastic-constant",             simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 902, "sim/hitches/aerotow/tow/weight-per-m-kg-m",            simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 903, "sim/hitches/aerotow/tow/dist",                         simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 904, "sim/hitches/aerotow/tow/connected-to-property-node",   simgear::props::BOOL, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 905, "sim/hitches/aerotow/tow/connected-to-ai-or-mp-callsign",   simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 906, "sim/hitches/aerotow/tow/brake-force",                  simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 907, "sim/hitches/aerotow/tow/end-force-x",                  simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 908, "sim/hitches/aerotow/tow/end-force-y",                  simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 909, "sim/hitches/aerotow/tow/end-force-z",                  simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 930, "sim/hitches/aerotow/is-slave",                         simgear::props::BOOL, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 931, "sim/hitches/aerotow/speed-in-tow-direction",           simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 932, "sim/hitches/aerotow/open",                             simgear::props::BOOL, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 933, "sim/hitches/aerotow/local-pos-x",                      simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 934, "sim/hitches/aerotow/local-pos-y",                      simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 935, "sim/hitches/aerotow/local-pos-z",                      simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },

    { 1001, "controls/flight/slats",  simgear::props::FLOAT, TT_SHORT_FLOAT_4,  V1_1_PROP_ID, NULL },
    { 1002, "controls/flight/speedbrake",  simgear::props::FLOAT, TT_SHORT_FLOAT_4,  V1_1_PROP_ID, NULL },
    { 1003, "controls/flight/spoilers",  simgear::props::FLOAT, TT_SHORT_FLOAT_4,  V1_1_PROP_ID, NULL },
    { 1004, "controls/gear/gear-down",  simgear::props::FLOAT, TT_SHORT_FLOAT_4,  V1_1_PROP_ID, NULL },
    { 1005, "controls/lighting/nav-lights",  simgear::props::FLOAT, TT_SHORT_FLOAT_3,  V1_1_PROP_ID, NULL },
    { 1006, "controls/armament/station[0]/jettison-all",  simgear::props::BOOL, TT_SHORTINT,  V1_1_PROP_ID, NULL },

    { 1100, "sim/model/variant", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 1101, "sim/model/livery/file", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },

    { 1200, "environment/wildfire/data", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 1201, "environment/contrail", simgear::props::INT, TT_SHORTINT,  V1_1_PROP_ID, NULL },

    { 1300, "tanker", simgear::props::INT, TT_SHORTINT,  V1_1_PROP_ID, NULL },

    { 1400, "scenery/events", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },

    { 1500, "instrumentation/transponder/transmitted-id", simgear::props::INT, TT_SHORTINT,  V1_1_PROP_ID, NULL },
    { 1501, "instrumentation/transponder/altitude", simgear::props::INT, TT_ASIS,  TT_SHORTINT, NULL },
    { 1502, "instrumentation/transponder/ident", simgear::props::BOOL, TT_ASIS,  TT_SHORTINT, NULL },
    { 1503, "instrumentation/transponder/inputs/mode", simgear::props::INT, TT_ASIS,  TT_SHORTINT, NULL },

    { 10001, "sim/multiplay/transmission-freq-hz",  simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10002, "sim/multiplay/chat",  simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },

    { 10100, "sim/multiplay/generic/string[0]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10101, "sim/multiplay/generic/string[1]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10102, "sim/multiplay/generic/string[2]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10103, "sim/multiplay/generic/string[3]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10104, "sim/multiplay/generic/string[4]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10105, "sim/multiplay/generic/string[5]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10106, "sim/multiplay/generic/string[6]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10107, "sim/multiplay/generic/string[7]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10108, "sim/multiplay/generic/string[8]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10109, "sim/multiplay/generic/string[9]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10110, "sim/multiplay/generic/string[10]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10111, "sim/multiplay/generic/string[11]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10112, "sim/multiplay/generic/string[12]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10113, "sim/multiplay/generic/string[13]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10114, "sim/multiplay/generic/string[14]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10115, "sim/multiplay/generic/string[15]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10116, "sim/multiplay/generic/string[16]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10117, "sim/multiplay/generic/string[17]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10118, "sim/multiplay/generic/string[18]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },
    { 10119, "sim/multiplay/generic/string[19]", simgear::props::STRING, TT_ASIS,  V1_1_2_PROP_ID, NULL },

    { 10200, "sim/multiplay/generic/float[0]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10201, "sim/multiplay/generic/float[1]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10202, "sim/multiplay/generic/float[2]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10203, "sim/multiplay/generic/float[3]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10204, "sim/multiplay/generic/float[4]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10205, "sim/multiplay/generic/float[5]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10206, "sim/multiplay/generic/float[6]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10207, "sim/multiplay/generic/float[7]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10208, "sim/multiplay/generic/float[8]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10209, "sim/multiplay/generic/float[9]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10210, "sim/multiplay/generic/float[10]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10211, "sim/multiplay/generic/float[11]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10212, "sim/multiplay/generic/float[12]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10213, "sim/multiplay/generic/float[13]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10214, "sim/multiplay/generic/float[14]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10215, "sim/multiplay/generic/float[15]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10216, "sim/multiplay/generic/float[16]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10217, "sim/multiplay/generic/float[17]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10218, "sim/multiplay/generic/float[18]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10219, "sim/multiplay/generic/float[19]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },

    { 10220, "sim/multiplay/generic/float[20]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10221, "sim/multiplay/generic/float[21]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10222, "sim/multiplay/generic/float[22]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10223, "sim/multiplay/generic/float[23]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10224, "sim/multiplay/generic/float[24]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10225, "sim/multiplay/generic/float[25]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10226, "sim/multiplay/generic/float[26]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10227, "sim/multiplay/generic/float[27]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10228, "sim/multiplay/generic/float[28]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10229, "sim/multiplay/generic/float[29]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10230, "sim/multiplay/generic/float[30]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10231, "sim/multiplay/generic/float[31]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10232, "sim/multiplay/generic/float[32]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10233, "sim/multiplay/generic/float[33]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10234, "sim/multiplay/generic/float[34]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10235, "sim/multiplay/generic/float[35]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10236, "sim/multiplay/generic/float[36]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10237, "sim/multiplay/generic/float[37]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10238, "sim/multiplay/generic/float[38]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10239, "sim/multiplay/generic/float[39]", simgear::props::FLOAT, TT_ASIS,  V1_1_PROP_ID, NULL },

    { 10300, "sim/multiplay/generic/int[0]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10301, "sim/multiplay/generic/int[1]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10302, "sim/multiplay/generic/int[2]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10303, "sim/multiplay/generic/int[3]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10304, "sim/multiplay/generic/int[4]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10305, "sim/multiplay/generic/int[5]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10306, "sim/multiplay/generic/int[6]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10307, "sim/multiplay/generic/int[7]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10308, "sim/multiplay/generic/int[8]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10309, "sim/multiplay/generic/int[9]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10310, "sim/multiplay/generic/int[10]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10311, "sim/multiplay/generic/int[11]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10312, "sim/multiplay/generic/int[12]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10313, "sim/multiplay/generic/int[13]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10314, "sim/multiplay/generic/int[14]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10315, "sim/multiplay/generic/int[15]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10316, "sim/multiplay/generic/int[16]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10317, "sim/multiplay/generic/int[17]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10318, "sim/multiplay/generic/int[18]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },
    { 10319, "sim/multiplay/generic/int[19]", simgear::props::INT, TT_ASIS,  V1_1_PROP_ID, NULL },

    { 10500, "sim/multiplay/generic/short[0]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10501, "sim/multiplay/generic/short[1]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10502, "sim/multiplay/generic/short[2]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10503, "sim/multiplay/generic/short[3]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10504, "sim/multiplay/generic/short[4]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10505, "sim/multiplay/generic/short[5]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10506, "sim/multiplay/generic/short[6]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10507, "sim/multiplay/generic/short[7]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10508, "sim/multiplay/generic/short[8]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10509, "sim/multiplay/generic/short[9]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10510, "sim/multiplay/generic/short[10]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10511, "sim/multiplay/generic/short[11]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10512, "sim/multiplay/generic/short[12]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10513, "sim/multiplay/generic/short[13]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10514, "sim/multiplay/generic/short[14]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10515, "sim/multiplay/generic/short[15]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10516, "sim/multiplay/generic/short[16]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10517, "sim/multiplay/generic/short[17]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10518, "sim/multiplay/generic/short[18]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10519, "sim/multiplay/generic/short[19]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10520, "sim/multiplay/generic/short[20]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10521, "sim/multiplay/generic/short[21]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10522, "sim/multiplay/generic/short[22]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10523, "sim/multiplay/generic/short[23]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10524, "sim/multiplay/generic/short[24]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10525, "sim/multiplay/generic/short[25]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10526, "sim/multiplay/generic/short[26]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10527, "sim/multiplay/generic/short[27]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10528, "sim/multiplay/generic/short[28]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10529, "sim/multiplay/generic/short[29]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10530, "sim/multiplay/generic/short[30]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10531, "sim/multiplay/generic/short[31]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10532, "sim/multiplay/generic/short[32]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10533, "sim/multiplay/generic/short[33]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10534, "sim/multiplay/generic/short[34]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10535, "sim/multiplay/generic/short[35]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10536, "sim/multiplay/generic/short[36]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10537, "sim/multiplay/generic/short[37]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10538, "sim/multiplay/generic/short[38]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10539, "sim/multiplay/generic/short[39]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10540, "sim/multiplay/generic/short[40]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10541, "sim/multiplay/generic/short[41]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10542, "sim/multiplay/generic/short[42]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10543, "sim/multiplay/generic/short[43]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10544, "sim/multiplay/generic/short[44]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10545, "sim/multiplay/generic/short[45]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10546, "sim/multiplay/generic/short[46]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10547, "sim/multiplay/generic/short[47]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10548, "sim/multiplay/generic/short[48]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10549, "sim/multiplay/generic/short[49]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10550, "sim/multiplay/generic/short[50]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10551, "sim/multiplay/generic/short[51]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10552, "sim/multiplay/generic/short[52]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10553, "sim/multiplay/generic/short[53]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10554, "sim/multiplay/generic/short[54]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10555, "sim/multiplay/generic/short[55]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10556, "sim/multiplay/generic/short[56]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10557, "sim/multiplay/generic/short[57]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10558, "sim/multiplay/generic/short[58]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10559, "sim/multiplay/generic/short[59]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10560, "sim/multiplay/generic/short[60]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10561, "sim/multiplay/generic/short[61]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10562, "sim/multiplay/generic/short[62]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10563, "sim/multiplay/generic/short[63]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10564, "sim/multiplay/generic/short[64]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10565, "sim/multiplay/generic/short[65]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10566, "sim/multiplay/generic/short[66]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10567, "sim/multiplay/generic/short[67]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10568, "sim/multiplay/generic/short[68]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10569, "sim/multiplay/generic/short[69]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10570, "sim/multiplay/generic/short[70]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10571, "sim/multiplay/generic/short[71]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10572, "sim/multiplay/generic/short[72]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10573, "sim/multiplay/generic/short[73]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10574, "sim/multiplay/generic/short[74]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10575, "sim/multiplay/generic/short[75]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10576, "sim/multiplay/generic/short[76]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10577, "sim/multiplay/generic/short[77]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10578, "sim/multiplay/generic/short[78]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },
    { 10579, "sim/multiplay/generic/short[79]", simgear::props::INT, TT_SHORTINT,  V1_1_2_PROP_ID, NULL },

};
/*
* For the 2017.x version 2 protocol the properties are sent in two partitions,
* the first of these is a V1 protocol packet (which should be fine with all clients), and a V2 partition
* which will contain the newly supported shortint and fixed string encoding schemes.
* This is to possibly allow for easier V1/V2 conversion - as the packet can simply be truncated at the
* first V2 property based on ID.
*/
const int MAX_PARTITIONS = 2;
const unsigned int numProperties = (sizeof(sIdPropertyList) / sizeof(sIdPropertyList[0]));

static mINTINT mIdCounts;
static int done_init = 0;
void init_full_map()
{
    if (done_init)
        return;
    unsigned int id, i;
    unsigned int dupe_cnt = 0;
    vUINT dupes;
    for (i = 0; i < numProperties; i++)
    {
        id = sIdPropertyList[i].id;
        iINTINT j = mIdCounts.find(id);
        if (j == mIdCounts.end()) {
            mIdCounts[id] = 0;
        }
        else {
            dupe_cnt++;
            dupes.push_back(id);
        }
    }
    done_init = 1;
    if (dupe_cnt) {
        // 10: 10520 10521 10522 10523 10524 10525 10526 10527 10528 10529
        sprtf("Note: Have %d (%d) duplicated IDs...\n", dupe_cnt, (int)dupes.size());
        for (i = 0; i < dupe_cnt; i++) {
            id = dupes[i];
            SPRTF("%u ", id);
        }
        SPRTF("\n");
    }
}

void add_id_count(unsigned int id)
{
    if (!done_init)
        init_full_map();
    mIdCounts[id]++;
}

// Look up a property ID using binary search.
namespace
{
    struct ComparePropertyId
    {
        bool operator()(const IdPropertyList& lhs,
            const IdPropertyList& rhs)
        {
            return lhs.id < rhs.id;
        }
        bool operator()(const IdPropertyList& lhs,
            unsigned id)
        {
            return lhs.id < id;
        }
        bool operator()(unsigned id,
            const IdPropertyList& rhs)
        {
            return id < rhs.id;
        }
    };
}

const IdPropertyList* findProperty(unsigned id)
{
    std::pair<const IdPropertyList*, const IdPropertyList*> result
        = std::equal_range(sIdPropertyList, sIdPropertyList + numProperties, id,
            ComparePropertyId());
    if (result.first == result.second) {
        return 0;
    }
    else {
        return result.first;
    }
}




// #else // !USE PROTO_2

struct IdPropertyList1 {
    unsigned id;
    const char* name;
    sgp_Type type;
};

static const IdPropertyList1* findProperty1(unsigned id);

// A static map of protocol property id values to property paths,
// This should be extendable dynamically for every specific aircraft ...
// For now only that static list
static const IdPropertyList1 sIdPropertyList1[] = {
    { 100, "surface-positions/left-aileron-pos-norm",  sgp_FLOAT },
    { 101, "surface-positions/right-aileron-pos-norm", sgp_FLOAT },
    { 102, "surface-positions/elevator-pos-norm",      sgp_FLOAT },
    { 103, "surface-positions/rudder-pos-norm",        sgp_FLOAT },
    { 104, "surface-positions/flap-pos-norm",          sgp_FLOAT },
    { 105, "surface-positions/speedbrake-pos-norm",    sgp_FLOAT },
    { 106, "gear/tailhook/position-norm",              sgp_FLOAT },
    { 107, "gear/launchbar/position-norm",             sgp_FLOAT },
    { 108, "gear/launchbar/state",                     sgp_STRING },
    { 109, "gear/launchbar/holdback-position-norm",    sgp_FLOAT },
    { 110, "canopy/position-norm",                     sgp_FLOAT },
    { 111, "surface-positions/wing-pos-norm",          sgp_FLOAT },
    { 112, "surface-positions/wing-fold-pos-norm",     sgp_FLOAT },

    { 200, "gear/gear[0]/compression-norm",           sgp_FLOAT },
    { 201, "gear/gear[0]/position-norm",              sgp_FLOAT },
    { 210, "gear/gear[1]/compression-norm",           sgp_FLOAT },
    { 211, "gear/gear[1]/position-norm",              sgp_FLOAT },
    { 220, "gear/gear[2]/compression-norm",           sgp_FLOAT },
    { 221, "gear/gear[2]/position-norm",              sgp_FLOAT },
    { 230, "gear/gear[3]/compression-norm",           sgp_FLOAT },
    { 231, "gear/gear[3]/position-norm",              sgp_FLOAT },
    { 240, "gear/gear[4]/compression-norm",           sgp_FLOAT },
    { 241, "gear/gear[4]/position-norm",              sgp_FLOAT },

    { 300, "engines/engine[0]/n1",  sgp_FLOAT },
    { 301, "engines/engine[0]/n2",  sgp_FLOAT },
    { 302, "engines/engine[0]/rpm", sgp_FLOAT },
    { 310, "engines/engine[1]/n1",  sgp_FLOAT },
    { 311, "engines/engine[1]/n2",  sgp_FLOAT },
    { 312, "engines/engine[1]/rpm", sgp_FLOAT },
    { 320, "engines/engine[2]/n1",  sgp_FLOAT },
    { 321, "engines/engine[2]/n2",  sgp_FLOAT },
    { 322, "engines/engine[2]/rpm", sgp_FLOAT },
    { 330, "engines/engine[3]/n1",  sgp_FLOAT },
    { 331, "engines/engine[3]/n2",  sgp_FLOAT },
    { 332, "engines/engine[3]/rpm", sgp_FLOAT },
    { 340, "engines/engine[4]/n1",  sgp_FLOAT },
    { 341, "engines/engine[4]/n2",  sgp_FLOAT },
    { 342, "engines/engine[4]/rpm", sgp_FLOAT },
    { 350, "engines/engine[5]/n1",  sgp_FLOAT },
    { 351, "engines/engine[5]/n2",  sgp_FLOAT },
    { 352, "engines/engine[5]/rpm", sgp_FLOAT },
    { 360, "engines/engine[6]/n1",  sgp_FLOAT },
    { 361, "engines/engine[6]/n2",  sgp_FLOAT },
    { 362, "engines/engine[6]/rpm", sgp_FLOAT },
    { 370, "engines/engine[7]/n1",  sgp_FLOAT },
    { 371, "engines/engine[7]/n2",  sgp_FLOAT },
    { 372, "engines/engine[7]/rpm", sgp_FLOAT },
    { 380, "engines/engine[8]/n1",  sgp_FLOAT },
    { 381, "engines/engine[8]/n2",  sgp_FLOAT },
    { 382, "engines/engine[8]/rpm", sgp_FLOAT },
    { 390, "engines/engine[9]/n1",  sgp_FLOAT },
    { 391, "engines/engine[9]/n2",  sgp_FLOAT },
    { 392, "engines/engine[9]/rpm", sgp_FLOAT },

    { 800, "rotors/main/rpm", sgp_FLOAT },
    { 801, "rotors/tail/rpm", sgp_FLOAT },
    { 810, "rotors/main/blade[0]/position-deg",  sgp_FLOAT },
    { 811, "rotors/main/blade[1]/position-deg",  sgp_FLOAT },
    { 812, "rotors/main/blade[2]/position-deg",  sgp_FLOAT },
    { 813, "rotors/main/blade[3]/position-deg",  sgp_FLOAT },
    { 820, "rotors/main/blade[0]/flap-deg",  sgp_FLOAT },
    { 821, "rotors/main/blade[1]/flap-deg",  sgp_FLOAT },
    { 822, "rotors/main/blade[2]/flap-deg",  sgp_FLOAT },
    { 823, "rotors/main/blade[3]/flap-deg",  sgp_FLOAT },
    { 830, "rotors/tail/blade[0]/position-deg",  sgp_FLOAT },
    { 831, "rotors/tail/blade[1]/position-deg",  sgp_FLOAT },

    { 900, "sim/hitches/aerotow/tow/length",                       sgp_FLOAT },
    { 901, "sim/hitches/aerotow/tow/elastic-constant",             sgp_FLOAT },
    { 902, "sim/hitches/aerotow/tow/weight-per-m-kg-m",            sgp_FLOAT },
    { 903, "sim/hitches/aerotow/tow/dist",                         sgp_FLOAT },
    { 904, "sim/hitches/aerotow/tow/connected-to-property-node",   sgp_BOOL },
    { 905, "sim/hitches/aerotow/tow/connected-to-ai-or-mp-callsign",   sgp_STRING },
    { 906, "sim/hitches/aerotow/tow/brake-force",                  sgp_FLOAT },
    { 907, "sim/hitches/aerotow/tow/end-force-x",                  sgp_FLOAT },
    { 908, "sim/hitches/aerotow/tow/end-force-y",                  sgp_FLOAT },
    { 909, "sim/hitches/aerotow/tow/end-force-z",                  sgp_FLOAT },
    { 930, "sim/hitches/aerotow/is-slave",                         sgp_BOOL },
    { 931, "sim/hitches/aerotow/speed-in-tow-direction",           sgp_FLOAT },
    { 932, "sim/hitches/aerotow/open",                             sgp_BOOL },
    { 933, "sim/hitches/aerotow/local-pos-x",                      sgp_FLOAT },
    { 934, "sim/hitches/aerotow/local-pos-y",                      sgp_FLOAT },
    { 935, "sim/hitches/aerotow/local-pos-z",                      sgp_FLOAT },

    { 1001, "controls/flight/slats",  sgp_FLOAT },
    { 1002, "controls/flight/speedbrake",  sgp_FLOAT },
    { 1003, "controls/flight/spoilers",  sgp_FLOAT },
    { 1004, "controls/gear/gear-down",  sgp_FLOAT },
    { 1005, "controls/lighting/nav-lights",  sgp_FLOAT },
    { 1006, "controls/armament/station[0]/jettison-all",  sgp_BOOL },

    { 1100, "sim/model/variant", sgp_INT },
    { 1101, "sim/model/livery/file", sgp_STRING },

    { 1200, "environment/wildfire/data", sgp_STRING },
    { 1201, "environment/contrail", sgp_INT },

    { 1300, "tanker", sgp_INT },

    { 1400, "scenery/events", sgp_STRING },

    { 1500, "instrumentation/transponder/transmitted-id", sgp_INT },
    { 1501, "instrumentation/transponder/altitude", sgp_INT },
    { 1502, "instrumentation/transponder/ident", sgp_BOOL },
    { 1503, "instrumentation/transponder/inputs/mode", sgp_INT },

    { 10001, "sim/multiplay/transmission-freq-hz",  sgp_STRING },
    { 10002, "sim/multiplay/chat",  sgp_STRING },

    { 10100, "sim/multiplay/generic/string[0]", sgp_STRING },
    { 10101, "sim/multiplay/generic/string[1]", sgp_STRING },
    { 10102, "sim/multiplay/generic/string[2]", sgp_STRING },
    { 10103, "sim/multiplay/generic/string[3]", sgp_STRING },
    { 10104, "sim/multiplay/generic/string[4]", sgp_STRING },
    { 10105, "sim/multiplay/generic/string[5]", sgp_STRING },
    { 10106, "sim/multiplay/generic/string[6]", sgp_STRING },
    { 10107, "sim/multiplay/generic/string[7]", sgp_STRING },
    { 10108, "sim/multiplay/generic/string[8]", sgp_STRING },
    { 10109, "sim/multiplay/generic/string[9]", sgp_STRING },
    { 10110, "sim/multiplay/generic/string[10]", sgp_STRING },
    { 10111, "sim/multiplay/generic/string[11]", sgp_STRING },
    { 10112, "sim/multiplay/generic/string[12]", sgp_STRING },
    { 10113, "sim/multiplay/generic/string[13]", sgp_STRING },
    { 10114, "sim/multiplay/generic/string[14]", sgp_STRING },
    { 10115, "sim/multiplay/generic/string[15]", sgp_STRING },
    { 10116, "sim/multiplay/generic/string[16]", sgp_STRING },
    { 10117, "sim/multiplay/generic/string[17]", sgp_STRING },
    { 10118, "sim/multiplay/generic/string[18]", sgp_STRING },
    { 10119, "sim/multiplay/generic/string[19]", sgp_STRING },

    { 10200, "sim/multiplay/generic/float[0]", sgp_FLOAT },
    { 10201, "sim/multiplay/generic/float[1]", sgp_FLOAT },
    { 10202, "sim/multiplay/generic/float[2]", sgp_FLOAT },
    { 10203, "sim/multiplay/generic/float[3]", sgp_FLOAT },
    { 10204, "sim/multiplay/generic/float[4]", sgp_FLOAT },
    { 10205, "sim/multiplay/generic/float[5]", sgp_FLOAT },
    { 10206, "sim/multiplay/generic/float[6]", sgp_FLOAT },
    { 10207, "sim/multiplay/generic/float[7]", sgp_FLOAT },
    { 10208, "sim/multiplay/generic/float[8]", sgp_FLOAT },
    { 10209, "sim/multiplay/generic/float[9]", sgp_FLOAT },
    { 10210, "sim/multiplay/generic/float[10]", sgp_FLOAT },
    { 10211, "sim/multiplay/generic/float[11]", sgp_FLOAT },
    { 10212, "sim/multiplay/generic/float[12]", sgp_FLOAT },
    { 10213, "sim/multiplay/generic/float[13]", sgp_FLOAT },
    { 10214, "sim/multiplay/generic/float[14]", sgp_FLOAT },
    { 10215, "sim/multiplay/generic/float[15]", sgp_FLOAT },
    { 10216, "sim/multiplay/generic/float[16]", sgp_FLOAT },
    { 10217, "sim/multiplay/generic/float[17]", sgp_FLOAT },
    { 10218, "sim/multiplay/generic/float[18]", sgp_FLOAT },
    { 10219, "sim/multiplay/generic/float[19]", sgp_FLOAT },

    { 10300, "sim/multiplay/generic/int[0]", sgp_INT },
    { 10301, "sim/multiplay/generic/int[1]", sgp_INT },
    { 10302, "sim/multiplay/generic/int[2]", sgp_INT },
    { 10303, "sim/multiplay/generic/int[3]", sgp_INT },
    { 10304, "sim/multiplay/generic/int[4]", sgp_INT },
    { 10305, "sim/multiplay/generic/int[5]", sgp_INT },
    { 10306, "sim/multiplay/generic/int[6]", sgp_INT },
    { 10307, "sim/multiplay/generic/int[7]", sgp_INT },
    { 10308, "sim/multiplay/generic/int[8]", sgp_INT },
    { 10309, "sim/multiplay/generic/int[9]", sgp_INT },
    { 10310, "sim/multiplay/generic/int[10]", sgp_INT },
    { 10311, "sim/multiplay/generic/int[11]", sgp_INT },
    { 10312, "sim/multiplay/generic/int[12]", sgp_INT },
    { 10313, "sim/multiplay/generic/int[13]", sgp_INT },
    { 10314, "sim/multiplay/generic/int[14]", sgp_INT },
    { 10315, "sim/multiplay/generic/int[15]", sgp_INT },
    { 10316, "sim/multiplay/generic/int[16]", sgp_INT },
    { 10317, "sim/multiplay/generic/int[17]", sgp_INT },
    { 10318, "sim/multiplay/generic/int[18]", sgp_INT },
    { 10319, "sim/multiplay/generic/int[19]", sgp_INT }
};

const unsigned int numProperties1 = (sizeof(sIdPropertyList1)
    / sizeof(sIdPropertyList1[0]));

static const IdPropertyList1* findProperty1(unsigned id) {
    unsigned int ui;
    for (ui = 0; ui < numProperties1; ui++) {
        if (sIdPropertyList1[ui].id == id)
            return &sIdPropertyList1[ui];
    }
    return 0;
}

#endif // USE_PROTO_2 y/n

////////////////////////////////////////////////////////////////////////////////////
#ifdef USE_SIMGEAR  // TOCHECK SETPREVPOS MACRO
#define SETPREVPOS(p1,p2) { p1->ppx = p2->px; p1->ppy = p2->py; p1->ppz = p2->pz; }
#else // !USE_SIMGEAR
#define SETPREVPOS(p1,p2) { p1->PrevPos.Set( p2->SenderPosition.GetX(), p2->SenderPosition.GetY(), p2->SenderPosition.GetZ() ); }
#endif // USE_SIMGEAR y/n


#define SAME_FLIGHT(pp1,pp2)  ((strcmp(pp2->callsign, pp1->callsign) == 0)&&(strcmp(pp2->aircraft, pp1->aircraft) == 0))

static double elapsed_sim_time = 0.0;
static bool got_sim_time = false;

#ifdef USE_PROTO_2
int Deal_With_Properties(xdr_data_t * xdr, xdr_data_t * msgEnd, xdr_data_t * propsEnd)
{
    static char _s_text[MAX_TEXT_SIZE];
    char *cp;
    int prop_cnt = 0;
    xdr_data_t * bgn_xdr;
    while (xdr < msgEnd) {
        bgn_xdr = xdr;  // Keep the start location
        // First element is always the ID
        // int id = XDR_decode<int>(*xdr);
#ifdef USE_SIMGEAR
        unsigned id = XDR_decode_uint32(*xdr);
        /*
        * As we can detect a short int encoded value (by the upper word being non-zero) we can
        * do the decode here; set the id correctly, extract the integer and set the flag.
        * This can then be picked up by the normal processing based on the flag
        */
        int int_value = 0;
        bool short_int_encoded = false;
        if (id & 0xffff0000)
        {
            int v1, v2;
            XDR_decode_shortints32(*xdr, v1, v2);
            int_value = v2;
            id = v1;
            short_int_encoded = true;
        }
#else 
        unsigned id = XDR_decode<int>(*xdr);
#endif
        //cout << pData->id << " ";
        xdr++;
        const IdPropertyList* plist = findProperty(id);
        const IdPropertyList1* plist1 = findProperty1(id);  // get the previous 'list', if there is one...
        if (plist)
        {
            const char *dt = "UNK";
            int ival = 0;
            double val = 0.0;
            // uint32_t length = 0;
            uint32_t txtlen = 0;
            // uint32_t offset = 0;
            add_2_ids(id);
            // How we decode the remainder of the property depends on the type
            switch (plist->type) {
            case simgear::props::INT:
            case simgear::props::BOOL:
            case simgear::props::LONG:
                if (short_int_encoded)
                {
                    ival = int_value;
                    dt = "EINT";
                    //pData->type = simgear::props::INT;
                }
                else
                {
                    ival = XDR_decode_uint32(*xdr);
                    xdr++;
                    dt = "INT";
                }
                if (VERB5) {
                    SPRTF("[v5]: %u %s %s %d\n", id, plist->name, dt, ival);
                }
                //cout << pData->int_value << "\n";
                prop_cnt++;
                break;
            case simgear::props::FLOAT:
            case simgear::props::DOUBLE:
                if (short_int_encoded)
                {
                    switch (plist->TransmitAs)
                    {
                    case TT_SHORT_FLOAT_1:
                        val = (double)int_value / 10.0;
                        // pData->float_value = (double)int_value / 10.0;
                        dt = "FLOAT_1";
                        break;
                    case TT_SHORT_FLOAT_2:
                        val = (double)int_value / 100.0;
                        dt = "FLOAT_2";
                        break;
                    case TT_SHORT_FLOAT_3:
                        val = (double)int_value / 1000.0;
                        dt = "FLOAT_3";
                        break;
                    case TT_SHORT_FLOAT_4:
                        val = (double)int_value / 10000.0;
                        dt = "FLOAT_4";
                        break;
                    case TT_SHORT_FLOAT_NORM:
                        val = (double)int_value / 32767.0;
                        dt = "FLOAT_N";
                        break;
                    default:
                        break;
                    }
                }
                else
                {
                    val = XDR_decode_float(*xdr);
                    xdr++;
                    dt = "FLOAT";
                }
                if (VERB5) {
                    SPRTF("[v5]: %u %s %s %lf\n", id, plist->name, dt, val);
                }
                prop_cnt++;
                break;
            case simgear::props::STRING:
            case simgear::props::UNSPECIFIED:
                // if the string is using short int encoding then it is in the new format.
                if (short_int_encoded)
                {
                    uint32_t length = int_value;
                    //pData->string_value = new char[length + 1];
                    cp = _s_text;
                    char *cptr = (char*)xdr;
                    for (unsigned i = 0; i < length; i++)
                    {
                        //pData->string_value[i] = *cptr++;
                        cp[i] = *cptr++;
                    }
                    //pData->string_value[length] = '\0';
                    cp[length] = 0;
                    xdr = (xdr_data_t*)cptr;
                    dt = "STRING_N";
                }
                else {
                    // String is complicated. It consists of
                    // The length of the string
                    // The string itself
                    // Padding to the nearest 4-bytes.    
                    uint32_t length = XDR_decode_uint32(*xdr);
                    xdr++;
                    //cout << length << " ";
                    // Old versions truncated the string but left the length unadjusted.
                    if (length > MAX_TEXT_SIZE)
                        length = MAX_TEXT_SIZE;
                    //pData->string_value = new char[length + 1];
                    cp = _s_text;
                    //cout << " String: ";
                    for (unsigned i = 0; i < length; i++)
                    {
                        //pData->string_value[i] = (char)XDR_decode_int8(*xdr);
                        cp[i] = (char)XDR_decode_int8(*xdr);
                        xdr++;
                    }

                    cp[length] = '\0';
                    txtlen = length;
                    // Now handle the padding
                    while ((length % 4) != 0)
                    {
                        xdr++;
                        length++;
                        //cout << "0";
                    }
                    dt = "STRING";
                    //cout << "\n";
                }
                if (VERB9) {
                    SPRTF("[v5]: %u %s %s len %d: '%s'\n", id, plist->name, dt, txtlen, cp);
                } else if (VERB5) {
                    SPRTF("[v5]: %u %s %s len %d\n", id, plist->name, dt, txtlen);
                }
                prop_cnt++;
                break;
            default:
                cp = GetNxtBuf();
                sprintf(cp, "%s: Unknown Prop type %d\n", module, (int)id);
                if (add_2_list(cp))
                    SPRTF("%s", cp);
                xdr++;  // assume there was a following INT!
                break;
            }
            if (plist1)
            {
                // This will probably be the SAME???
            }
            else
            {
                if (VERB9)
                {
                    SPRTF("[v9]: Such a property did %d NOT exist in Version 1!\n", id);
                }
            }
        }
        else {
            cp = GetNxtBuf();
            sprintf(cp, "%s: %u: Not in the Prop list...\n", module, id);
            if (add_2_list(cp))
                SPRTF("%s", cp);
            xdr++;
        }
        if (VERB9 && show_consumed_bytes) {
            size_t consumed = ((char *)xdr - (char *)bgn_xdr);
            SPRTF("[v9]: %d bytes consumed by this property\n", (int)consumed);
        }
    }
    return prop_cnt;
}

#else // !USE_PROTO_2

int Deal_With_Properties(xdr_data_t * xdr, xdr_data_t * msgEnd, xdr_data_t * propsEnd)
{
    static char _s_text[MAX_TEXT_SIZE];
    xdr_data_t * txd;
    char *cp;
    int prop_cnt = 0;
    while (xdr < msgEnd) {
        // First element is always the ID
        // int id = XDR_decode<int>(*xdr);
#ifdef USE_SIMGEAR
        unsigned id = XDR_decode_uint32(*xdr);
        /*
        * As we can detect a short int encoded value (by the upper word being non-zero) we can
        * do the decode here; set the id correctly, extract the integer and set the flag.
        * This can then be picked up by the normal processing based on the flag
        */
        int int_value = 0;
        bool short_int_encoded = false;
        if (id & 0xffff0000)
        {
            int v1, v2;
            XDR_decode_shortints32(*xdr, v1, v2);
            int_value = v2;
            id = v1;
            short_int_encoded = true;
        }
#else 
        unsigned id = XDR_decode<int>(*xdr);
#endif
        //cout << pData->id << " ";
        xdr++;
        const IdPropertyList* plist = findProperty(id);
        if (plist) {
            int ival = 0, bval = 0, c = 0;
            float val = 0.0;
            uint32_t length = 0;
            uint32_t txtlen = 0;
            uint32_t offset = 0;
            // How we decode the remainder of the property depends on the type
            switch (plist->type) {
            case sgp_INT:
                //case props::LONG:
#ifdef USE_SIMGEAR
                ival = XDR_decode_int32(*xdr);
#else
                ival = XDR_decode<int>(*xdr);
#endif
                if (VERB5) {
                    SPRTF("%u %s %s %d\n", id, plist->name, type2stg(plist->type), ival);
                }
                xdr++;
                prop_cnt++;
                break;
            case sgp_BOOL:
#ifdef USE_SIMGEAR
                bval = XDR_decode_int32(*xdr);
#else
                bval = XDR_decode<int>(*xdr);
#endif
                if (VERB5) {
                    SPRTF("%u %s %s %s\n", id, plist->name, type2stg(plist->type),
                        (bval ? "True" : "False"));
                }
                xdr++;
                prop_cnt++;
                break;
            case sgp_FLOAT:
                //case props::DOUBLE:
            {
#ifdef USE_SIMGEAR
                val = XDR_decode_float(*xdr);
#else
                val = XDR_decode<float>(*xdr);
#endif
                //if (SGMisc<float>::isNaN(val))
                //    return false;
                if (VERB5) {
                    SPRTF("%u %s %s %f\n", id, plist->name, type2stg(plist->type), val);
                }
                xdr++;
                prop_cnt++;
                break;
            }
            case sgp_STRING:
                //case props::UNSPECIFIED:
            {
                // String is complicated. It consists of
                // The length of the string
                // The string itself
                // Padding to the nearest 4-bytes.
                // XXX Yes, each byte is padded out to a word! Too late
                // to change...
#ifdef USE_SIMGEAR
                length = XDR_decode_int32(*xdr);
#else
                length = XDR_decode<int>(*xdr);
#endif
                xdr++;
                txd = xdr;
                // Old versions truncated the string but left the length
                // unadjusted.
                if (length > MAX_TEXT_SIZE)
                    length = MAX_TEXT_SIZE;
                xdr += length;
                txtlen = length;
                // Now handle the padding
                while ((length % 4) != 0)
                {
                    xdr++;
                    length++;
                    //cout << "0";
                }
                if (VERB5) {
                    SPRTF("%u %s %s len %d\n", id, plist->name, type2stg(plist->type), txtlen);
                }
                cp = _s_text;
                offset = 0;
                while (txtlen--) {
#ifdef USE_SIMGEAR
                    int c = XDR_decode_int32(*txd);
#else
                    int c = XDR_decode<int>(*txd);
#endif
                    cp[offset++] = (char)c;
                    txd++;
                }
                cp[offset] = 0;
                if (offset && VERB9)
                    SPRTF("Text: '%s'\n", cp);
            }
            prop_cnt++;
            break;
            default:
                cp = GetNxtBuf();
                sprintf(cp, "%s: Unknown Prop type %d\n", module, (int)id);
                if (add_2_list(cp))
                    SPRTF("%s", cp);
                xdr++;
                break;
            }
        }
        else {
            cp = GetNxtBuf();
            sprintf(cp, "%s: %u: Not in the Prop list...\n", module, id);
            if (add_2_list(cp))
                SPRTF("%s", cp);
        }
    }
    return prop_cnt;
}
#endif // USE_PROTO_2 y/n

Packet_Type Deal_With_Packet(char *packet, int len)
{
    static CF_Pilot _s_new_pilot;
    // static char _s_tdchk[256];
    uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    T_PositionMsg*  PosMsg;
    PT_MsgHdr       MsgHdr;
    PCF_Pilot       pp, pp2;
    size_t          max, ii;
    //char           *upd_by;
    double          sseconds;
    // char           *tb = _s_tdchk;
    //bool            revived;
    time_t          curr_time = time(0);
    double          lat, lon, alt;
    double          px, py, pz;
    char           *pcs;
    int             i;
    char           *pm;

    pp = &_s_new_pilot;
    memset(pp, 0, sizeof(CF_Pilot)); // ensure new is ALL zero
    MsgHdr = (PT_MsgHdr)packet;
#ifdef USE_SIMGEAR
    MsgMagic = XDR_decode_uint32(MsgHdr->Magic);
    MsgId    = XDR_decode_uint32(MsgHdr->MsgId);
    MsgLen   = XDR_decode_uint32(MsgHdr->MsgLen);
    MsgProto = XDR_decode_uint32(MsgHdr->Version);
#else // !USE_SIMGEAR
    MsgMagic = XDR_decode<uint32_t>(MsgHdr->Magic);
    MsgId = XDR_decode<uint32_t>(MsgHdr->MsgId);
    MsgLen = XDR_decode<uint32_t>(MsgHdr->MsgLen);
    MsgProto = XDR_decode<uint32_t>(MsgHdr->Version);
#endif // USE_SIMGEAR y/n

    pcs = pp->callsign;
    for (i = 0; i < MAX_CALLSIGN_LEN; i++) {
        pcs[i] = MsgHdr->Callsign[i];
        if (MsgHdr->Callsign[i] == 0)
            break;
    }

    //if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
    if (!((MsgMagic == RELAY_MAGIC) || (MsgMagic == MSG_MAGIC)) || (MsgProto != PROTO_VER)) {
        SPRTF("%s: Invalid packet...\n", module);
        failed_cnt++;
        //if (len < (int)MsgLen) {
        //    return pkt_InvLen1;
        if (!((MsgMagic == RELAY_MAGIC) || (MsgMagic == MSG_MAGIC))) {
            return pkt_InvMag;
        }
        else if (!(MsgProto == PROTO_VER)) {
            return pkt_InvProto;
        }
        return pkt_Invalid;
    }

    pp->curr_time = curr_time; // set CURRENT time
    pp->last_seen = curr_time;  // and LAST SEEN time
    if (MsgId == POS_DATA_ID)
    {
        if (MsgLen < sizeof(T_MsgHdr) + sizeof(T_PositionMsg)) {
            SPRTF("%s: Invalid position packet... TOO SMALL! Only %d, expect min %d\n", module,
                (int)MsgLen, (int)(sizeof(T_MsgHdr) + sizeof(T_PositionMsg)));
            failed_cnt++;
            return pkt_InvPos;
        }
        // jump up the the position message
        PosMsg = (T_PositionMsg *)(packet + sizeof(T_MsgHdr));
        pp->prev_time = pp->curr_time;
#ifdef USE_SIMGEAR
        pp->sim_time = XDR_decode_double(PosMsg->time); // get SIM time
#else
        pp->sim_time = XDR_decode64<double>(PosMsg->time); // get SIM time
#endif
        pm = get_Model(PosMsg->Model);
        strcpy(pp->aircraft, pm);

        // SPRTF("%s: POS Packet %d of len %d, buf %d, cs %s, mod %s, time %lf\n", module, packet_cnt, MsgLen, len, pcs, pm, pp->sim_time);
        // get Sender address and port - need patch in fgms to pass this

#ifdef USE_SIMGEAR
        pp->SenderAddress = XDR_decode_uint32(MsgHdr->ReplyAddress);
        pp->SenderPort = XDR_decode_uint32(MsgHdr->ReplyPort);
        px = XDR_decode_double(PosMsg->position[X]);
        py = XDR_decode_double(PosMsg->position[Y]);
        pz = XDR_decode_double(PosMsg->position[Z]);
        pp->ox = XDR_decode_float(PosMsg->orientation[X]);
        pp->oy = XDR_decode_float(PosMsg->orientation[Y]);
        pp->oz = XDR_decode_float(PosMsg->orientation[Z]);
#else
        pp->SenderAddress = XDR_decode<uint32_t>(MsgHdr->ReplyAddress);
        pp->SenderPort = XDR_decode<uint32_t>(MsgHdr->ReplyPort);
        px = XDR_decode64<double>(PosMsg->position[X]);
        py = XDR_decode64<double>(PosMsg->position[Y]);
        pz = XDR_decode64<double>(PosMsg->position[Z]);
        pp->ox = XDR_decode<float>(PosMsg->orientation[X]);
        pp->oy = XDR_decode<float>(PosMsg->orientation[Y]);
        pp->oz = XDR_decode<float>(PosMsg->orientation[Z]);
#endif
        if ((px == 0.0) || (py == 0.0) || (pz == 0.0)) {
            failed_cnt++;
            return pkt_InvPos;
        }
        // speed in this read raw log app is *not* of the essence
        // so ALWAYS do the 'alternate' matchs FIRST
        pp->SenderPosition.Set(px, py, pz);
        sgCartToGeod(pp->SenderPosition, pp->GeodPoint);
        lat = pp->GeodPoint.GetX();
        lon = pp->GeodPoint.GetY();
        alt = pp->GeodPoint.GetZ(); // this is FEET
#ifdef USE_SIMGEAR // TOCHECK - Use SG functions

        SGVec3d position(px, py, pz);
        SGGeod GeodPoint;
        SGGeodesy::SGCartToGeod(position, GeodPoint);
        lat = GeodPoint.getLatitudeDeg();
        lon = GeodPoint.getLongitudeDeg();
        alt = GeodPoint.getElevationFt();
        pp->px = px;
        pp->py = py;
        pp->pz = pz;
        SGVec3f angleAxis(pp->ox, pp->oy, pp->oz);
        SGQuatf ecOrient = SGQuatf::fromAngleAxis(angleAxis);
        SGQuatf qEc2Hl = SGQuatf::fromLonLatRad((float)GeodPoint.getLongitudeRad(),
            (float)GeodPoint.getLatitudeRad());
        // The orientation wrt the horizontal local frame
        SGQuatf hlOr = conj(qEc2Hl)*ecOrient;
        float hDeg, pDeg, rDeg;
        hlOr.getEulerDeg(hDeg, pDeg, rDeg);
        pp->heading = hDeg;
        pp->pitch = pDeg;
        pp->roll = rDeg;
#else // #ifdef USE_SIMGEAR
#ifdef USE_GeographicLib
                                    // This is a test of using GeoGraphicLib version other calcs
        double dlat, dlon, dalt;
        if (!geoc_to_geod(&dlat, &dlon, &dalt, px, py, pz)) {
            double dist;
            if (!geod_distance(&dist, lat, lon, dlat, dlon)) {
                if (dist < 10.0) {
                    lat = dlat;
                    lon = dlon;
                    alt = (dalt * M2F); // METERS_TO_FEET;
                }
                else {
#ifndef NDEBUG
                    SPRTF("%s: Calc differ by %d m, %lf,%lf,%lf vs %lf,%lf,%lf! CHECK ME!\n", module,
                        (int)dist, lat, lon, alt, dlat, dlon, (dalt * M2F));
#endif
                }
            }
        }
#endif // USE_GeographicLib y/n
#endif // #ifdef USE_SIMGEAR y/n
        // what ever math used, set pilots position
        pp->lat = lat;;
        pp->lon = lon;
        pp->alt = alt;  // this is FEET
        if (alt <= -9990.0) {
            failed_cnt++;
            return pkt_InvHgt;
        }
#ifdef USE_SIMGEAR  // TOCHECK SG function to get speed
        SGVec3f linearVel;
        for (unsigned i = 0; i < 3; ++i)
            linearVel(i) = XDR_decode_float(PosMsg->linearVel[i]);
        pp->speed = norm(linearVel) * SG_METER_TO_NM * 3600.0;
#else // !#ifdef USE_SIMGEAR
        pp->SenderOrientation.Set(pp->ox, pp->oy, pp->oz);

        euler_get(lat, lon, pp->ox, pp->oy, pp->oz,
            &pp->heading, &pp->pitch, &pp->roll);

        pp->linearVel.Set(
            XDR_decode<float>(PosMsg->linearVel[X]),
            XDR_decode<float>(PosMsg->linearVel[Y]),
            XDR_decode<float>(PosMsg->linearVel[Z])
        );
        pp->angularVel.Set(
            XDR_decode<float>(PosMsg->angularVel[X]),
            XDR_decode<float>(PosMsg->angularVel[Y]),
            XDR_decode<float>(PosMsg->angularVel[Z])
        );
        pp->linearAccel.Set(
            XDR_decode<float>(PosMsg->linearAccel[X]),
            XDR_decode<float>(PosMsg->linearAccel[Y]),
            XDR_decode<float>(PosMsg->linearAccel[Z])
        );
        pp->angularAccel.Set(
            XDR_decode<float>(PosMsg->angularAccel[X]),
            XDR_decode<float>(PosMsg->angularAccel[Y]),
            XDR_decode<float>(PosMsg->angularAccel[Z])
        );
        pp->speed = cf_norm(pp->linearVel) * SG_METER_TO_NM * 3600.0;
#endif // #ifdef USE_SIMGEAR

        pos_cnt++;
        pp->expired = false;
        max = vPilots.size();
        for (ii = 0; ii < max; ii++) {
            pp2 = &vPilots[ii]; // search list for this pilots
            if (SAME_FLIGHT(pp, pp2)) {
                pp2->last_seen = curr_time; // ALWAYS update 'last_seen'
                //seconds = curr_time - pp2->curr_time; // seconds since last PACKET
                sseconds = pp->sim_time - pp2->first_sim_time;
                if (sseconds > elapsed_sim_time) {
                    double add = sseconds - elapsed_sim_time;
                    if (VERB9) {
                        SPRTF("[v9]: %s: Update elapsed from %lf by %lf to %lf, from cs %s, model %s\n", module,
                            elapsed_sim_time, add, sseconds,
                            pp->callsign, pp->aircraft);
                    }
                    elapsed_sim_time = sseconds;
                    got_sim_time = true;    // we have a rough sim time
                }
                sseconds = pp->sim_time - pp2->sim_time; // curr packet sim time minus last packet sim time
                // sseconds = pp->sim_time - pp2->first_sim_time;
#ifdef USE_SIMGEAR  // TOCHECK - SG to get diatance
                SGVec3d p1(pp->px, pp->py, pp->pz);       // current position
                SGVec3d p2(pp2->px, pp2->py, pp2->pz);    // previous position
                pp->dist_m = length(p2 - p1); // * SG_METER_TO_NM;
#else // !#ifdef USE_SIMGEAR
                pp->dist_m = (Distance(pp2->SenderPosition, pp->SenderPosition) * SG_NM_TO_METER); /** Nautical Miles to Meters */
#endif // #ifdef USE_SIMGEAR y/n
                // if (upd_by) {
                    //if (revived) {
                    //    pp->flight_id = get_epoch_id(); // establish NEW UNIQUE ID for flight
                    //    pp->first_sim_time = pp->sim_time;   // restart first sim time
                    //    pp->cumm_nm += pp2->total_nm;  // get cummulative nm
                    //    pp2->total_nm = 0.0;            // restart nm
                    //}
                    //else {
                pp->flight_id = pp2->flight_id; // use existing FID
                pp->first_sim_time = pp2->first_sim_time; // keep first sim time
                pp->first_time = pp2->first_time; // keep first epoch time
            // }
                pp->expired = false;
                pp->packetCount = pp2->packetCount + 1;
                pp->packetsDiscarded = pp2->packetsDiscarded;
                pp->prev_sim_time = pp2->sim_time;
                pp->prev_time = pp2->curr_time;
                // accumulate total distance travelled (in nm)
                pp->total_nm = pp2->total_nm + (pp->dist_m * SG_METER_TO_NM);
                SETPREVPOS(pp, pp2);  // copy POS to PrevPos to get distance travelled
                pp->curr_time = curr_time; // set CURRENT packet time
                *pp2 = *pp;     // UPDATE the RECORD with latest info
                // print_pilot(pp2, upd_by, pt_Pos);
                //if (revived)
                //    Pilot_Tracker_Connect(pp2);
                //else
                //    Pilot_Tracker_Position(pp2);
                break;
            }
        }
        if (ii < max) {
            same_count++;
            //  print_pilot(pp2,upd_by,pt_Pos);
            // print_pilot(pp, (char *)"C", pt_Pos);
            print_pilot(pp2, (char *)"S", pt_Pos);
        }
        else {
            pp->packetCount = 1;
            pp->packetsDiscarded = 0;
            pp->first_sim_time = pp->prev_sim_time = pp->sim_time;
            SETPREVPOS(pp, pp); // set as SAME as current
            pp->curr_time = curr_time; // set CURRENT packet time
            pp->pt = pt_New;
            pp->flight_id = get_epoch_id(); // establish UNIQUE ID for flight
            pp->dist_m = 0.0;
            pp->total_nm = 0.0;
            vPilots.push_back(*pp);
            print_pilot(pp, (char *)"N", pt_Pos);

        }

        ///////////////////////////////////////////////////////////////////////////////////////
        // DEAL WITH PROPERTIES
        xdr_data_t * xdr =(xdr_data_t *)(packet + sizeof(T_MsgHdr) + sizeof(T_PositionMsg));
        xdr_data_t * msgEnd = (xdr_data_t *)(packet + len);
        xdr_data_t * propsEnd = (xdr_data_t *)(packet + MAX_PACKET_SIZE);
        int prop_cnt = Deal_With_Properties(xdr, msgEnd, propsEnd);
        if (VERB5) {
            SPRTF("[v5]: Done position packet: len %d bytes, %d props...\n", len, prop_cnt);
        }
        return pkt_Pos;

    } else if (MsgId == CHAT_MSG_ID) {
     SPRTF("%s: CHAT Packet %d of len %d, buf %d, cs %s\n", module, packet_cnt, MsgLen, len, pcs);
     chat_cnt++;
     return pkt_Chat;
 }
 SPRTF("%s: Got ID %d! Not postion or chat packet...\n", module, MsgId);
 failed_cnt++;
 return pkt_Invalid;
}

static bool isMagicHdr(char *cp)
{
    if ((cp[0] == 'F') && (cp[1] == 'G') &&
        (cp[2] == 'F') && (cp[3] == 'S') &&
        (cp[4] == 0) && (cp[5] == 1) &&
        (cp[6] == 0) && (cp[7] == 1)) {
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////
// int get_next_block()
//
// Will process the current udp block - that is pass it to 
// Deal_With_Packet( buf, len )
// whihc 'decodes' the udp packet, and stores the 'live'
// pilots into the vector vPilots.
//
// Then will read any remaining data from the file to re-fill
// the buffer, and search for the 'next' udp packet.
//
// If fails to find 'next' udp packet will return 0 - sort of
// like end of file.
//
// Will return 1 if a next packet is found, thus is intended
// to be used like -
// while (get_next_block()) {
//      process flight data
// } else
// time to reset the file back to the beginning.
//
// The caller should take care of the timing of these get_next_Block()
// calls. During the processing of the udp packet, the double
// elapsed_sim_time variable will be advanced as sim time advances,
// and can be used to 'regulate' calls to get_next_block()
//
/////////////////////////////////////////////////////////////////

static int get_next_block()
{
    int key = 0;
    Packet_Type pt;
    size_t i, j, size = raw_data_size;  // MAX_RAW_LOG;
    char *cp = raw_log_buffer;
    if (cp && raw_block_size) {
        pt = Deal_With_Packet(cp, raw_block_size);
        packet_cnt++;
        if (pt < pkt_Max) sPktStr[pt].count++;  // set the packet stats
        j = 0;
        for (i = raw_block_size; i < size; i++) {
            cp[j++] = cp[i];
        }
        raw_log_remaining -= raw_block_size;    // reduce remaining in raw log
        raw_data_size = raw_data_size - raw_block_size;  // set size of remaining raw data after this packet
        if (raw_log_fp) {
            size = raw_block_size;  // size to read at end of buffer
            if (raw_log_remaining < size) {
                size = raw_log_remaining;
            }
            // load more dat at offset of used bloc size, potentially fill buffer
            size_t rd = fread(&cp[raw_data_size], 1, size, raw_log_fp);
            raw_block_size = 0;
            if (rd == size) {
                raw_data_size += size;  // bump by this read
                i = 0;
                // this has to be true - just a debug check
                //if (!isMagicHdr(&cp[i])) {
                //    goto Bad_Read;
                //}
                for (i = 4; i < raw_data_size; i++) {
                    if (isMagicHdr(&cp[i])) {
                        break;
                    }
                }
                raw_block_size = i; // either found next, or out of data, 
                key = 1;    // but have next block
            }
            else {
 // Bad_Read:
                if (feof(raw_log_fp)) {
                    if (VERB9)
                    {
                        SPRTF("[v9]: %s: Reached EOF!\n", module);
                    }
                }
                else {
                    SPRTF("%s: Failed to get the next raw block!\n", module);

                }
                fclose(raw_log_fp);
                raw_log_fp = 0;
                free(raw_log_buffer);
                raw_log_buffer = 0;
            }
        }
    }
    return key;
}


/////////////////////////////////////////////////////////////////
// int open_raw_log()
// 
// Open the raw log 'rb', allocate a read buffer, and
// search for the first udp block.
//
// If successful return 0, else 1 is an error
//
// If successful, next call should be to get_next_block()
// to process the current block, load data and find next.
//
////////////////////////////////////////////////////////////////
static int open_raw_log()
{
    const char *tf = usr_input;
    if (stat(tf, &sbuf)) {
        SPRTF("%s: Failed to stat '%s'!\n", module, tf);
        return 1;
    }
    size_t size = sbuf.st_size;
    if (size <= MAX_RAW_LOG) {
        SPRTF("%s: Files '%s' too small! Only %u bytes!\n", module, tf, (int)size);
        return 1;

    }
    raw_log_size = size;
    raw_log_remaining = size;
    if (size > MAX_RAW_LOG) {
        size = MAX_RAW_LOG;
    }
    char *cp = (char *)malloc(size);
    if (!cp) {
        SPRTF("%s: memory allocation FAILED\n", module);
        return 1;
    }
    FILE *fp = fopen(tf, "rb");
    if (!fp) {
        SPRTF("%s: Failed to open '%s'!\n", module, tf);
        free(cp);
        return 1;
    }
    size_t rd = fread(cp, 1, size, fp);
    if (rd != size) {
        SPRTF("%s: Failed read of '%s'! Req %u, got %u?\n", module, tf, (int)size, (int)rd);
        fclose(fp);
        free(cp);
        return 1;
    }

    size_t i, j, bgn;
    for (i = 0; i < size; i++) {
        if (isMagicHdr(&cp[i])) {
            if (i) {
                // UGH, not at head - move ALL data up to head
                bgn = i;
                j = 0;
                for (; i < size; i++) {
                    cp[j++] = cp[i];
                }
                rd = fread(&cp[j], 1, bgn, fp);    // top up the buffer from the file
                if (rd != bgn) {
                    SPRTF("%s: Failed read of '%s'! Req %u, got %u?\n", module, tf, (int)bgn, (int)rd);
                    fclose(fp);
                    free(cp);
                    return 1;
                }
            }
            // search for next
            for (i = 4; i < size; i++) {
                if (isMagicHdr(&cp[i])) {
                    // end of first block
                    raw_log_fp = fp;
                    raw_log_buffer = cp;
                    raw_block_size = i;
                    raw_data_size = size;
                    raw_bgn_secs = get_seconds();   // start of raw log reading
                    return 0;
                }
            }
        }
    }
    free(cp);
    if (fp)
        fclose(fp);
    SPRTF("%s: Failed find first udp packet in '%s'!\n", module, tf);
    return 1;
}

static int process_log() // actions of app
{
    size_t blk_cnt = 0;
    if (open_raw_log())
        return 1;
    while (get_next_block()) {
        blk_cnt++;
    }
    if (VERB1)
    {
        size_t ii, len = vIdsUsed.size();
        SPRTF("%s: Processed %d mp packets... using %d of %d prop ids...\n", module,
            (int)(blk_cnt ? blk_cnt + 1 : blk_cnt),
            (int)len,
            (int)numProperties
        );
        if (len) {
            uint32_t id, cnt, havecnt = 0, total = 0;
            iINTINT p;
            const char *pt;
            const IdPropertyList *list;
            if (VERB5) {
                for (p = mIdCounts.begin(); p != mIdCounts.end(); p++)
                {
                    cnt = p->second;
                    if (cnt)
                        havecnt++;
                    total++;
                }
                SPRTF("[v5]: Show of %d of %d properties that have a count...\n", (int)havecnt, (int)total);
                     //    10    20 sim/multiplay/protocol-version
                SPRTF("Id     Count Property\n");
                for (p = mIdCounts.begin(); p != mIdCounts.end(); p++)
                {
                    id = p->first;
                    cnt = p->second;
                    if (cnt) {
                        list = findProperty(id);
                        if (list) {
                            pt = type2stg(list->type);
                            SPRTF("%5u %5u %s %s\n", id, cnt, list->name, pt);
                        }
                        else
                            SPRTF("%5u %5u\n", id, cnt);
                    }
                }
                if (VERB9) {
                    // show all entries that have NO count
                    SPRTF("[v9]: Show of %d of %d properties that have NO count...\n", (int)(total - havecnt), (int)total);
                    SPRTF("Id     Count Property\n");
                    for (p = mIdCounts.begin(); p != mIdCounts.end(); p++)
                    {
                        id = p->first;
                        cnt = p->second;
                        if (!cnt) {
                            list = findProperty(id);
                            if (list) {
                                pt = type2stg(list->type);
                                SPRTF("%5u %5u %s %s\n", id, cnt, list->name, pt);
                            }
                            else
                                SPRTF("%5u %5u\n", id, cnt);
                        }

                    }
                }
            }
            else if (VERB2) {
                // using default comparison (operator <):
                std::sort(vIdsUsed.begin(), vIdsUsed.end());           //(12 32 45 71)26 80 53 33
                // just show simple oneline list - no counts
                SPRTF("[v2]: Props %d ids: ", (int)len);
                for (ii = 0; ii < len; ii++)
                    SPRTF("%d ", vIdsUsed[ii]);
                SPRTF("\n");
            }
        }
    }
    return 0;
}

int chk_log_file(int argc, char **argv)
{
    int i, i2, c;
    char *arg, *sarg;
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        i2 = i + 1;
        if (*arg == '-') {
            sarg = &arg[1];
            while (*sarg == '-')
                sarg++;
            c = *sarg;
            switch (c) {
            case 'l':
                if (i2 < argc) {
                    i++;
                    sarg = argv[i];
                    def_log = strdup(sarg);
                    break;
                }
                else {
                    fprintf(stderr, "Expected log file to follow '%s'!\n", arg);
                    return 1;
                }
            }
        }
    }
    set_log_file((char *)def_log, false);
    return 0;
}

void clean_up()
{
    vWarnings.clear();
    vIdsUsed.clear();
    vPilots.clear();
    mIdCounts.clear();
}

void Create_Prop_Packet();

// main() OS entry
int main( int argc, char **argv )
{
    int iret = 0;
    iret = chk_log_file(argc, argv);
    if (iret)
        return iret;
    iret = parse_args(argc,argv);
    if (iret) {
        if (iret == 2)
            iret = 0;
        return iret;
    }

    if (do_packet_test) {
        Create_Prop_Packet();
    }

    iret = process_log(); // TODO: actions of app
    show_packet_stats();
    show_warnings();
    clean_up();
    return iret;
}

/////////////////////////////////////////////////////////////////////////
//// Create a full property packet
// short FGMultiplayMgr::get_scaled_short(double v, double scale)
short get_scaled_short(double v, double scale)
{
    float nv = v * scale;
    if (nv >= 32767) return 32767;
    if (nv <= -32767) return -32767;
    short rv = (short)nv;
    return rv;
}

void Create_Prop_Packet()
{
    int protocolVersion = 2;
    unsigned int i, pid;
    char Msg[MAX_PACKET_SIZE * 3];
    xdr_data_t *ptr = reinterpret_cast<xdr_data_t*>(Msg + sizeof(T_MsgHdr) + sizeof(T_PositionMsg));
    // xdr_data_t *msgEnd = reinterpret_cast<xdr_data_t*>(Msg + MAX_PACKET_SIZE);
    xdr_data_t *msgEnd = reinterpret_cast<xdr_data_t*>(Msg + (MAX_PACKET_SIZE * 3));
    int partition = 1;
    int propsDone = 0;
    int partcount[4];
    vUINT vIdsAdded;

    for (i = 0; i < 4; i++)
        partcount[i] = 0;

    for (partition = 1; partition <= protocolVersion; partition++)
    {
        for (i = 0; i < numProperties; i++)
        {
            // std::vector<FGPropertyData*>::const_iterator it = motionInfo.properties.begin();
            // while (it != motionInfo.properties.end()) {
            pid = sIdPropertyList[i].id;
            const struct IdPropertyList* propDef = &sIdPropertyList[i]; // mPropertyDefinition[(*it)->id];
            
            //if (pid > 10319)
            //    break;

            if (propDef->version == partition || propDef->version > protocolVersion) // getProtocolToUse())
            {
                if (ptr + 2 >= msgEnd)
                {
                    // SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer packet truncated prop id: " << (*it)->id << ": " << propDef->name);
                    SPRTF("Multiplayer packet truncated prop id: %d\nnode: %s\n", pid, propDef->name);
                    break;
                }

                // First element is the ID. Write it out when we know we have room for
                // the whole property.
                xdr_data_t id = XDR_encode_uint32(pid); // (*it)->id);


                /*
                * 2017.2 protocol has the ability to transmit as a different type (to save space), so
                * process this when using this protocol (protocolVersion 2) or later
                */
                int transmit_type = propDef->type;  // (*it)->type;

                if (propDef->TransmitAs != TT_ASIS && protocolVersion > 1)
                {
                    transmit_type = propDef->TransmitAs;
                }
#if 0 // 00000000000000000000000000000000000000000
                if (pMultiPlayDebugLevel->getIntValue() & 2)
                    SG_LOG(SG_NETWORK, SG_INFO,
                        "[SEND] pt " << partition <<
                        ": buf[" << (ptr - data) * sizeof(*ptr)
                        << "] id=" << (*it)->id << " type " << transmit_type);
#endif // 0000000000000000000000000000000000000000

                // The actual data representation depends on the type
                switch (transmit_type) {
                case TT_SHORTINT:
                {
                    //*ptr++ = XDR_encode_shortints32((*it)->id, (*it)->int_value);
                    *ptr++ = XDR_encode_shortints32(pid, 0);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }
                case TT_SHORT_FLOAT_1:
                {
                    //short value = get_scaled_short((*it)->float_value, 10.0);
                    //*ptr++ = XDR_encode_shortints32((*it)->id, value);
                    short value = get_scaled_short(0.0, 10.0);
                    *ptr++ = XDR_encode_shortints32(pid, value);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }
                case TT_SHORT_FLOAT_2:
                {
                    //short value = get_scaled_short((*it)->float_value, 100.0);
                    //*ptr++ = XDR_encode_shortints32((*it)->id, value);
                    short value = get_scaled_short(0.0, 100.0);
                    *ptr++ = XDR_encode_shortints32(pid, value);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }
                case TT_SHORT_FLOAT_3:
                {
                    //short value = get_scaled_short((*it)->float_value, 1000.0);
                    //*ptr++ = XDR_encode_shortints32((*it)->id, value);
                    short value = get_scaled_short(0.0, 1000.0);
                    *ptr++ = XDR_encode_shortints32(pid, value);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }
                case TT_SHORT_FLOAT_4:
                {
                    //short value = get_scaled_short((*it)->float_value, 10000.0);
                    //*ptr++ = XDR_encode_shortints32((*it)->id, value);
                    short value = get_scaled_short(0.0, 10000.0);
                    *ptr++ = XDR_encode_shortints32(pid, value);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }

                case TT_SHORT_FLOAT_NORM:
                {
                    //short value = get_scaled_short((*it)->float_value, 32767.0);
                    //*ptr++ = XDR_encode_shortints32((*it)->id, value);
                    short value = get_scaled_short(0.0, 32767.0);
                    *ptr++ = XDR_encode_shortints32(pid, value);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }

                case simgear::props::INT:
                case simgear::props::BOOL:
                case simgear::props::LONG:
                    *ptr++ = id;
                    //*ptr++ = XDR_encode_uint32((*it)->int_value);
                    *ptr++ = XDR_encode_uint32(0);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                case simgear::props::FLOAT:
                case simgear::props::DOUBLE:
                    *ptr++ = id;
                    //*ptr++ = XDR_encode_float((*it)->float_value);
                    *ptr++ = XDR_encode_float(0.0);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                case simgear::props::STRING:
                case simgear::props::UNSPECIFIED:
                {
                    if (protocolVersion > 1)
                    {
                        // New string encoding:
                        // xdr[0] : ID length packed into 32 bit containing two shorts.
                        // xdr[1..len/4] The string itself (char[length])
                        //const char* lcharptr = (*it)->string_value;
                        const char* lcharptr = "";

                        if (lcharptr != 0)
                        {
                            uint32_t len = strlen(lcharptr);

                            if (len >= MAX_TEXT_SIZE)
                            {
                                len = MAX_TEXT_SIZE - 1;
                                //SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer property truncated at MAX_TEXT_SIZE in string " << (*it)->id);
                                SPRTF("Multiplayer property truncated at MAX_TEXT_SIZE in string id %d\n", pid);
                            }

                            char *encodeStart = (char*)ptr;
                            char *msgEndbyte = (char*)msgEnd;

                            if (encodeStart + 2 + len >= msgEndbyte)
                            {
                                //SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer property not sent (no room) string " << (*it)->id);
                                SPRTF("Multiplayer property not sent (no room) string %d\n", pid);
                                goto escape;
                            }

                            //*ptr++ = XDR_encode_shortints32((*it)->id, len);
                            *ptr++ = XDR_encode_shortints32(pid, len);
                            encodeStart = (char*)ptr;
                            if (len != 0)
                            {
                                int lcount = 0;
                                while (*lcharptr && (lcount < MAX_TEXT_SIZE))
                                {
                                    if (encodeStart + 2 >= msgEndbyte)
                                    {
                                        //SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer packet truncated in string " << (*it)->id << " lcount " << lcount);
                                        SPRTF("Multiplayer packet truncated in string %d, lcount %d\n", pid, lcount);
                                        break;
                                    }
                                    *encodeStart++ = *lcharptr++;
                                    lcount++;
                                }
                            }
                            ptr = (xdr_data_t*)encodeStart;
                            propsDone++;
                            partcount[partition]++;
                            vIdsAdded.push_back(pid);
                        }
                        else
                        {
                            // empty string, just send the id and a zero length
                            *ptr++ = id;
                            *ptr++ = XDR_encode_uint32(0);
                            propsDone++;
                            partcount[partition]++;
                            vIdsAdded.push_back(pid);
                        }
                    }
                    else {

                        // String is complicated. It consists of
                        // The length of the string
                        // The string itself
                        // Padding to the nearest 4-bytes.        
                        // const char* lcharptr = (*it)->string_value;
                        const char* lcharptr = "";

                        if (lcharptr != 0)
                        {
                            // Add the length         
                            ////cout << "String length: " << strlen(lcharptr) << "\n";
                            uint32_t len = strlen(lcharptr);
                            if (len >= MAX_TEXT_SIZE)
                            {
                                len = MAX_TEXT_SIZE - 1;
                                //SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer property truncated at MAX_TEXT_SIZE in string " << (*it)->id);
                                SPRTF("Multiplayer property truncated at MAX_TEXT_SIZE in string %d\n", pid );
                            }

                            // XXX This should not be using 4 bytes per character!
                            // If there's not enough room for this property, drop it
                            // on the floor.
                            if (ptr + 2 + ((len + 3) & ~3) >= msgEnd)
                            {
                                // SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer property not sent (no room) string " << (*it)->id);
                                SPRTF("Multiplayer property not sent (no room) string %d\n", pid );
                                goto escape;
                            }
                            //cout << "String length unint32: " << len << "\n";
                            *ptr++ = id;
                            *ptr++ = XDR_encode_uint32(len);
                            if (len != 0)
                            {
                                // Now the text itself
                                // XXX This should not be using 4 bytes per character!
                                int lcount = 0;
                                while ((*lcharptr != '\0') && (lcount < MAX_TEXT_SIZE))
                                {
                                    if (ptr + 2 >= msgEnd)
                                    {
                                        //SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer packet truncated in string " << (*it)->id << " lcount " << lcount);
                                        SPRTF("Multiplayer packet truncated in string %d, lcount %d\n", pid, lcount);
                                        break;
                                    }
                                    *ptr++ = XDR_encode_int8(*lcharptr);
                                    lcharptr++;
                                    lcount++;
                                }
                                // Now pad if required
                                while ((lcount % 4) != 0)
                                {
                                    if (ptr + 2 >= msgEnd)
                                    {
                                        // SG_LOG(SG_NETWORK, SG_ALERT, "Multiplayer packet truncated in string " << (*it)->id << " lcount " << lcount);
                                        SPRTF("Multiplayer packet truncated in string %d, lcount %d\n", pid, lcount);
                                        break;
                                    }
                                    *ptr++ = XDR_encode_int8(0);
                                    lcount++;
                                }
                            }
                            propsDone++;
                            partcount[partition]++;
                            vIdsAdded.push_back(pid);
                        }
                        else
                        {
                            // Nothing to encode
                            *ptr++ = id;
                            *ptr++ = XDR_encode_uint32(0);
                            propsDone++;
                            partcount[partition]++;
                            vIdsAdded.push_back(pid);
                        }
                    }
                }
                break;

                default:
                    *ptr++ = id;
                    //*ptr++ = XDR_encode_float((*it)->float_value);;
                    *ptr++ = XDR_encode_float(0.0);
                    propsDone++;
                    partcount[partition]++;
                    vIdsAdded.push_back(pid);
                    break;
                }
            }
            //++it;
        }
    }
escape:
    unsigned int msgLen = reinterpret_cast<char*>(ptr) - Msg;   // msgBuf.Msg;
    SPRTF("Done %d of %d v2 properties (v1=%d)... partition %d... ", propsDone, numProperties, numProperties1, partition);
    for (i = 0; i < 4; i++) {
        if (partcount[i])
            SPRTF("%d: %d ", i, partcount[i]);
    }
    SPRTF("\n");
    SPRTF("Have test message of length %d...\n", msgLen);
    size_t ii, max = vIdsAdded.size();
    SPRTF("Total %d ID added...\n", (int)max);
    mINTINT mIds;
    for (ii = 0; ii < max; ii++)
    {
        pid = vIdsAdded[ii];
        const IdPropertyList *list = findProperty(pid);
        if (list) {
            SPRTF("%5d %s v%d ", pid, list->name, list->version);
        }
        else {
            SPRTF("%5d NO LIST ", pid);
        }
        // check for DUPLICATES
        iINTINT j = mIds.find(pid);
        if (j == mIds.end()) {
            mIds[pid] = 0;
        }
        else {
            SPRTF("*** DUPLICATE *** ");
        }

        SPRTF("\n");
    }

    exit(1);
}

// eof = raw-log.cxx

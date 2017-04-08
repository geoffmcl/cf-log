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
#include <vector>
#include <string>
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
#include "tiny_xdr.hxx"
#include "mpMsgs.hxx"
#include "cf_misc.hxx"

#ifndef SPRTF
#define SPRTF printf
#endif

typedef std::vector<std::string> vSTG;

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

static int verbosity = 0;
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

static const char *sample = "F:\\Projects\\cf-log\\data\\sampleudp01.log";

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

void show_warnings()
{
    std::string s;
    size_t ii, len = vWarnings.size();
    SPRTF("Repeating %d warnings...\n", (int)len);
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

}


void give_help( char *name )
{
    SPRTF("%s: usage: [options] usr_input\n", module);
    SPRTF("Options:\n");
    SPRTF(" --help  (-h or -?) = This help and exit(0)\n");
    SPRTF(" --verb[n]     (-v) = Bump or set verbosity to n. Values 0,1,2,5,9 (def=%d)\n", verbosity);
    // TODO: More help
    SPRTF("\n");
    SPRTF("Read and decode a raw FGFS mp packet.\n");
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
    // if (!VERB9) return;
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

////////////////////////////////////////////////////////////////////////////////////
struct IdPropertyList {
    unsigned id;
    const char* name;
    sgp_Type type;
};

static const IdPropertyList* findProperty(unsigned id);

// A static map of protocol property id values to property paths,
// This should be extendable dynamically for every specific aircraft ...
// For now only that static list
static const IdPropertyList sIdPropertyList[] = {
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

const unsigned int numProperties = (sizeof(sIdPropertyList)
    / sizeof(sIdPropertyList[0]));

static const IdPropertyList* findProperty(unsigned id) {
    unsigned int ui;
    for (ui = 0; ui < numProperties; ui++) {
        if (sIdPropertyList[ui].id == id)
            return &sIdPropertyList[ui];
    }
    return 0;
}



////////////////////////////////////////////////////////////////////////////////////
#ifdef USE_SIMGEAR  // TOCHECK SETPREVPOS MACRO
#define SETPREVPOS(p1,p2) { p1->ppx = p2->px; p1->ppy = p2->py; p1->ppz = p2->pz; }
#else // !USE_SIMGEAR
#define SETPREVPOS(p1,p2) { p1->PrevPos.Set( p2->SenderPosition.GetX(), p2->SenderPosition.GetY(), p2->SenderPosition.GetZ() ); }
#endif // USE_SIMGEAR y/n


#define SAME_FLIGHT(pp1,pp2)  ((strcmp(pp2->callsign, pp1->callsign) == 0)&&(strcmp(pp2->aircraft, pp1->aircraft) == 0))

static double elapsed_sim_time = 0.0;
static bool got_sim_time = false;

Packet_Type Deal_With_Packet(char *packet, int len)
{
    static CF_Pilot _s_new_pilot;
    static char _s_tdchk[256];
    static char _s_text[MAX_TEXT_SIZE];

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
    char           *tb = _s_tdchk;
    //bool            revived;
    time_t          curr_time = time(0);
    double          lat, lon, alt;
    double          px, py, pz;
    char           *pcs;
    int             i;
    char           *pm;
    char           *cp;

    pp = &_s_new_pilot;
    memset(pp, 0, sizeof(CF_Pilot)); // ensure new is ALL zero
    MsgHdr = (PT_MsgHdr)packet;
    MsgMagic = XDR_decode<uint32_t>(MsgHdr->Magic);
    MsgId = XDR_decode<uint32_t>(MsgHdr->MsgId);
    MsgLen = XDR_decode<uint32_t>(MsgHdr->MsgLen);
    MsgProto = XDR_decode<uint32_t>(MsgHdr->Version);

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
        pp->sim_time = XDR_decode64<double>(PosMsg->time); // get SIM time
        pm = get_Model(PosMsg->Model);
        strcpy(pp->aircraft, pm);

        // SPRTF("%s: POS Packet %d of len %d, buf %d, cs %s, mod %s, time %lf\n", module, packet_cnt, MsgLen, len, pcs, pm, pp->sim_time);
        // get Sender address and port - need patch in fgms to pass this
        pp->SenderAddress = XDR_decode<uint32_t>(MsgHdr->ReplyAddress);
        pp->SenderPort = XDR_decode<uint32_t>(MsgHdr->ReplyPort);
        px = XDR_decode64<double>(PosMsg->position[X]);
        py = XDR_decode64<double>(PosMsg->position[Y]);
        pz = XDR_decode64<double>(PosMsg->position[Z]);
        pp->ox = XDR_decode<float>(PosMsg->orientation[X]);
        pp->oy = XDR_decode<float>(PosMsg->orientation[Y]);
        pp->oz = XDR_decode<float>(PosMsg->orientation[Z]);
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
            linearVel(i) = XDR_decode<float>(PosMsg->linearVel[i]);
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
                        SPRTF("%s: Update elapsed from %lf by %lf to %lf, from cs %s, model %s\n", module,
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
        xdr_data_t * txd;
        xdr_data_t * xdr =(xdr_data_t *)(packet + sizeof(T_MsgHdr) + sizeof(T_PositionMsg));
        xdr_data_t * msgEnd = (xdr_data_t *)(packet + len);
        xdr_data_t * propsEnd = (xdr_data_t *)(packet + MAX_PACKET_SIZE);
        while (xdr < msgEnd) {
            // First element is always the ID
            //unsigned id = XDR_decode_uint32(*xdr);
            int id = XDR_decode<int>(*xdr);
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
                    ival = XDR_decode<int>(*xdr);
                    if (VERB5) {
                        SPRTF("%u %s %s %d\n", id, plist->name, type2stg(plist->type), ival);
                    }
                    xdr++;
                    break;
                case sgp_BOOL:
                    bval = XDR_decode<int>(*xdr);
                    if (VERB5) {
                        SPRTF("%u %s %s %s\n", id, plist->name, type2stg(plist->type),
                            (bval ? "True" : "False"));
                    }
                    xdr++;
                    break;
                case sgp_FLOAT:
                //case props::DOUBLE:
                {
                    val = XDR_decode<float>(*xdr);
                    //if (SGMisc<float>::isNaN(val))
                    //    return false;
                    if (VERB5) {
                        SPRTF("%u %s %s %f\n", id, plist->name, type2stg(plist->type), val);
                    }
                    xdr++;
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
                    length = XDR_decode<int>(*xdr);
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
                        int c = XDR_decode<int>(*txd);
                        cp[offset++] = (char)c;
                        txd++;
                    }
                    cp[offset] = 0;
                    if (offset && VERB9)
                        SPRTF("Text: '%s'\n", cp);
                }
                break;
                default:
                    cp = GetNxtBuf();
                    sprintf(cp,"%s: Unknown Prop type %d\n", module, (int)id);
                    if (add_2_list(cp))
                        SPRTF("%s", cp);
                    xdr++;
                    break;
                }
            }
            else {
                cp = GetNxtBuf();
                sprintf(cp,"%s: %u: Not in the Prop list...\n", module, id);
                if (add_2_list(cp))
                    SPRTF("%s", cp);
            }


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
                    SPRTF("%s: Reached EOF!\n", module);
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

    SPRTF("%s: Processed %d mp packets...\n", module, 
        (int) (blk_cnt ? blk_cnt + 1 : blk_cnt));

    return 0;
}


// main() OS entry
int main( int argc, char **argv )
{
    int iret = 0;
    set_log_file((char *)def_log, false);
    iret = parse_args(argc,argv);
    if (iret) {
        if (iret == 2)
            iret = 0;
        return iret;
    }

    iret = process_log(); // TODO: actions of app
    show_packet_stats();
    show_warnings();

    return iret;
}


// eof = raw-log.cxx

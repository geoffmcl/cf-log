/*\
 * cf-pilot.cxx
 *
 * Copyright (c) 2014 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/

#include <stdio.h>
#include <vector>
#include <time.h>
#ifndef _MSC_VER
#include <string.h> // for strcpy(), ...
#include <stdlib.h> // for abs(), ...
#endif // !_MSC_VER
#ifdef USE_SIMGEAR
// so use SGMath.hxx
#include <simgear/compiler.h>
#include <simgear/constants.h>
#include <simgear/math/SGMath.hxx>
#else
// use substitute maths
#include "fg_geometry.hxx"
#include "cf_euler.hxx"
#endif
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "mpMsgs.hxx"
#include "tiny_xdr.hxx"
#include "cf-log.hxx"
#include "cf-pilot.hxx"

static const char *module = "cf-pilot";
#define mod_name module

static size_t pos_cnt = 0;
static size_t chat_cnt = 0;
static size_t failed_cnt = 0;
static size_t discard_cnt = 0;
size_t packet_cnt = 0;
double elapsed_sim_time = 0.0;
bool got_sim_time = false;

void show_packets()
{
    SPRTF("%s: Packets %d, pos %d, discard %d, failed %d, chat %d.\n", module,
        (int)packet_cnt, (int)pos_cnt, (int)discard_cnt, (int)failed_cnt, (int)chat_cnt);
    if (VERB1) {
        double elap = get_seconds() - app_bgn_secs;
        if (elap > 0.0) {
            double rate = (double)packet_cnt / elap;
            SPRTF("%s: Rate %lf pkts/sec, elapsed %s\n", module,
                rate, get_seconds_stg(elap) );
        }
    }
}


#ifndef MEOL
#ifdef WIN32
#define MEOL "\r\n"
#else
#define MEOL "\n"
#endif
#endif

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
enum { X, Y, Z };
enum { Lat, Lon, Alt };
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
#else // #ifdef USE_SIMGEAR
    Point3D         SenderPosition;
    Point3D         PrevPos;
    Point3D         SenderOrientation;
    Point3D         GeodPoint;
    Point3D  linearVel, angularVel,  linearAccel, angularAccel;      
#endif // #ifdef USE_SIMGEAR y/n
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

time_t m_PlayerExpires = 10;     // standard expiration period (seconds)
double m_MinDistance_m = 2000.0;  // started at 100.0;   // got movement (meters)
int m_MinSpdChange_kt = 20;
int m_MinHdgChange_deg = 1;
int m_MinAltChange_ft = 100;

static int m_ExpiredCnt = 0;    // total of expired in vector
// maybe when this reaches some maximum watermark, they are 
// all erased, but be warned erasing in a vector causes a
// reallocation of ALL vector memory from the erase point to the end
static int m_MaxExpired = 100;
#define ADD_VECTOR_ERASE

void clean_up_pilots( bool clear )
{
    size_t exp, ii, max = vPilots.size();
    exp = 0;
    PCF_Pilot pp;
    for (ii = 0; ii < max; ii++) {
        pp = &vPilots[ii];
        if ( pp->expired ) {
            exp++;
        }
    }
    if (max) {
        SPRTF("%s: Have %d entries, %d active pilots, %d expired, in list. (cld=%s)\n", module,
            (int)max, (int)(max - exp), (int)exp,
            (clear ? "yes" : "no") );
    }
    if (clear) {
        vPilots.clear();
    }

}

//////////////////////////////////////////////////////////////////////
// Rather rough service to remove leading PATH
// and remove trailing file extension
char *get_Model( char *pm )
{
    static char _s_buf[MAX_MODEL_NAME_LEN+4];
    int i, c, len;
    char *cp = _s_buf;
    char *model = pm;
    len = MAX_MODEL_NAME_LEN;
    for (i = 0; i < len; i++) {
        c = pm[i];
        if (c == '/')
            model = &pm[i+1];
        else if (c == 0)
            break;
    }
    strcpy(cp,model);
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

///////////////////////////////////////////////////////////////////////////
// Essentially just a DEBUG service
#define ADD_ORIENTATION
void print_pilot(PCF_Pilot pp, char *pm, Pilot_Type pt)
{
    if (!VERB9) return;
    char * cp = GetNxtBuf();
    //struct in_addr in;
    int ialt;
    double dlat, dlon, dalt;

    //in.s_addr = pp->SenderAddress;
    dlat = pp->lat;
    dlon = pp->lon;
    dalt = pp->alt;

    ialt = (int) (dalt + 0.5);
#ifdef ADD_ORIENTATION  // TOCHECK -add orientation to output
    sprintf(cp,"%s %s at %f,%f,%d, orien %f,%f,%f, in %s hdg=%d spd=%d pkts=%d/%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->ox, pp->oy, pp->oz,
        pp->aircraft, 
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5),
        pp->packetCount, pp->packetsDiscarded );
        // inet_ntoa(in), pp->SenderPort);
#else
    sprintf(cp,"%s %s at %f,%f,%d, %s pkts=%d/%d hdg=%d spd=%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->aircraft, 
        pp->packetCount, pp->packetsDiscarded,
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5) );
#endif
    if ((pp->pt == pt_Pos) && (pp->dist_m > 0.0)) {
        double secs = pp->sim_time - pp->prev_sim_time;
        if (secs > 0) {
            double calc_spd = pp->dist_m * 1.94384 / secs; // m/s to knots
            int ispd = (int)(calc_spd + 0.5);
            int enm = (int)(pp->total_nm + 0.5);
            //sprintf(EndBuf(cp),"Est=%d/%f/%f/%f ", ispd, pp->dist_m, secs, pp->total_nm);
            sprintf(EndBuf(cp),"Est=%d kt %d nm ", ispd, enm);
        }
    }
    if (pp->expired)
        strcat(cp,"EXPIRED");
    strcat(cp,MEOL);
    SPRTF(cp);
}



// Hmmm, in a testap it appears abs() can take 0.1 to 30% longer than test and subtract in _MSC_VER, Sooooooo
#ifdef _MSC_VER
#define SPD_CHANGE(pp1,pp2) (int)(((pp1->speed > pp2->speed) ? pp1->speed - pp2->speed : pp2->speed - pp1->speed ) + 0.5)
#define HDG_CHANGE(pp1,pp2) (int)(((pp1->heading > pp2->heading) ? pp1->heading - pp2->heading : pp2->heading - pp1->heading) + 0.5)
#define ALT_CHANGE(pp1,pp2) (int)(((pp1->alt > pp2->alt) ? pp1->alt - pp2->alt : pp2->alt - pp1->alt) + 0.5)
#else // seem in unix abs() is more than 50% FASTER
#define SPD_CHANGE(pp1,pp2) (int)(abs(pp1->speed - pp2->speed) + 0.5)
#define HDG_CHANGE(pp1,pp2) (int)(abs(pp1->heading - pp2->heading) + 0.5)
#define ALT_CHANGE(pp1,pp2) (int)(abs(pp1->alt - pp2->alt) + 0.5)
#endif // _MSC_VER

#ifdef USE_SIMGEAR  // TOCHECK SETPREVPOS MACRO
#define SETPREVPOS(p1,p2) { p1->ppx = p2->px; p1->ppy = p2->py; p1->ppz = p2->pz; }
#else // !USE_SIMGEAR
#define SETPREVPOS(p1,p2) { p1->PrevPos.Set( p2->SenderPosition.GetX(), p2->SenderPosition.GetY(), p2->SenderPosition.GetZ() ); }
#endif // USE_SIMGEAR y/n

#define SAME_FLIGHT(pp1,pp2)  ((strcmp(pp2->callsign, pp1->callsign) == 0)&&(strcmp(pp2->aircraft, pp1->aircraft) == 0))

Packet_Type Deal_With_Packet( char *packet, int len )
{
    static CF_Pilot _s_new_pilot;
    static char _s_tdchk[256];
    uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    T_PositionMsg*  PosMsg;
    PT_MsgHdr       MsgHdr;
    PCF_Pilot       pp, pp2;
    size_t          max, ii;
    char           *upd_by;
    double          sseconds;
    char           *tb = _s_tdchk;
    bool            revived;
    time_t          curr_time = time(0);
    double          lat, lon, alt;
    double          px, py, pz;
    char           *pcs;
    int             i;
    char           *pm;

    pp = &_s_new_pilot;
    memset(pp,0,sizeof(CF_Pilot)); // ensure new is ALL zero
    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);

    pcs = pp->callsign;
    for (i = 0; i < MAX_CALLSIGN_LEN; i++) {
        pcs[i] = MsgHdr->Callsign[i];
        if (MsgHdr->Callsign[i] == 0)
            break;
    }

        //if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
    if (!((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
        SPRTF("%s: Invalid packet...\n", module );
        failed_cnt++;
        //if (len < (int)MsgLen) {
        //    return pkt_InvLen1;
        if ( !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC)) ) {
            return pkt_InvMag;
        } else if ( !(MsgProto == PROTO_VER) ) {
            return pkt_InvProto;
        }
        return pkt_Invalid;
    }

    pp->curr_time = curr_time; // set CURRENT time
    pp->last_seen = curr_time;  // and LAST SEEN time
    if (MsgId == POS_DATA_ID)
    {
        if (MsgLen < sizeof(T_MsgHdr) + sizeof(T_PositionMsg)) {
            SPRTF("%s: Invalid position packet...\n", module );
            failed_cnt++;
            return pkt_InvPos;
        }
        PosMsg = (T_PositionMsg *) (packet + sizeof(T_MsgHdr));
        pp->prev_time = pp->curr_time;
        pp->sim_time = XDR_decode64<double> (PosMsg->time); // get SIM time
        pm = get_Model(PosMsg->Model);
        strcpy(pp->aircraft,pm);

        // SPRTF("%s: POS Packet %d of len %d, buf %d, cs %s, mod %s, time %lf\n", module, packet_cnt, MsgLen, len, pcs, pm, pp->sim_time);
        // get Sender address and port - need patch in fgms to pass this
        pp->SenderAddress = XDR_decode<uint32_t> (MsgHdr->ReplyAddress);
        pp->SenderPort    = XDR_decode<uint32_t> (MsgHdr->ReplyPort);
        px = XDR_decode64<double> (PosMsg->position[X]);
        py = XDR_decode64<double> (PosMsg->position[Y]);
        pz = XDR_decode64<double> (PosMsg->position[Z]);
        pp->ox = XDR_decode<float> (PosMsg->orientation[X]);
        pp->oy = XDR_decode<float> (PosMsg->orientation[Y]);
        pp->oz = XDR_decode<float> (PosMsg->orientation[Z]);
        if ( (px == 0.0) || (py == 0.0) || (pz == 0.0)) {   
            failed_cnt++;
            return pkt_InvPos;
        }
#ifdef USE_SIMGEAR // TOCHECK - Use SG functions

        SGVec3d position(px,py,pz);
        SGGeod GeodPoint;
        SGGeodesy::SGCartToGeod ( position, GeodPoint );
        lat = GeodPoint.getLatitudeDeg();
        lon = GeodPoint.getLongitudeDeg();
        alt = GeodPoint.getElevationFt();
        pp->px = px;
        pp->py = py;
        pp->pz = pz;
        SGVec3f angleAxis(pp->ox,pp->oy,pp->oz);
        SGQuatf ecOrient = SGQuatf::fromAngleAxis(angleAxis);
        SGQuatf qEc2Hl = SGQuatf::fromLonLatRad((float)GeodPoint.getLongitudeRad(),
                                          (float)GeodPoint.getLatitudeRad());
        // The orientation wrt the horizontal local frame
        SGQuatf hlOr = conj(qEc2Hl)*ecOrient;
        float hDeg, pDeg, rDeg;
        hlOr.getEulerDeg(hDeg, pDeg, rDeg);
        pp->heading = hDeg;
        pp->pitch   = pDeg;
        pp->roll    = rDeg;
#else // #ifdef USE_SIMGEAR
        pp->SenderPosition.Set (px, py, pz);
        sgCartToGeod ( pp->SenderPosition, pp->GeodPoint );
        lat = pp->GeodPoint.GetX();
        lon = pp->GeodPoint.GetY();
        alt = pp->GeodPoint.GetZ();
#endif // #ifdef USE_SIMGEAR y/n
        pp->lat = lat;;
        pp->lon = lon;
        pp->alt = alt;
        if (alt <= -9990.0) {
            failed_cnt++;
            return pkt_InvHgt;
        }
#ifdef USE_SIMGEAR  // TOCHECK SG function to get speed
        SGVec3f linearVel;
        for (unsigned i = 0; i < 3; ++i)
            linearVel(i) = XDR_decode<float> (PosMsg->linearVel[i]);
        pp->speed = norm(linearVel) * SG_METER_TO_NM * 3600.0;
#else // !#ifdef USE_SIMGEAR
        pp->SenderOrientation.Set ( pp->ox, pp->oy, pp->oz );

        euler_get( lat, lon, pp->ox, pp->oy, pp->oz,
            &pp->heading, &pp->pitch, &pp->roll );

        pp->linearVel.Set (
          XDR_decode<float> (PosMsg->linearVel[X]),
          XDR_decode<float> (PosMsg->linearVel[Y]),
          XDR_decode<float> (PosMsg->linearVel[Z])
            );
        pp->angularVel.Set (
          XDR_decode<float> (PosMsg->angularVel[X]),
          XDR_decode<float> (PosMsg->angularVel[Y]),
          XDR_decode<float> (PosMsg->angularVel[Z])
            );
        pp->linearAccel.Set (
          XDR_decode<float> (PosMsg->linearAccel[X]),
          XDR_decode<float> (PosMsg->linearAccel[Y]),
          XDR_decode<float> (PosMsg->linearAccel[Z])
            );
        pp->angularAccel.Set (
          XDR_decode<float> (PosMsg->angularAccel[X]),
          XDR_decode<float> (PosMsg->angularAccel[Y]),
          XDR_decode<float> (PosMsg->angularAccel[Z])
            );
        pp->speed = cf_norm(pp->linearVel) * SG_METER_TO_NM * 3600.0;
#endif // #ifdef USE_SIMGEAR

        pos_cnt++;
        pp->expired = false;
        max = vPilots.size();
        upd_by = 0;
        for (ii = 0; ii < max; ii++) {
            pp2 = &vPilots[ii]; // search list for this pilots
            if (SAME_FLIGHT(pp,pp2)) {
                pp2->last_seen = curr_time; // ALWAYS update 'last_seen'
                //seconds = curr_time - pp2->curr_time; // seconds since last PACKET
                sseconds = pp->sim_time - pp2->first_sim_time;
                if (sseconds > elapsed_sim_time) {
                    double add = sseconds - elapsed_sim_time;
                    if (VERB9) {
                        SPRTF("%s: Update elapsed from %lf by %lf to %lf, from cs %s, model %s\n", module,
                            elapsed_sim_time, add, sseconds, 
                            pp->callsign, pp->aircraft );
                    }
                    elapsed_sim_time = sseconds;
                    got_sim_time = true;    // we have a rough sim time
                }
                sseconds = pp->sim_time - pp2->sim_time; // curr packet sim time minus last packet sim time
                int spdchg = SPD_CHANGE(pp,pp2); // change_in_speed( pp, pp2 );
                int hdgchg = HDG_CHANGE(pp,pp2); // change_in_heading( pp, pp2 );
                int altchg = ALT_CHANGE(pp,pp2); // change_in_altitude( pp, pp2 );
                revived = false;
                pp->pt = pt_Pos;
                if (pp2->expired) {
                    pp2->expired = false;
                    pp->pt = pt_Revived;
                    sprintf(tb,"REVIVED=%d", (int)sseconds);
                    upd_by = tb;    // (char *)"TIME";
                    revived = true;
                    pp->dist_m = 0.0;
                } else {
#ifdef USE_SIMGEAR  // TOCHECK - SG to get diatance
                    SGVec3d p1(pp->px,pp->py,pp->pz);       // current position
                    SGVec3d p2(pp2->px,pp2->py,pp2->pz);    // previous position
                    pp->dist_m = length(p2 - p1); // * SG_METER_TO_NM;
#else // !#ifdef USE_SIMGEAR
                    pp->dist_m = (Distance ( pp2->SenderPosition, pp->SenderPosition ) * SG_NM_TO_METER); /** Nautical Miles to Meters */
#endif // #ifdef USE_SIMGEAR y/n
                    if ((time_t)sseconds >= m_PlayerExpires) {
                        sprintf(tb,"TIME=%d", (int)sseconds);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (pp->dist_m > m_MinDistance_m) {
                        sprintf(tb,"DIST=%d/%d", (int)(pp->dist_m+0.5), (int)sseconds);
                        upd_by = tb; // (char *)"DIST";
                    } else if (spdchg > m_MinSpdChange_kt) {
                        sprintf(tb,"SPDC=%d", spdchg);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (hdgchg > m_MinHdgChange_deg) {
                        sprintf(tb,"HDGC=%d", hdgchg);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (altchg > m_MinAltChange_ft) {
                        sprintf(tb,"ALTC=%d", altchg);
                        upd_by = tb;    // (char *)"TIME";
                    }
                }
                if (upd_by) {
                    if (revived) {
                        pp->flight_id      = get_epoch_id(); // establish NEW UNIQUE ID for flight
                        pp->first_sim_time = pp->sim_time;   // restart first sim time
                        pp->cumm_nm       += pp2->total_nm;  // get cummulative nm
                        pp2->total_nm      = 0.0;            // restart nm
                    } else {
                        pp->flight_id      = pp2->flight_id; // use existing FID
                        pp->first_sim_time = pp2->first_sim_time; // keep first sim time
                        pp->first_time     = pp2->first_time; // keep first epoch time
                    }
                    pp->expired          = false;
                    pp->packetCount      = pp2->packetCount + 1;
                    pp->packetsDiscarded = pp2->packetsDiscarded;
                    pp->prev_sim_time    = pp2->sim_time;
                    pp->prev_time        = pp2->curr_time;
                    // accumulate total distance travelled (in nm)
                    pp->total_nm         = pp2->total_nm + (pp->dist_m * SG_METER_TO_NM);
                    SETPREVPOS(pp,pp2);  // copy POS to PrevPos to get distance travelled
                    pp->curr_time        = curr_time; // set CURRENT packet time
                    *pp2 = *pp;     // UPDATE the RECORD with latest info
                    print_pilot(pp2,upd_by,pt_Pos);
                    //if (revived)
                    //    Pilot_Tracker_Connect(pp2);
                    //else
                    //    Pilot_Tracker_Position(pp2);

                } else {
                    sprintf(tb,"DISC T=%d,D=%d/%d,S=%d,H=%d,A=%d", (int)sseconds,
                        (int)(pp->dist_m+0.5), (int)sseconds,
                        spdchg, hdgchg, altchg);
                    print_pilot(pp2,tb,pt_Pos);
                    discard_cnt++;
                    pp2->packetsDiscarded++;
                    return pkt_Discards;
                }

                return pkt_Pos;

            }
        }
        pp->packetCount = 1;
        pp->packetsDiscarded = 0;
        pp->first_sim_time = pp->prev_sim_time = pp->sim_time;
        SETPREVPOS(pp,pp); // set as SAME as current
        pp->curr_time  = curr_time; // set CURRENT packet time
        pp->pt = pt_New;
        pp->flight_id = get_epoch_id(); // establish UNIQUE ID for flight
        pp->dist_m = 0.0;
        pp->total_nm = 0.0;
        vPilots.push_back(*pp);
        print_pilot(pp,"FRST",pt_Pos);
        return pkt_First;

    } else if (MsgId == CHAT_MSG_ID) {
        SPRTF("%s: CHAT Packet %d of len %d, buf %d, cs %s\n", module, packet_cnt, MsgLen, len, pcs);
        chat_cnt++;
        return pkt_Chat;
    }
    SPRTF("%s: Got ID %d! Not postion or chat packet...\n", module, MsgId );
    failed_cnt++;
    return pkt_Invalid;
}


PKTSTR sPktStr[pkt_Max] = {
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

void packet_stats()
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
            SPRTF("%s %d ", pps[i].desc, cnt );
        }
    }
    SPRTF("Total %d, Bad %d\n", PacketCount, Bad_Packets );

}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
// void Expire_Pilots()
// called periodically, well called after an elapse of 
// the current m_PlayerExpires seconds have elapsed.
//
// TODO: Due to a fgfs BUG, mp packets commence even before
// the scenery is loaded, and during the heavy load 
// of airport/navaid, and scenery tiles, no mp packets
// are sent. At a 10 second TTL this expires a flight 
// prematurely, only to be revived 10-30 seconds later.
// ===========================================================
typedef std::vector<size_t> vSZT;
void Expire_Pilots()
{
    vCFP *pvlist = &vPilots;
    size_t max, ii, xcnt, nxcnt;
    PCF_Pilot pp;
    time_t curr = time(0);  // get current epoch seconds
    time_t diff;
    int idiff, iExp;
    iExp = (int)m_PlayerExpires;
    char *tb = GetNxtBuf();
    max = pvlist->size();
    xcnt = 0;
    nxcnt = 0;
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        if ( pp->expired ) {
            xcnt++;
        } else {
            // diff = curr - pp->curr_time;
            diff = curr - pp->last_seen; // 20121222 - Use LAST SEEN for expiry
            idiff = (int)diff;
            //if ((pp->curr_time + m_PlayerExpires) > curr) {
            if (idiff > iExp) {
                pp->expired = true;
                pp->exp_time = curr;    // time expired - epoch secs
                sprintf(tb,"EXPIRED %d",idiff); 
                //print_pilot(pp,"EXPIRED");
                print_pilot(pp, tb, pt_Expired);
                //Pilot_Tracker_Disconnect(pp);
                nxcnt++;
            }
        }
    }
    m_ExpiredCnt = (xcnt + nxcnt);

#ifdef ADD_VECTOR_ERASE
    if (m_MaxExpired && (m_ExpiredCnt > m_MaxExpired)) {
        // time to clean up vector memory
        vSZT vst;
        for (ii = 0; ii < max; ii++) {
            pp = &pvlist->at(ii);
            if ( pp->expired ) {
                vst.push_back(ii);
            }
        }
        while (!vst.empty()) {
            ii = vst.back();
            vst.pop_back();
            pvlist->erase(pvlist->begin() + ii);
        }
        ii = pvlist->size();
        SPRTF("%s: Removed %d expired pilots from vector. Was %d, now %d\n", mod_name,
            (int) m_ExpiredCnt, (int) max, (int) ii );
        m_ExpiredCnt = 0;
    }
#endif // #ifdef ADD_VECTOR_ERASE
}

//////////////////////////////////////////////////////////////////////////
const char *json_file = "tempflts.json";
bool json_file_disabled = false;
bool is_json_file_disabled() 
{
    if (!json_file)
        return true;
    return json_file_disabled; 
}
///////////////////////////////////////////////////////////////////////
// A simple buffer, reallocated to suit json string size
// to hold the full json string
// =====================================================
typedef struct tagJSONSTR {
    int size;
    int used;
    char *buf;
}JSONSTR, *PJSONSTR;

static PJSONSTR _s_pJsonStg = 0;
static PJSONSTR _s_pXmlStg = 0;

const char *header = "{\"success\":true,\"source\":\"cf-client\",\"last_updated\":\"%s\",\"flights\":[\n";
const char *tail   = "],\"count\":%u}\n";
const char*json_stg_org = "{\"fid\":\"%s\",\"callsign\":\"%s\",\"lat\":\"%f\",\"lon\":\"%f\",\"alt_ft\":\"%d\",\"model\":\"%s\",\"spd_kts\":\"%d\",\"hdg\":\"%d\",\"dist_nm\":\"%d\"}";
const char*json_stg = "{\"fid\":%s,\"callsign\":\"%s\",\"lat\":%f,\"lon\":%f,\"alt_ft\":%d,\"model\":\"%s\",\"spd_kts\":%d,\"hdg\":%d,\"dist_nm\":%d}";

void Realloc_JSON_Buf(PJSONSTR pjs, int len)
{
    while ((pjs->used + len) >= pjs->size) {
        pjs->size <<= 2;
        pjs->buf = (char *)realloc(pjs->buf,pjs->size);
        if (!pjs->buf) {
            SPRTF("%s: ERROR: Failed in memory rellocation! Size %d. Aborting\n", mod_name, pjs->size);
            exit(1);
        }
    }
}

int Append_2_Buf( PJSONSTR pjs, char *buf )
{
    int len = (int)strlen(buf);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcat(pjs->buf,buf);
    pjs->used += len;
    return len;
}

int Add_JSON_Head(PJSONSTR pjs) 
{
    int iret = 0;
    char *tp = Get_Current_UTC_Time_Stg();
    char *cp = GetNxtBuf();
    int len = sprintf(cp,header,tp);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcpy(pjs->buf,cp);
    pjs->used = (int)strlen(pjs->buf);
    return iret;
}

time_t show_time = 0;
time_t show_delay = 300;
long write_count = 0;
#ifndef DEF_JSON_SIZE
#define DEF_JSON_SIZE 1024; // 16 for testing
#endif

////////////////////////////////////////////////////////////////////////////
// XML Feed - FIX20130404 - Add XML feed
int Get_XML( char **pbuf )
{
    PJSONSTR pjs = _s_pXmlStg;
    if (pjs) {
        *pbuf = pjs->buf;
        return pjs->used;
    }
    return 0;
}
/* ------------------------------------
<?xml version="1.0" encoding="UTF-8"?>
 <fg_server pilot_cnt="35">
  <marker spd_kt="123" heading="221" alt="137" lng="-88.270755" lat="30.504218" 
  model="Dragonfly" server_ip="" callsign="jmckay"/>
  ... plus 34 more ...
 </fg_server>
 Was
  <marker roll="0.271530002355576" pitch="0.205735370516777" heading="221.242599487305" 
  alt="137.212286" lng="-88.270755" lat="30.504218" model="Dragonfly" server_ip="mpserver01" 
  callsign="jmckay"/>
 ------------------------------------ */

const char *x_head = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
const char *x_open = "<fg_server pilot_cnt=\"%d\">\n";
const char *x_mark = "<marker spd_kt=\"%d\" heading=\"%d\" alt=\"%d\" lng=\"%.6f\" lat=\"%.6f\" model=\"%s\" server_ip=\"%s\" callsign=\"%s\"/>\n";
const char *x_tail = "</fg_server>\n";

int Write_XML() // FIX20130404 - Add XML feed
{
    struct in_addr in;
    static char _s_xbuf[1028];
    PJSONSTR pxs = _s_pXmlStg;
    char *paddr;
    if (!pxs) {
        pxs = new JSONSTR;
        pxs->size = DEF_JSON_SIZE;
        pxs->buf = (char *)malloc(pxs->size);
        if (!pxs->buf) {
            SPRTF("%s: ERROR: Failed in memory allocation! Size %d. Aborting\n", mod_name, pxs->size);
            exit(1);
        }
        pxs->used = 0;
        _s_pXmlStg = pxs;
    }
    vCFP *pvlist = &vPilots;
    size_t max, ii;
    PCF_Pilot pp;
    char *pb = _s_xbuf;
    int count;
    max = pvlist->size();
    // clear pevious
    pxs->buf[0] = 0;
    pxs->used   = 0;
    if (!max)
        return 0;
    count = 0;
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        if ( pp->expired )
            continue;
        count++;
    }
    Append_2_Buf( pxs, (char *)x_head );
    sprintf(pb,x_open,count);
    Append_2_Buf( pxs, pb );
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        if ( pp->expired )
            continue;
        in.s_addr = pp->SenderAddress;
        paddr = inet_ntoa(in);
        if (!paddr) paddr = (char *)"";
        // "<marker spd_kt=\"%d\"
        // heading=\"%d\"
        // alt=\"%d\"
        // lng=\"%.6f\"
        // lat=\"%.6f\"
        // model=\"%s\"
        // server_ip=\"%s\"
        // callsign=\"%s\"/>\n";
        sprintf(pb,x_mark,
            (int)(pp->speed + 0.5),
            (int)(pp->heading + 0.5),
            (int)(pp->alt + 0.5),
            pp->lon,
            pp->lat,
            get_Model(pp->aircraft),
            paddr,
            pp->callsign );
        Append_2_Buf( pxs, pb );
    }
    Append_2_Buf( pxs, (char *)x_tail );
    return 0;
}


////////////////////////////////////////////////////////////////////////////

int Get_JSON( char **pbuf )
{
    PJSONSTR pjs = _s_pJsonStg;
    if (pjs) {
        *pbuf = pjs->buf;
        return pjs->used;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// int Write_JSON()
// Format the JSON string into a buffer ready to be collected
// 20121125 - Added the unique flight id to the output
// 20121127 - Added total distance (nm) to output
// =======================================================================
int Write_JSON()
{
    static char _s_jbuf[1028];
    static char _s_epid[264];
    vCFP *pvlist = &vPilots;
    size_t max, ii;
    PCF_Pilot pp;
    int len, wtn, count, total_cnt;
    // struct in_addr in;
    PJSONSTR pjs = _s_pJsonStg;
    if (!pjs) {
        pjs = new JSONSTR;
        pjs->size = DEF_JSON_SIZE;
        pjs->buf = (char *)malloc(pjs->size);
        if (!pjs->buf) {
            SPRTF("%s: ERROR: Failed in memory allocation! Size %d. Aborting\n", mod_name, pjs->size);
            exit(1);
        }
        pjs->used = 0;
        _s_pJsonStg = pjs;
    }
    Add_JSON_Head(pjs);
    max = pvlist->size();
    char *tb = _s_jbuf; // buffer for each json LINE;
    char *epid = _s_epid;
    count = 0;
    total_cnt = 0;
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        total_cnt++;
        if ( !pp->expired ) {
            // in.s_addr = pp->SenderAddress;
            set_epoch_id_stg( epid, pp->flight_id );
//#ifdef USE_SIMGEAR  // json string generation
            sprintf(tb,json_stg,
                epid,
                pp->callsign, 
                pp->lat, pp->lon, 
                (int) (pp->alt + 0.5),
                pp->aircraft,
                (int)(pp->speed + 0.5),
                (int)(pp->heading + 0.5),
                (int)(pp->total_nm + 0.5) );
//#else // #ifdef USE_SIMGEAR
//            sprintf(tb,json_stg2,
//                pp->callsign, 
//                pp->GeodPoint.GetX(), pp->GeodPoint.GetY(), 
//                (int) (pp->GeodPoint.GetZ() + 0.5),
//                pp->aircraft,
//                (int)(pp->speed + 0.5), (int)(pp->heading + 0.5));
//#endif // #ifdef USE_SIMGEAR y/n
            strcat(tb,",\n");
            len = (int)strlen(tb);
            if ((pjs->used + len) >= pjs->size) {
                Realloc_JSON_Buf(pjs,len);
            }
            strcat(pjs->buf,tb);
            pjs->used = (int)strlen(pjs->buf);
            count++;
        }
    }
    if (max) {
        len = (int)strlen(pjs->buf);
        pjs->buf[len-2] = ' ';  // convert last comma to space
    }
    len = (int)sprintf(tb,tail,count);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcat(pjs->buf,tb);
    pjs->used = (int)strlen(pjs->buf);

    const char *pjson = json_file;
    if (!is_json_file_disabled() && pjson) {
        FILE *fp = fopen(pjson,"w");
        if (!fp) {
            SPRTF("%s: ERROR: Failed to create JSON file [%s]\n", mod_name, pjson);
            json_file_disabled = true;
            json_file = 0;  // only show FAILED once
            return 1;
        }
        wtn = fwrite(pjs->buf,1,pjs->used,fp);
        fclose(fp);
        if (wtn != pjs->used) {
            SPRTF("%s: ERROR: Failed write %d to JSON file [%s]\n", mod_name, pjs->used, pjson);
            return 1;
        }
        write_count++;
        time_t curr = time(0);
        if (curr > show_time) {
            SPRTF("%s: Written %s, %d times, last with %d of %d pilots\n", mod_name, pjson, write_count, count, total_cnt);
            show_time = curr + show_delay;
        }
    }
    return 0;
}



// eof = cf-pilot.cxx

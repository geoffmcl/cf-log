/*\
 * cf-pilot.hxx
 *
 * Copyright (c) 2014 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/

#ifndef _CF_PILOT_HXX_
#define _CF_PILOT_HXX_

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


extern Packet_Type Deal_With_Packet( char *packet, int len );
extern void Expire_Pilots();
extern int Write_JSON();
extern int Write_XML(); // FIX20130404 - Add XML feed
extern void packet_stats();
extern void show_packets();

typedef struct tagPKTSTR {
    Packet_Type pt;
    const char *desc;
    int count;
    int totals;
    void *vp;
}PKTSTR, *PPKTSTR;

extern PKTSTR sPktStr[];
extern PPKTSTR Get_Pkt_Str();

extern time_t m_PlayerExpires;     // standard expiration period (seconds)
extern size_t packet_cnt;
extern double elapsed_sim_time;
extern bool got_sim_time;

// get the data
extern int Get_JSON( char **pbuf );
extern int Get_XML( char **pbuf );
extern void clean_up_pilots( bool clear = true );


#endif // #ifndef _CF_PILOT_HXX_
// eof - cf-pilot.hxx

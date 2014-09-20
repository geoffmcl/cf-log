/*\
 * cf-log.cxx
 *
 * Copyright (c) 2014 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <time.h>
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

#include "cf_misc.hxx"
#include "sprtf.hxx"
#include "mpKeyboard.hxx"
#include "cf-pilot.hxx"
#include "cf-server.hxx"
#include "cf-log.hxx"

// forward refs

bool use_sim_time = false;  // true;

#define DEF_MS 55
#define SLEEP(a) mySleep(a);

#ifndef SPRTF
#define SPRTF printf
#endif
#ifndef EndBuf
#define EndBuf(a)   ( a + strlen(a) )
#endif

static const char *module = "cf-log";
#define mod_name module

#ifdef _MSC_VER
const char *raw_log = "C:\\Users\\user\\Downloads\\logs\\fgx-cf\\cf_raw.log";
#else
const  char *raw_log = (char *)"/home/geoff/downloads/cf_raw.log";
#endif

static struct stat sbuf;
int verbosity = 0;

// static char callsign[MAX_CALLSIGN_LEN+2];


int check_keyboard()
{
    int res = test_for_input();
    if (res) {
        if (res == 0x1b) {
            SPRTF("%s: Got escape key... exiting...\n", module );
            return 1;
        } else {
            SPRTF("%s: Got unused key %X!\n", module, res );
        }
    }
    return 0;
}

// get first block of raw udp data
#define MAX_RAW_LOG 2048
static size_t raw_log_size, raw_log_remaining;
static FILE *raw_log_fp = 0;
static char *raw_log_buffer = 0;
static size_t raw_block_size, raw_data_size;
double raw_bgn_secs, app_bgn_secs;
void clean_up_log() 
{
    if (raw_log_fp)
        fclose(raw_log_fp);
    raw_log_fp = 0;
    if (raw_log_buffer)
        free(raw_log_buffer);
    raw_log_buffer = 0;
}

int get_next_block()
{
    int key = 0;
    Packet_Type pt;
    size_t i, j, size = raw_data_size;  // MAX_RAW_LOG;
    char *cp = raw_log_buffer;
    if (cp && raw_block_size) {
        pt = Deal_With_Packet( cp, raw_block_size );
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
            size_t rd = fread( &cp[raw_data_size], 1, size, raw_log_fp );
            raw_block_size = 0;
            if (rd == size) {
                raw_data_size += size;  // bump by this read
                i = 0;
                // this has to be true - just a debug check
                if ( !((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
                      (cp[i+2] == 'G') && (cp[i+3] == 'F')) ) {
                    goto Bad_Read;
                }
                for (i = 4; i < raw_data_size; i++) {
                    if ((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
                        (cp[i+2] == 'G') && (cp[i+3] == 'F')) {
                        break;
                    }
                }
                raw_block_size = i; // either found next, or out of data, 
                key = 1;    // but have next block
            } else {
Bad_Read:
                fclose(raw_log_fp);
                raw_log_fp = 0;
                free(raw_log_buffer);
                raw_log_buffer = 0;
                SPRTF("%s: Failed to get the next raw block!\n", module );
            }
        }
    }
    return key;
}

int open_raw_log()
{
    int key = 0;
    const char *tf = raw_log;
    if (stat(tf,&sbuf)) {
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
    char *cp = (char *) malloc( size );
    if (!cp) {
        SPRTF("%s: memory allocation FAILED\n", module);
        return 1;
    }
    FILE *fp = fopen( tf, "rb" );
    if (!fp) {
        SPRTF("%s: Failed to open '%s'!\n", module, tf);
        free(cp);
        return 1;
    }
    size_t rd = fread(cp,1,size,fp);
    if (rd != size) {
        SPRTF("%s: Failed read of '%s'! Req %u, got %u?\n", module, tf, (int)size, (int)rd);
        fclose(fp);
        free(cp);
        return 1;
    }

    size_t i, j, bgn;
    for (i = 0; i < size; i++) {
        if ((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
            (cp[i+2] == 'G') && (cp[i+3] == 'F')) {
            if (i) {
                // UGH, not at head - move ALL data up to head
                bgn = i;
                j = 0;
                for ( ; i < size; i++) {
                    cp[j++] = cp[i];
                }
                rd = fread(&cp[j],1,bgn,fp);    // top up the buffer from the file
                if (rd != bgn) {
                    SPRTF("%s: Failed read of '%s'! Req %u, got %u?\n", module, tf, (int)bgn, (int)rd);
                    fclose(fp);
                    free(cp);
                    return 1;
                }
            }
            // search for next
            for (i = 4; i < size; i++) {
                if ((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
                    (cp[i+2] == 'G') && (cp[i+3] == 'F')) {
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
    SPRTF("%s: Failed find fisrt udp packet in '%s'!\n", module, tf);
    return 1;
}

int open_raw_log_whole()
{
    int key = 0;
    const char *tf = raw_log;
    time_t diff, curr, last_expire, pilot_ttl, last_json;
    size_t i, bgn;
    bgn = 0;
    Packet_Type pt;
    double bgn_secs = get_seconds();
    pilot_ttl = m_PlayerExpires;
    curr = last_expire = time(0);
    last_json = 0;
    size_t rd = 0;
    
    if (stat(tf,&sbuf)) {
        SPRTF("%s: Failed to stat '%s'!\n", module, tf);
        return 1;
    }
    size_t size = sbuf.st_size;
    if (!size) {
        SPRTF("%s: Files '%s' has no size!\n", module, tf);
        return 1;

    }
    char *cp = (char *) malloc( size + 4 );
    if (!cp) {
        SPRTF("%s: memory allocation FAILED\n", module);
        return 1;
    }
    FILE *fp = fopen( tf, "rb" );
    if (!fp) {
        SPRTF("%s: Failed to stat '%s'!\n", module, tf);
        key = 1;
        goto exit;
    }
    rd = fread(cp,1,size,fp);
    fclose(fp);
    fp = 0;
    if (rd != size) {
        SPRTF("%s: Failed read of '%s'!\n", module, tf);
        key = 1;
        goto exit;
    }

    for (i = 0; i < size; i++) {
        if ((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
            (cp[i+2] == 'G') && (cp[i+3] == 'F')) {
            if (packet_cnt) {
                if (use_sim_time && got_sim_time) {
                    double secs = get_seconds() - bgn_secs;
                    while ((secs < elapsed_sim_time)&&(key == 0)) {
                        SLEEP(DEF_MS);
                        key = check_keyboard();
                        secs = get_seconds() - bgn_secs;
                    }
                }
                if (key) break;
                SPRTF("%s: Packet %d is length %u\n", module, (int)packet_cnt, (int)(i - bgn));
                pt = Deal_With_Packet( &cp[bgn], i - bgn );
                if (pt < pkt_Max) sPktStr[pt].count++;  // set the packet stats
            }
            bgn = i;
            packet_cnt++;
            curr = time(0);
            key = check_keyboard();
            if (key)
                break;
            // time to check if any active pilot expired
            diff = curr - last_expire;
            if (diff > pilot_ttl) {
                Expire_Pilots();
                last_expire = curr;   // set new time
            }
            if (last_json != curr) {
                Write_JSON();
                Write_XML(); // FIX20130404 - Add XML feed
                last_json = curr;
            }
        }
    }
    if (key == 0) {
        if (packet_cnt) {
           pt = Deal_With_Packet( &cp[bgn], i - bgn );
           if (pt < pkt_Max) sPktStr[pt].count++;  // set the packet stats
        }
        packet_stats();
    }

exit:

    free(cp);
    if (fp)
        fclose(fp);

    return key;
}


int split_raw_log_whole( char *new_log, size_t count, size_t begin )
{
    int key = 0;
    const char *tf = raw_log;
    size_t rd = 0;
    FILE *out = fopen( new_log, "wb" );
    if (!out) {
        SPRTF("%s: Failed to create new log file '%s'\n", module, new_log );
        return 1;
    }
    if (stat(tf,&sbuf)) {
        SPRTF("%s: Failed to stat '%s'!\n", module, tf);
        return 1;
    }
    size_t size = sbuf.st_size;
    if (!size) {
        SPRTF("%s: Files '%s' has no size!\n", module, tf);
        return 1;

    }
    char *cp = (char *) malloc( size + 4 );
    if (!cp) {
        SPRTF("%s: memory allocation FAILED\n", module);
        return 1;
    }
    FILE *fp = fopen( tf, "rb" );
    if (!fp) {
        SPRTF("%s: Failed to stat '%s'!\n", module, tf);
        key = 1;
        goto exit;
    }
    rd = fread(cp,1,size,fp);
    fclose(fp);
    fp = 0;
    if (rd != size) {
        SPRTF("%s: Failed read of '%s'!\n", module, tf);
        key = 1;
        goto exit;
    }
    size_t i, bgn, cnt, len, wtn;
    bgn = 0;
    cnt = 0;
    for (i = 0; i < size; i++) {
        if ((cp[i+0] == 'S') && (cp[i+1] == 'F') &&
            (cp[i+2] == 'G') && (cp[i+3] == 'F')) {
            len = (i - bgn);
            if (len && packet_cnt && (packet_cnt >= begin)) {
                SPRTF("%s: Packet %d is length %u\n", module, (int)packet_cnt, (int)len);
                wtn = fwrite( &cp[bgn], 1, len, out );
                if (wtn != len) {
                    SPRTF("%s: Failed to write packet!\n", module );
                    key = 1;
                    break;
                }
                cnt++;
                if (count && (cnt >= count)) {
                    break;
                }

            }
            bgn = i;
            packet_cnt++;
            key = check_keyboard();
            if (key)
                break;
        }
    }
    if (key == 0) {
        len = (i - bgn);
        if ( len && (packet_cnt >= begin) ) {
            if ( !(count && (cnt >= count)) ) {
                SPRTF("%s: Packet %d is length %u\n", module, (int)packet_cnt, (int)len);
                wtn = fwrite( &cp[bgn], 1, len, out );
                if (wtn != len) {
                    SPRTF("%s: Failed to write packet!\n", module );
                    key = 1;
                }
                cnt++;
            }
        }
        SPRTF("%s: Written %d packets to '%s'\n", module, (int)cnt, new_log );
    }

exit:

    free(cp);
    if (fp)
        fclose(fp);
    if (out)
        fclose(out);

    exit(key);
    return key;
}



void test_sleeps()
{
    double bgn_secs, secs;
    int ms = 200;
    int i, max = 5;
    int total_ms = 0;
    double bgn = get_seconds();
    for (i = 0; i < max; i++) {
        bgn_secs = get_seconds();
        mySleep(ms);
        secs = get_seconds() - bgn_secs;
        SPRTF("mySleep(%d) ms, took %.15lf ms\n", ms, secs * 1000);
        total_ms += ms;
        ms += 100;
    }
    secs = get_seconds() - bgn;
    SPRTF("Sleeps(%d) ms, took %.15lf ms\n", total_ms, secs * 1000);

#if 0 // 00000000000000 THIS FAILS 000000000000000
    ms = 200;
    for (i = 0; i < max; i++) {
        bgn_secs = get_seconds();
        mySelectSleep(ms);
        secs = get_seconds() - bgn_secs;
        SPRTF("mySelectSleep(%d) ms, took %.15lf ms\n", ms, secs * 1000);
        total_ms += ms;
        ms += 100;
    }
    secs = get_seconds() - bgn;
    SPRTF("Sleeps(%d) ms, took %.15lf ms\n", total_ms, secs * 1000);
#endif // 000000000000000000000000000000000000000000

    exit(1);
}


// #define USE_WHOLE_READ  // back to whole read for testing
int main( int argc, char **argv )
{
    // split_raw_log_whole( "shortlog.bin", 5000, 0 );
    app_bgn_secs = get_seconds();
    int iret = server_main( argc, argv );
    clean_up_log();
    clean_up_pilots();
    SPRTF("%s: Ran for %s, exit(%d)\n", module, get_seconds_stg( get_seconds() - app_bgn_secs ), iret);
    return iret;
}
// main() OS entry
int main_test( int argc, char **argv )
{
    int iret = 0;
    set_log_file("tempcflog.txt",false);
    //test_sleeps();
    verbosity = 2;
#ifdef USE_WHOLE_READ
    iret = open_raw_log_whole();
    if (iret == 0) {
        show_packets();
    }
#else
    iret = open_raw_log();
    if (iret == 0) {
        size_t cnt = 0;
        while (get_next_block()) {
            cnt++;
        }
        show_packets();
    }
#endif
    return iret;
}


// eof = cf-log.cxx

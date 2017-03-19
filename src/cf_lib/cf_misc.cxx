/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0  2012/10/17 00:00:00  geoff
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of the
 *   License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, US
 *
 */

// Module: cf_misc.cxx
// Some miscellaneous functions
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#pragma warning ( disable : 4996 ) // disable this stupid warning
#include <WinSock2.h>
#include <sys/timeb.h> // _timeb & _ftime()
#pragma comment (lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_ntoa()
#define __STDC_FORMAT_MACROS
#include <inttypes.h> // for PRIu64
#include <sys/time.h>
#include <ctype.h> // for toupper()
#endif
#include <string.h> // strdup(), strlen()
#include <stdio.h>  // sprintf(), ...
#include <stdint.h> // for unit64_t
#include <time.h>
#include <stdarg.h> // va_start in unix
#include <stdlib.h> // malloc() in unix
#include "sprtf.hxx"    // GetNxtBuf()
#include "cf_misc.hxx"
#include "typcnvt.hxx"

static const char *mod_name = "cf_misc";

#ifdef _MSC_VER

// get a message from the system for this error value
char *get_errmsg_text( int err )
{
    LPSTR ptr = 0;
    DWORD fm = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&ptr, 0, NULL );
    if (ptr) {
        size_t len = strlen(ptr);
        while(len--) {
            if (ptr[len] > ' ') break;
            ptr[len] = 0;
        }
        if (len) return ptr;
        LocalFree(ptr);
    }
    return NULL;
}

int nanosleep( struct timespec *req, struct timespec *rem )
{
    DWORD ms = req->tv_nsec / 1000000; // nanoseconds to milliseconds
    if (req->tv_sec)
        ms += (DWORD)(req->tv_sec * 1000); // seconds to milliseconds
    if (ms == 0)
        ms = 1;
    Sleep(ms);
    return 0;
}

#endif // #ifdef _MSC_VER

char *get_address_stg( sockaddr_in *pAddr, int size )
{
    static char _s_addr_buf[1024];
    char *paddr = (char *)_s_addr_buf;
#ifdef _MSC_VER
    DWORD rblen = 1024;
    int res = WSAAddressToString( 
        (LPSOCKADDR)pAddr,  // _In_      LPSOCKADDR lpsaAddress,
        (DWORD)size,        // _In_      DWORD dwAddressLength,
        0,                  // _In_opt_  LPWSAPROTOCOL_INFO lpProtocolInfo,
        (LPTSTR)_s_addr_buf,    // _Inout_   LPTSTR lpszAddressString,
        &rblen );           // _Inout_   LPDWORD lpdwAddressStringLength
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        char *perr = get_errmsg_text(err);
        if (perr) {
            sprintf(paddr,"WSAAddressToString returned error %d - %s", err, perr);
            LocalFree(perr);
        } else
            sprintf(paddr,"WSAAddressToString returned error %d", err);
    } else {
        char *pa = paddr;
        if (pAddr->sin_addr.s_addr == htonl(INADDR_ANY))
            pa = "INADDR_ANY";
        else if (pAddr->sin_addr.s_addr == htonl(INADDR_NONE))
            pa = "INADDR_NONE";
        int port = (int)ntohs( pAddr->sin_port );
        char *pdup = strdup(pa);
        if (pdup) {
            sprintf(paddr,"NetAddr: IP %s, Port %d ", pdup, port);
            free(pdup);
        }
    }
#else // _MSC_VER
    // TODO: These functions are deprecated because they don't handle IPv6! Use inet_ntop() or inet_pton() instead!
    char *some_ptr = inet_ntoa(pAddr->sin_addr); // return the IP
    int port = (int)ntohs( pAddr->sin_port );
    sprintf(paddr,"NetAddr: IP %s, Port %d ", some_ptr, port);
#endif // _MSC_VER y/n
    return paddr;
}

// get short name - that is remove any path
char *get_base_name(char *name)
{
    char *bn = strdup(name);
    size_t len = strlen(bn);
    size_t i, off;
    int c;
    off = 0;
    for (i = 0; i < len; i++) {
        c = bn[i];
        if (( c == '/' )||( c == '\\' ))
            off = i + 1;
    }
    return &bn[off];
}

const char *ts_form = "%04d-%02d-%02d %02d:%02d:%02d";
// Creates the UTC time string
char *Get_UTC_Time_Stg(time_t Timestamp)
{
    char *ps = GetNxtBuf();
    tm  *ptm;
    ptm = gmtime (& Timestamp);
    sprintf (
        ps,
        ts_form,
        ptm->tm_year+1900,
        ptm->tm_mon+1,
        ptm->tm_mday,
        ptm->tm_hour,
        ptm->tm_min,
        ptm->tm_sec );
    return ps;
}

char *Get_Current_UTC_Time_Stg()
{
    time_t Timestamp = time(0);
    return Get_UTC_Time_Stg(Timestamp);
}

char *Get_Current_GMT_Time_Stg()
{
    time_t timestamp = time(0);
    struct tm * timeinfo;
    timeinfo = gmtime ( &timestamp );
    return asctime(timeinfo);
}

/////////////////////////////////////////////////////
// set_epoch_id_stg
// format a utin64_t into a buffer
int set_epoch_id_stg( char *cp, uint64_t id )
{
#ifdef _MSC_VER
    return sprintf(cp, "%I64u", id);
#else
    return sprintf(cp, "%" PRIu64, id);
#endif
}

//////////////////////////////////////////////////////
// Return uint64_t in buffer
char *get_epoch_id_stg(uint64_t id)
{
    char *cp = GetNxtBuf();
    set_epoch_id_stg( cp, id );
    return cp;
}

//////////////////////////////////////////////////////
// get epoch time in usecs
uint64_t get_epoch_usecs()
{
    struct timeval tv;
    gettimeofday( &tv, 0 );
    uint64_t id = ((tv.tv_sec * 1000000) + tv.tv_usec);
    return id;
}

////////////////////////////////////////////////////////
// uint64_t get_epoch_id()
//
// get UNIQUE ID for flight
// allows for up to 999 pilots joining in the same second
// ======================================================
uint64_t get_epoch_id()
{
    static time_t prev = 0;
    static int eq_count = 0;
    time_t curr = time(0);
    if (curr == prev)
        eq_count++;
    else {
        eq_count = 0;
        prev = curr;
    }
    uint64_t id = curr * 1000;
    id += eq_count;
    return id;
}

DiskType is_file_or_directory ( char * path )
{
	struct stat buf;
    if (!path)
        return DT_NONE;
	if (stat(path,&buf) == 0)
	{
		if (buf.st_mode & M_IS_DIR)
			return DT_DIR;
		else
			return DT_FILE;
	}
	return DT_NONE;
}

// #define USE_PERF_COUNTER
#if (defined(WIN32) && defined(USE_PERF_COUNTER))
// QueryPerformanceFrequency( &frequency ) ;
// QueryPerformanceCounter(&timer->start) ;
double get_seconds()
{
    static double dfreq;
    static bool done_freq = false;
    static bool got_perf_cnt = false;
    if (!done_freq) {
        LARGE_INTEGER frequency;
        if (QueryPerformanceFrequency( &frequency )) {
            got_perf_cnt = true;
            dfreq = (double)frequency.QuadPart;
        }
        done_freq = true;
    }
    double d;
    if (got_perf_cnt) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter (&counter);
        d = (double)counter.QuadPart / dfreq;
    }  else {
        DWORD dwd = GetTickCount(); // milliseconds that have elapsed since the system was started
        d = (double)dwd / 1000.0;
    }
    return d;
}

#else // !WIN32
double get_seconds()
{
    struct timeval tv;
    gettimeofday(&tv,0);
    double t1 = (double)(tv.tv_sec+((double)tv.tv_usec/1000000.0));
    return t1;
}
#endif // WIN32 y/n

///////////////////////////////////////////////////////////////////////////////

// see : http://geoffair.org/misc/powers.htm
char *get_seconds_stg( double dsecs )
{
    static char _s_secs_buf[256];
    char *cp = _s_secs_buf;
    sprintf(cp,"%g",dsecs);
    if (dsecs < 0.0) {
        strcpy(cp,"?? secs");
    } else if (dsecs < 0.0000000000001) {
        strcpy(cp,"~0 secs");
    } else if (dsecs < 0.000000005) {
        // nano- n 10 -9 * 
        dsecs *= 1000000000.0;
        sprintf(cp,"%.3f nano-secs",dsecs);
    } else if (dsecs < 0.000005) {
        // micro- m 10 -6 * 
        dsecs *= 1000000.0;
        sprintf(cp,"%.3f micro-secs",dsecs);
    } else if (dsecs <  0.005) {
        // milli- m 10 -3 * 
        dsecs *= 1000.0;
        sprintf(cp,"%.3f milli-secs",dsecs);
    } else if (dsecs < 60.0) {
        sprintf(cp,"%.3f secs",dsecs);
    } else {
        int mins = (int)(dsecs / 60.0);
        dsecs -= (double)mins * 60.0;
        if (mins < 60) {
            sprintf(cp,"%d:%.3f min:secs", mins, dsecs);
        } else {
            int hrs = mins / 60;
            mins -= hrs * 60;
            sprintf(cp,"%d:%02d:%.3f hrs:min:secs", hrs, mins, dsecs);
        }
    }
    return cp;
}

///////////////////////////////////////////////////////
// cf_String implemetation
cf_String::cf_String(): _buf(0), _size(0), _strlen(0)
{
    _size = _increment;
    _buf  = (char *)malloc(_size * sizeof(char));
    if (!_buf) {
        ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
        exit(1);
    }
    clear();
}

cf_String::cf_String(const char *str): _buf(0), _size(0), _strlen(0)
{
    _size = _increment;
    _buf = (char *)malloc(_size * sizeof(char));
    if (!_buf) {
        ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
        exit(1);
    }
    this->Printf("%s", str);
}

cf_String::~cf_String()
{
    if (_buf)
        free(_buf);
}

void cf_String::clear()
{
    _strlen = 0;
    _buf[0] = 0;
}

const char *cf_String::Printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    clear();
    const char *result = _appendf(fmt, ap);
    va_end(ap);
    return result;
}

const char *cf_String::Appendf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const char *result = _appendf(fmt, ap);
    va_end(ap);
    return result;
}

const char *cf_String::Strcat( const char *stg )
{
    size_t len = ::strlen(stg);
    if ((_strlen + len + 1) > _size) {
        if ((_size + _increment) > (_strlen + len))
            _size += _increment;
        else
            _size += len + 1;
    	_buf = (char *)realloc(_buf, _size);
        if (!_buf) {
            ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
            exit(1);
        }
    }
    ::strcat(_buf,stg);
    _strlen = ::strlen(_buf);
    return _buf;
}

const char *cf_String::Strcpy( const char *stg )
{
    size_t len = ::strlen(stg);
    if (len + 1 > _size) {
        if ((_size + _increment) > len)
            _size += _increment;
        else
            _size += len + 1;
    	_buf = (char *)realloc(_buf, _size);
        if (!_buf) {
            ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
            exit(1);
        }
    }
    ::strcpy(_buf,stg);
    _strlen = ::strlen(_buf);
    return _buf;
}


const char *cf_String::_appendf(const char *fmt, va_list ap)
{
#ifdef _MSC_VER
    // Window's verion of vsnprintf() returns -1 when the string won't
    // fit, so we need to keep trying with larger buffers until it
    // does.
    int newLen;
    while ((newLen = vsnprintf_s(_buf + _strlen, _size - _strlen,
				 _size - _strlen - 1, fmt, ap)) < 0) {
        _size += _increment;
    	_buf = (char *)realloc(_buf, _size);
        if (!_buf) {
            ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
            exit(1);
        }
    }
#else
    size_t newLen;
    va_list ap_copy;
    va_copy(ap_copy, ap);

    newLen = vsnprintf(_buf + _strlen, _size - _strlen, fmt, ap);
    if ((newLen + 1) > (_size - _strlen)) {
	    // This just finds the next multiple of _increment greater
	    // than the size we need.  Perhaps nicer would be to find the
	    // next power of 2?
	    _size = (_strlen + newLen + 1 + _increment) / _increment * _increment;
	    _buf = (char *)realloc(_buf, _size);
        if (!_buf) {
            ::printf("%s: ERROR: Memory allocation FAILED!\n", mod_name);
            exit(1);
        }
	    vsnprintf(_buf + _strlen, _size - _strlen, fmt, ap_copy);
    }
    va_end(ap_copy);
#endif
    _strlen += newLen;
    return _buf;
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTION   : InStr
// Return type: int 
// Arguments  : char *lpb - source
//            : char *lps - needle
// Description: Return the position of the FIRST instance of the string in lps
//              Emulates the Visual Basic function.
///////////////////////////////////////////////////////////////////////////////
int InStr( char *lpb, char *lps )
{
   int   iRet = 0;
   int   i, j, k, l, m;
   char  c;
   i = (int)strlen(lpb);
   j = (int)strlen(lps);
   if( i && j && ( i >= j ) ) {
      c = *lps;   // get the first we are looking for
      l = i - ( j - 1 );   // get the maximum length to search
      for( k = 0; k < l; k++ ) {
         if( lpb[k] == c ) {
            // found the FIRST char so check until end of compare string
            for( m = 1; m < j; m++ ) {
               if( lpb[k+m] != lps[m] )   // on first NOT equal
                  break;   // out of here
            }
            if( m == j ) {  // if we reached the end of the search string
               iRet = k + 1;  // return NUMERIC position (that is LOGICAL + 1)
               break;   // and out of the outer search loop
            }
         }
      }  // for the search length
   }
   return iRet;
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTION   : InStri
// Return type: int 
// Arguments  : char *lpb - source
//            : char *lps - needle
// Description: Return the position of the FIRST instance of the string in lps
//              Ignores case.
///////////////////////////////////////////////////////////////////////////////
int InStri( char *lpb, char *lps )
{
   int   iRet = 0;
   int   i, j, k, l, m;
   char  c;
   i = (int)strlen(lpb);
   j = (int)strlen(lps);
   if( i && j && ( i >= j ) ) {
      c = toupper(*lps);   // get the first we are looking for
      l = i - ( j - 1 );   // get the maximum length to search
      for( k = 0; k < l; k++ ) {
         if( lpb[k] == c ) {
            // found the FIRST char so check until end of compare string
            for( m = 1; m < j; m++ ) {
               if( toupper(lpb[k+m]) != toupper(lps[m]) )   // on first NOT equal
                  break;   // out of here
            }
            if( m == j ) {  // if we reached the end of the search string
               iRet = k + 1;  // return NUMERIC position (that is LOGICAL + 1)
               break;   // and out of the outer search loop
            }
         }
      }  // for the search length
   }
   return iRet;
}

///////////////////////////////////////////////////////

char *get_gmt_stg(void)
{
    static char _s_gmt_buf[128];
    char *gmt = _s_gmt_buf;
    int len = sprintf(gmt,"%s",Get_Current_GMT_Time_Stg());
    while (len--) {
        if (gmt[len] > ' ')
            break;
        gmt[len] = 0;
    }
    return gmt;
}

std::string getHostStg( unsigned int addr )
{
    long x = ntohl(addr);
    std::string result = NumToStr ((x>>24) & 0xff, 0) + ".";
    result += NumToStr ((x>>16) & 0xff, 0) + ".";
    result += NumToStr ((x>> 8) & 0xff, 0) + ".";
    result += NumToStr ((x>> 0) & 0xff, 0);
    return result;
}


// eof - cf_misc.cxx

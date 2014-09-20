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

// Module: cf_misc.hxx
// Some miscellaneous functions
#ifndef _CF_MISC_HXX_
#define _CF_MISC_HXX_
#include <stdint.h>
#ifdef _MSC_VER
#include <WinSock2.h>
extern char *get_errmsg_text( int err ); // get a message from the system for this error value
#else
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_ntoa()
#endif // #ifdef _MSC_VER
#include <string>

extern char *get_address_stg( sockaddr_in *pAddr, int size );
extern char *get_base_name(char *name);
extern char *Get_UTC_Time_Stg(time_t Timestamp); // Creates the UTC time string using this timestamp
extern char *Get_Current_UTC_Time_Stg(); // Creates the UTC time string using current time
extern char *Get_Current_GMT_Time_Stg(); // Form Www Mmm dd hh:mm:ss yyyy GMT
extern char *get_gmt_stg(void); // form Mth date YEAR, cleaned
extern std::string getHostStg( unsigned int addr ); // return ???.???.???.??? form

extern int set_epoch_id_stg( char *cp, uint64_t id );
extern uint64_t get_epoch_id();
extern char *get_epoch_id_stg(uint64_t id);
extern uint64_t get_epoch_usecs();

extern int InStr( char *lpb, char *lps );
extern int InStri( char *lpb, char *lps );

#ifdef _MSC_VER
#define PFX64 "I64u"
#else
#define PFX64 PRIu64
#endif

#ifdef _MSC_VER
#define M_IS_DIR _S_IFDIR
#else // !_MSC_VER
#define M_IS_DIR S_IFDIR
#endif

enum DiskType {
    DT_NONE,
    DT_FILE,
    DT_DIR
};

extern DiskType is_file_or_directory ( char * path );

#ifdef _MSC_VER
#if !defined(HAVE_STRUCT_TIMESPEC)
#define HAVE_STRUCT_TIMESPEC
#if !defined(_TIMESPEC_DEFINED)
#define _TIMESPEC_DEFINED
struct timespec {
        time_t tv_sec;
        long tv_nsec;
};
#endif /* _TIMESPEC_DEFINED */
#endif /* HAVE_STRUCT_TIMESPEC */
extern int nanosleep( struct timespec *req, struct timespec *rem );
#endif // _MSC_VER

extern double get_seconds();
extern char *get_seconds_stg( double secs );

#ifndef DEF_INCREMENT
#define DEF_INCREMENT 1000
#endif

class cf_String
{
public:
    cf_String();
    cf_String(const char *str);
    ~cf_String();

    const char *Str() { return _buf;    }
    size_t Strlen()   { return _strlen; }
    void clear();
    const char *Printf( const char *fmt, ...);
    const char *Appendf( const char *fmt, ...);
    const char *Strcat( const char *stg );
    const char *Strcpy( const char *stg );

protected:
    char *_buf;
    size_t _size, _strlen;
    static const size_t _increment = DEF_INCREMENT;
    const char *_appendf(const char*fmt, va_list ap);
};

#endif // #ifndef _CF_MISC_HXX_
// eof - cf_misc.hxx


// logstream.hxx
#ifndef _LOGSTREAM_HXX_
#define _LOGSTREAM_HXX_
#include <sstream>
#include "sprtf.hxx"

using std::ostringstream;

// NOT USED
#define SG_SYSTEMS 1
#define SG_ALERT 1

#ifndef SPRTF
#define SPRTF sprtf
#endif

#ifdef FG_NDEBUG
#define SG_LOG(C,P,M)
#define SG_LOG2(C,P,M)
#else // #ifdef FG_NDEBUG

// log to file only - screen output is OFF
#define SG_LOG(C,P,M) { ostringstream msg; msg << M << std::endl; \
    SPRTF( msg.str().c_str() ); }
    
// log to file, but also send to stdout
#define SG_LOG2(C,P,M) { ostringstream msg; msg << M << std::endl; \
    int _cso = add_screen_out(1); \
    SPRTF( msg.str().c_str() ); \
    add_screen_out(_cso); }

#endif // FG_NDEBUG y/n

#endif // #ifndef _LOGSTREAM_HXX_
// eof - logstream.hxx

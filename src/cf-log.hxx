/*\
 * cf-log.hxx
 *
 * Copyright (c) 2014 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/

#ifndef _CF_LOG_HXX_
#define _CF_LOG_HXX_

extern int verbosity;

#define VERB1 (verbosity >= 1)
#define VERB2 (verbosity >= 2)
#define VERB5 (verbosity >= 5)
#define VERB9 (verbosity >= 9)

extern const char *raw_log;
extern double raw_bgn_secs, app_bgn_secs;
extern int get_next_block();
extern int open_raw_log();
extern void clean_up_log(); 

#endif // #ifndef _CF_LOG_HXX_
// eof - cf-log.hxx

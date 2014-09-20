/*\
 * cf-server.cxx
 *
 * Copyright (c) 2014 - Geoff R. McLane
 * Licence: GNU GPL version 2
 *
\*/

#include <stdio.h>
#include <string.h> // for strlen(), strdup(), ...
#include <time.h>
#ifndef _MSC_VER
#include <stdlib.h> // for atoi(), ...
#include <unistd.h> // usleep(), ...
#endif
#include "mongoose.h"
#include "cf-log.hxx"
#include "cf_misc.hxx"
#include "cf-pilot.hxx"
#include "sprtf.hxx"
#include "mpKeyboard.hxx"
#include "cf-server.hxx"

static const char *module = "cf-server";

// forward reference
void http_close();
void send_extra_headers(struct mg_connection *conn);

static bool use_sim_time2 = true;

#ifndef DEF_SERVER_PORT
#define DEF_SERVER_PORT 5555
#endif
#ifndef DEF_SLEEP_MS
#define DEF_SLEEP_MS 10     // was 100
#endif
// for select timeout
#ifndef DEF_TIMEOUT_MS
#define DEF_TIMEOUT_MS 50   // was 500
#endif

#ifndef SLEEP
#ifdef _MSC_VER
#define SLEEP(x) Sleep(x)
#else // !_MSC_VER
#define SLEEP(x) usleep( x * 1000 )
#endif // _MSC_VER y/n
#endif // SLEEP

static int port = DEF_SERVER_PORT;
// options
static int sleep_ms = DEF_SLEEP_MS;
static int timeout_ms = DEF_TIMEOUT_MS;
static const char *log_file = "temphttp.txt";

static size_t cb_cnt = 0;
static size_t http_cnt = 0;
static size_t json_cnt = 0;
static size_t xml_cnt = 0;
static size_t info_cnt = 0;

void show_http_stats()
{
    SPRTF("%s: %d cb, %d http get, %d json, %d xml, %d info.\n", module,
        (int)cb_cnt,
        (int)http_cnt,
        (int)json_cnt,
        (int)xml_cnt,
        (int)info_cnt );
}

/////////////////////////////////////////////////////////////////////////////
// utility functions
#ifndef ISNUM
#define ISNUM(a) ((a >= '0')&&(a <= '9'))
#endif
#ifndef ADDED_IS_DIGITS
#define ADDED_IS_DIGITS
static char *get_file_name( char *path )
{
    char *file = path;
    size_t ii, max = strlen(path);
    int c;
    for (ii = 0; ii < max; ii++) {
        c = path[ii];
        if ((c == ':')||(c == '\\')||(c == '/')) {
            if (path[ii+1]) {
                file = &path[ii+1];
            }
        }
    }
    return file;
}

static int is_digits(char * arg)
{
    size_t len,i;
    len = strlen(arg);
    for (i = 0; i < len; i++) {
        if ( !ISNUM(arg[i]) )
            return 0;
    }
    return 1; /* is all digits */
}

#endif // #ifndef ADDED_IS_DIGITS

////////////////////////////////////////////////////////////////////////////

static void give_help( char *name )
{
    printf("\n");
    printf("%s - version 1.0.0\n", name);
    printf("\n");
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    //      123456789112345678921
    printf(" --help   (-h or -?) = This help and exit(2)\n");
    printf(" --port <num>   (-p) = Set port (def=%d)\n", port);
    printf(" --raw <file>   (-r) = Set the raw udp log file to use. \n Default is '%s'\n", raw_log);
    printf(" --log <file>   (-l) = Set output log file. (def=%s, in CWD if relative)\n", log_file);
    printf(" --sleep <ms>   (-s) = Set milliseconds sleep in loop. 0 for none. (def=%d)\n", sleep_ms);
    printf(" --timeout <ms> (-t) = Set milliseconds timeout for select(). (def=%d)\n", timeout_ms);
    printf(" --verb[num]    (-v) = Bump or set verbosity. (def=%d)\n", verbosity);
    printf("\n");
    printf("Will establish a HTTP server on the port, and respond to GET with -\n");
    printf("/flights.json - return json list of current flights, updated each second\n");
    printf("/flights.xml  - return xml  list of current flights, updated each second\n");
    printf("\n");
    printf("All others will return 400 - command error, or 404 - file not found\n");
    printf("\n");
    printf("All output will be written to stdout, and the log file.\n");
    printf("Will exit on ESC keying, if in foreground\n");
}


static int parse_commands( int argc, char **argv )
{
    int iret = 0;
    int i, c, i2;
    char *arg, *sarg;
    for (i = 1; i < argc; i++) {
        i2 = i + 1;
        arg = argv[i];
        if (*arg == '-') {
            sarg = &arg[1];
            while (*sarg == '-') sarg++;
            c = *sarg;
            switch (c)
            {
            case 'r':
                if (i2 < argc) {
                    i++;
                    sarg = argv[i];
                    raw_log = strdup(sarg);
                    if (is_file_or_directory((char *)raw_log) == DT_FILE) {
                        SPRTF("%s: Set default base directory to '%s'\n", module, raw_log);
                    } else {
                        SPRTF("%s: Unable to 'stat' file '%s'! Aborting...\n", module, raw_log);
                        goto Bad_CMD;
                    }
                } else {
                    SPRTF("%s: Expected file name to follow %s!\n", module, arg );
                    goto Bad_CMD;
                }
                break;
            case 'h':
            case '?':
                give_help( get_file_name(argv[0]) );
                return 2;
            case 'l':
                i++;    // log file already checked and handled
                break;
            case 'p':
                if (i2 < argc) {
                    i++;
                    sarg = argv[i];
                    port = atoi(sarg);
                    SPRTF("%s: Set port to %d\n", module, port);
                } else {
                    SPRTF("%s: Expected port value to follow %s!\n", module, arg );
                    goto Bad_CMD;
                }
                break;
            case 's':
                if (i2 < argc) {
                    i++;
                    sarg = argv[i];
                    sleep_ms = atoi(sarg);
                    SPRTF("%s: Set sleep to %d ms\n", module, sleep_ms);
                } else {
                    SPRTF("%s: Expected ms sleep value to follow %s!\n", module, arg );
                    goto Bad_CMD;
                }
                break;
            case 't':
                if (i2 < argc) {
                    i++;
                    sarg = argv[i];
                    timeout_ms = atoi(sarg);
                    SPRTF("%s: Set select timeout to %d ms\n", module, timeout_ms);
                } else {
                    SPRTF("%s: Expected ms timeout value to follow %s!\n", module, arg );
                    goto Bad_CMD;
                }
                break;
            case 'v':
                sarg++; // skip the -v
                if (*sarg) {
                    // expect digits
                    if (is_digits(sarg)) {
                        verbosity = atoi(sarg);
                    } else if (*sarg == 'v') {
                        verbosity++; /* one inc for first */
                        while(*sarg == 'v') {
                            verbosity++;
                            arg++;
                        }
                    } else
                        goto Bad_CMD;
                } else
                    verbosity++;
                if (VERB1) printf("%s: Set verbosity to %d\n", module, verbosity);
                break;
            default:
                goto Bad_CMD;
                break;
            }
        } else {
Bad_CMD:
            SPRTF("%s: Unknown command %s\n", module, arg );
            return 1;
        }
    }

    return iret;
}

int check_log_file(int argc, char **argv)
{
    int i,fnd = 0;
    char *arg, *sarg;
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (*arg == '-') {
            sarg = &arg[1];
            while (*sarg == '-') sarg++;
            if (*sarg == 'l') {
                if ((i + 1) < argc) {
                    i++;
                    sarg = argv[i];
                    log_file = strdup(sarg);
                    fnd = 1;
                } else {
                    printf("%s: Expected a file name to follow [%s]\n", module, arg );
                    return 1;
                }
            }
        } else {

        }
    }
    set_log_file((char *)log_file, false);
    if (fnd)
        SPRTF("%s: Set LOG file to [%s]\n", module, log_file);
    return 0;
}

#if 0 // 000000000000000000000000000000000000000000000
const char *guess_content_type( const char *file )
{
    std::string f = file;
    std::string::size_type pos = f.rfind('.');
    if (pos > 0) {
        f = f.substr(pos+1);
        return get_mime_type_from_ext(f);
    }
    return "text/plain";    // what else to do????
}
#endif // 000000000000000000000000000000000000000000000

/////////////////////////////////////////////////////////////////////////////////////////
// the server
static bool use_plain_text = false;
static bool send_exta_hdrs = true;
static char info[] =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset=\"utf-8\">"
    "<title>Server Information</title>"
    "</head>"
    "<body>"
    "<h1 align=\"center\">Server Information</h1>"
    "<p>This server only respond to two URI :-</p>"
    "<ul>"
    "<li>/flights.json - Return a json encoded list of current pilots</li>"
    "<li>/flights.xml - The same list xml encoded list of current pilots</li>"
    "<li>/ or /info - Returns this page</li>"
    "</ul>"
    "<p><strong>All others will return 400 bad command, or 404 not found</strong></p>"
    "</body>"
    "</html>";

static int showOptions(struct mg_connection *conn)
{
    int iret = MG_FALSE;
    char *cp = info;
    int len = strlen(cp);
    if (len) {
        mg_send_header(conn,"Content-Type","text/html");
        mg_send_data(conn,cp,len);
        iret = MG_TRUE;
        if (VERB2) SPRTF("%s: Sent Information html, len %d\n", module, len);
    }
    return iret;
}

static int sendJSON(struct mg_connection *conn)
{
    int iret = MG_FALSE;
    char *cp = 0;
    int len = Get_JSON( &cp );
    if (len && cp) {
        if (use_plain_text)
            mg_send_header(conn,"Content-Type","text/plain");
        else
            mg_send_header(conn,"Content-Type","application/json");
        if (send_exta_hdrs)
            send_extra_headers(conn);
        mg_send_data(conn,cp,len);
        iret = MG_TRUE;
        if (VERB2) SPRTF("%s: Sent JSON string, len %d\n", module, len);
    }
    return iret;
}

static int sendXML(struct mg_connection *conn)
{
    int iret = MG_FALSE;
    char *cp = 0;
    int len = Get_XML( &cp );
    if (len && cp) {
        mg_send_header(conn,"Content-Type","text/xml");
        if (send_exta_hdrs)
            send_extra_headers(conn);
        mg_send_data(conn,cp,len);
        iret = MG_TRUE;
        if (VERB2) SPRTF("%s: Sent XML string, len %d\n", module, len);

    }
    return iret;
}


static int event_handler(struct mg_connection *conn, enum mg_event ev) 
{
    int iret = MG_FALSE;
    int i;
    bool verb = (VERB1) ? true : false;
    const char *q = (conn->query_string && *conn->query_string) ? "?" : "";
    cb_cnt++;
    if (ev == MG_AUTH) {
        return MG_TRUE;   // Authorize all requests
    } else if (ev == MG_REQUEST) {
        http_cnt++;
        SPRTF("%s: got URI %s%s%s\n", module,
            conn->uri,q,
            ((q && *q) ? conn->query_string : "") );
        if (VERB9) {
            if (conn->num_headers) {
                SPRTF("%s: Show of %d headers...\n", module, conn->num_headers);
                for (i = 0; i < conn->num_headers; i++) {
                    //struct mg_header *p = &conn->http_headers[i];
                    const char *n = conn->http_headers[i].name;
                    const char *v = conn->http_headers[i].value;
                    SPRTF(" %s: %s\n", n, v );
                }
            }
        }
        if ((strcmp(conn->uri,"/") == 0)||(strcmp(conn->uri,"/info") == 0)) {
            info_cnt++;
            iret = showOptions(conn);
        } else if (strcmp(conn->uri,"/flights.json") == 0) {
            json_cnt++;
            iret = sendJSON(conn);
        } else if (strcmp(conn->uri,"/flights.xml") == 0) {
            xml_cnt++;
            iret = sendXML(conn);
        } else {
            // iret = sendFile(conn);
        }
    }
    if ( (ev == MG_REQUEST) && (iret == MG_FALSE) ) {
        if (VERB1) {
            SPRTF("%s: No repsonse sent! Returning MG_FALSE to mongoose.\n", module );
        }
    }
    return iret;
}

//////////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER
#define snprintf _snprintf
#endif
#ifndef MVER
#define MVER MONGOOSE_VERSION
#endif

static char server_name[64];        // Set by init_server_name()
static struct mg_server *server;    // Set by start_mongoose()

void send_extra_headers(struct mg_connection *conn)
{
    mg_send_header(conn,"Access-Control-Allow-Origin","*");
    mg_send_header(conn,"Access-Control-Allow-Methods","OPTIONS, POST, GET");
    mg_send_header(conn,"Access-Control-Allow-Headers","Origin, Accept, Content-Type, X-Requested-With, X-CSRF-Token");
}

#define EV_HANDLER event_handler

static void init_server_name(void)
{
  const char *descr = " for cf-log project";
  snprintf(server_name, sizeof(server_name), "Mongoose web server v.%s%s",
           MVER, descr);
}


int http_init( const char *addr, int port, mg_handler_t handler )
{
    init_server_name();
    if (handler)
        server = mg_create_server(NULL, handler);
    else
        server = mg_create_server(NULL, EV_HANDLER);
    if (!server) {
        SPRTF("%s: mg_create_server(NULL, event_handler) FAILED!\n", module );
        return 1;
    }
    char *tmp = GetNxtBuf();
    sprintf(tmp,"%u",port);
    const char *msg = mg_set_option(server, "listening_port", tmp);
    if (msg) {
        http_close();
        SPRTF("%s: Failed to set listening port %u - %s!\n", module, port, msg);
        return 1;
    }

    // there is NO document root here mg_get_option(server, "document_root")
    SPRTF("%s: %s on port %s\n", module,
         server_name, 
         mg_get_option(server, "listening_port"));

    return 0;
}

// poll for http events - uses select with ms timeout
void http_poll(int timeout_ms)
{
    if (server) {
        mg_poll_server(server, timeout_ms);
    }
}

// close the http server
void http_close()
{
    if (server)
        mg_destroy_server(&server);
    server = 0;
    SPRTF("%s: destroyed mongoose server.\n", module);
}


//////////////////////////////////////////////////////////////////////////////////
void show_stats()
{
    SPRTF("%s: Show some stats...\n", module );
    show_packets();
    clean_up_pilots(false);
    show_http_stats();
}
//////////////////////////////////////////////////////////////////////////////////
int run_server()
{
    int res, iret = 0;
    time_t diff, next, curr = time(0);
    time_t pilot_ttl = m_PlayerExpires;
    time_t last_expire = curr;
    time_t last_json = curr;
    next = curr;
    bool need_reset = false;
    time_t reset_time = 0;
    SPRTF("%s: Waiting on %d... ESC to exit\n", module, port );
    while (1) {
        curr = time(0);
        res = test_for_input();
        if (res) {
            if (res == 0x1b) {
                SPRTF("%s: Got ESC exit key...\n", module );
                break;
            } else if ((res == '?')||(res == 's')) {
                show_stats();
            } else {
                SPRTF("%s: Got unknown key %X!\n", module, res );
            }
        }

        http_poll(timeout_ms);    // server->poll();

        bool get_udp = true;
        if (use_sim_time2 && got_sim_time) {
            double secs = get_seconds() - raw_bgn_secs;
            if (secs < elapsed_sim_time) {
                get_udp = false;
            }
        }
        if (get_udp) {
            // feed in next udp packet from raw log
            if (!need_reset) {
                if (!get_next_block()) {
                    SPRTF("%s: No more upd packets, need to expire all, and reset...\n", module);
                    need_reset = true;
                    reset_time = curr + pilot_ttl + 3;
                    got_sim_time = false;   // raw log restart, so restart sim timing
                    clean_up_log(); // ensure current log is CLOSED
                    clean_up_pilots();  // remove ALL pilots from vector
                }
            }
        }
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
        if (next != curr) {
            next = curr;
            // any one seconds tasks???
            if (sleep_ms > 0) {
                SLEEP(sleep_ms);
            }
        }
        if (need_reset) {
            if (curr > reset_time) {
                if (open_raw_log()) {
                    SPRTF("%s: Failed to re-open '%s' on reset!\n", module, raw_log);
                    iret = 1;
                    break;
                }
                if (!get_next_block()) {
                    SPRTF("%s: Failed to get_next_block of '%s' after reset!\n", module, raw_log);
                    iret = 1;
                    break;
                }
                need_reset = false; // and should be good to go again
            }
        }
    }
    return iret;
}

////////////////////////////////////////////////////////////////////////////////////////
int server_main( int argc, char **argv )
{
    int iret;
    iret = check_log_file(argc,argv);
    if (iret)
        return iret;

    iret = parse_commands(argc,argv);
    if (iret)
        return iret;

    iret = open_raw_log();
    if (iret)
        return iret;

    SPRTF("%s: Starting HTTP server using mongoose version %s, on port %d\n", module, MONGOOSE_VERSION,
        port );

    if (http_init(0,port,event_handler)) {
        SPRTF("%s: Server FAILED! Aborting...\n", module );
        return 1;
    }

    run_server();

    http_close();

    return iret;
}

// eof = cf-server.cxx

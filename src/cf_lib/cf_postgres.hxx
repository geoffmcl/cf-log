// cf_postgres.hxx
#ifndef _CF_POSTGRES_HXX_
#define _CF_POSTGRES_HXX_
#ifdef _MSC_VER
#pragma warning ( disable : 4996 )
#endif
#include <libpq-fe.h>
#include <string.h> // strdup()

typedef int (*SQLCB)(void*,int,char**,char**);

#define DEF_PG_DB   "crossfeed"
#define DEF_PG_IP   "127.0.0.1"
#define DEF_PG_PORT "5432"
#define DEF_PG_USER "crossfeed"
#define DEF_PG_PWD  "crossfeed"

class cf_postgres 
{
public:
    cf_postgres();
    ~cf_postgres();
    int db_open(bool create = false);
    void db_close();
    int db_ok();
    char *get_db_name() { return db_name; }
    int db_exec( const char *sql, SQLCB sqlcb = 0,
                    void *vp = 0, char **errmsg = 0, double *diff = 0 );
    void set_sql_cb( SQLCB sqlcb ) { sql_cb = sqlcb; }
    int query_count;
    double total_secs_in_query;
    int m_iMax_Retries, iBusy_Retries;
    SQLCB sql_cb;
    void set_db_name( char *file ) { db_name = strdup(file); }
    void set_db_host( char *host ) { db_host = strdup(host); }
    void set_db_port( char *port ) { db_port = strdup(port); }
    void set_db_user( char *user ) { db_user = strdup(user); }
    void set_db_pwd ( char *pwd  ) { db_pwd  = strdup(pwd);  }
    void set_db_opts( char *opts ) { db_opts = strdup(opts); }
    // create tables
    void set_ct_flights(char *sql) { ct_flights = sql;       }
    void set_ct_positions(char *sql) { ct_positions = sql;   }

    PGresult *res;
    ExecStatusType status;
private:
    PGconn *conn;
    char *db_host;
    char *db_port;
    char *db_user;
    char *db_pwd;
    char *db_name;
    char *db_opts;
    char *db_tty;

    char *ct_flights;
    char *ct_positions;

};

#endif // #ifndef _CF_POSTGRES_HXX_
// eof - cf_postgres.hxx

// cf_sqlite.hxx
#ifndef _CF_SQLITE_HXX_
#define _CF_SQLITE_HXX_
#ifdef _MSC_VER
#pragma warning ( disable : 4996 )
#endif
#include <sqlite3.h>
#include <string.h> // strdup()

typedef int (*SQLCB)(void*,int,char**,char**);

#define DEF_SQL_FILE "fgxtracker.db"

class cf_sqlite 
{
public:
    cf_sqlite();
    ~cf_sqlite();
    int open_db(bool create = false);
    void close_db();
    char *get_db_name() { return db_name; }
    void set_db_name(char *file) { db_name = strdup(file); }
    void set_ct_flights(char *sql) { ct_flights = sql; }
    void set_ct_positions(char *sql) { ct_positions = sql; }
    //int db_exec( sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**),
    int db_exec( const char *sql, SQLCB sqlcb,
                    void *vp, char **errmsg = 0, double *diff = 0 );
    void set_sql_cb( SQLCB sqlcb ) { sql_cb = sqlcb; }
    int query_count;
    double total_secs_in_query;
    int m_iMax_Retries, iBusy_Retries;
    SQLCB sql_cb;
private:
    sqlite3 *db;
    char *db_name;
    char *ct_flights;
    char *ct_positions;
};

#endif // #ifndef _CF_SQLITE_HXX_
// eof - cf_sqlite.hxx

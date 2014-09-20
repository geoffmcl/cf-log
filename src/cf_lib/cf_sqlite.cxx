// cf_sqlite.cxx

#include <time.h> // nanosleep() in unix
#include "cf_sqlite.hxx"
#include "sprtf.hxx"
#include "cf_misc.hxx"

static const char *mod_name = "cf_sqlite.cxx";

cf_sqlite::cf_sqlite()
{
    db = 0;
    db_name = (char *)"DEF_SQL_FILE";
    ct_flights = 0;
    ct_positions = 0;
    query_count = 0;
    total_secs_in_query = 0.0;
    m_iMax_Retries = 10;
    iBusy_Retries = 0;
    sql_cb = 0;
}

cf_sqlite::~cf_sqlite()
{


}

//////////////////////////////////////////////////////////////////////////////
// db_init()
//
// Intialize the DATABASE
//
//////////////////////////////////////////////////////////////////////////////
int cf_sqlite::open_db(bool create)
{
    if (!db) {
        char *pfile = get_db_name();
        if (!pfile)
            return 1;
        int rc;
        char *db_err;
        bool is_new = true;
        if ( is_file_or_directory((char *)pfile) == DT_FILE ) {
            is_new = false;
            SPRTF("%s: Re-openning database [%s]...\n",
				mod_name,
				pfile );
        } else {
            if (create) {
                if (!ct_flights || !ct_positions) {
                    SPRTF("%s: ERROR: Creating database [%s]...\n"
                        "either flights or positions sql not set!\n",
	    			    mod_name,
		    		    pfile );
                    return 1;
                } else {
                    SPRTF("%s: Creating database [%s]...\n",
	    			    mod_name,
		    		    pfile );
                }
            } else {
                SPRTF("%s: Error: database [%s] NOT FOUND!\n",
	    			mod_name,
		    		pfile );
                return 1;
            }
        }
        rc = sqlite3_open(pfile, &db);
        if ( rc ) {
            SPRTF("%s: ERROR: Unable to open database [%s] - %s\n",
                mod_name,
                pfile,
                sqlite3_errmsg(db) );
            sqlite3_close(db);
            db = (sqlite3 *)-1;
            return 1;
        }
        if (is_new) {
            //rc = sqlite3_exec(db, "create table 'helloworld' (id integer);", NULL, 0, &db_err)
            SPRTF("%s: Creating 'flights' table...\n",
				mod_name );
            rc = sqlite3_exec(db, ct_flights, NULL, 0, &db_err);
            if ( rc != SQLITE_OK ) {
                SPRTF("%s: ERROR: Unable to create 'flights' table in database [%s] - %s\n",
                    mod_name,
                    pfile,
                    db_err );
                 sqlite3_free(db_err);
                 sqlite3_close(db);
                 db = (sqlite3 *)-1;
                 return 2;
            }
            SPRTF("%s: Creating 'positions' table...\n",
				mod_name );
            rc = sqlite3_exec(db, ct_positions, NULL, 0, &db_err);
            if ( rc != SQLITE_OK ) {
                SPRTF("%s: ERROR: Unable to create 'positions' table in database [%s] - %s\n",
                    mod_name,
                    pfile,
                    db_err );
                 sqlite3_free(db_err);
                 sqlite3_close(db);
                 db = (sqlite3 *)-1;
                 return 3;
            }
        }
    }
    return 0;
}

void cf_sqlite::close_db()
{
    if (db && (db != (sqlite3 *)-1))
        sqlite3_close(db);
    db = 0;
}

int cf_sqlite::db_exec(
  const char *sql,                           /* SQL to be evaluated */
  int (*callback)(void*,int,char**,char**),  /* Callback function */
  void *vp,                                  /* 1st argument to callback */
  char **errmsg,                              /* Error msg written here */
  double *pdiff /* = NULL */
)
{
    int rc;
    int iret = 0;
    double t1, t2, diff;
    query_count++;
    t1 = get_seconds();
    rc = sqlite3_exec(db,sql,callback,vp,errmsg);
    if ( rc != SQLITE_OK ) {
        if ((rc == SQLITE_BUSY)  ||    /* The database file is locked */
            (rc == SQLITE_LOCKED) ) { /* A table in the database is locked */
                // wait a bit
                // perhaps these are NOT critical ERRORS, so what to do...
                struct timespec req;
                int max_tries = 0;
                while (max_tries < m_iMax_Retries) {
                    if (*errmsg)
                        sqlite3_free(*errmsg);
                    *errmsg = 0;
                    req.tv_sec = 0;
                    req.tv_nsec = 100000000;
                    nanosleep( &req, 0 ); // give over the CPU for 100 ms
                    rc = sqlite3_exec(db,sql,callback,vp,errmsg);
                    if ( rc != SQLITE_OK ) {
                        if ((rc == SQLITE_BUSY)  ||    /* The database file is locked */
                            (rc == SQLITE_LOCKED) ) { /* A table in the database is locked */
                            max_tries++;    // just wait some more
                            iBusy_Retries++;
                        } else
                            break;  // some other error - die
                    }
                }
        }
        if ( rc != SQLITE_OK ) {
            SPRTF("Query [%s] FAILED!\n[%s]\n", sql, *errmsg);
            iret = 1;
        }
    }
    t2 = get_seconds();
    diff = t2 - t1;
    total_secs_in_query += diff;
    if (pdiff)
        *pdiff = diff;
    return iret;
}

// eof - cf_sqlite.cxx



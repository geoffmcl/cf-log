// cf_postgres.cxx


#include "cf_postgres.hxx"
#include "sprtf.hxx"
#include "cf_misc.hxx"

static const char *mod_name = "cf_postgres";

cf_postgres::cf_postgres()
{
    conn = 0;
    ct_flights = 0;
    ct_positions = 0;
    query_count = 0;
    total_secs_in_query = 0.0;
    m_iMax_Retries = 10;
    iBusy_Retries = 0;
    sql_cb = 0;
    db_host = (char *)DEF_PG_IP;    // "127.0.0.1"; // "localhost";
    db_port = (char *)DEF_PG_PORT;  // 5432;
    db_user = (char *)DEF_PG_USER;  // "cf";
    db_pwd =  (char *)DEF_PG_PWD;   // "Bravo747g";
    db_name = (char *)DEF_PG_DB;    // "tracker_test";
    db_opts = (char *)"";
    db_tty = (char *)"";

}

cf_postgres::~cf_postgres()
{
    db_close();

}

//////////////////////////////////////////////////////////////////////////////
// int db_open()
//
// open the DATABASE
//
// db_opts - Trace/debug options to be sent to the server.
// db_tty  - A file or tty for optional debug output from the backend.
//
//////////////////////////////////////////////////////////////////////////////
int cf_postgres::db_open(bool create)
{
    int iret = 0;
    conn = PQsetdbLogin(db_host, db_port, db_opts, db_tty, db_name, db_user, db_pwd);
    if (!conn) {
        SPRTF("%s: ERROR: Connection to database failed due to memory!\n", mod_name);
        iret = 1;
    } else if (PQstatus(conn) != CONNECTION_OK) {
        SPRTF("%s: ERROR: Connection to database failed:\n [%s]\n", mod_name, PQerrorMessage(conn) );
        PQfinish(conn);
        iret = 1;
        conn = 0;
    }
    return iret;
}

void cf_postgres::db_close()
{
    if (conn)
        PQfinish(conn);
    conn = 0;
}

int cf_postgres::db_ok()
{
    if (!conn)
        return -1;
    if (PQstatus(conn) != CONNECTION_OK)
        return 1;
    return 0;
}


int cf_postgres::db_exec(
  const char *sql,  /* SQL to be evaluated */
  SQLCB sqlcb,      /* Callback function */
  void *vp,         /* 1st argument to callback */
  char **errmsg,    /* Error msg written here */
  double *pdiff     /* = NULL */
)
{
    double t1, t2, diff;
    query_count++;
    t1 = get_seconds();
    int iret = db_ok();
    if (errmsg)
        *errmsg = 0;
    if (iret) {
        if (iret == -1)
            SPRTF("%s: ERROR: Database has NOT been opened!\n", mod_name);
        else
            SPRTF("%s: ERROR: Database is NOT connected!\n", mod_name);
    } else {    // if (!iret) {
        res = PQexec( conn, sql );
        if (!res) {
            SPRTF("%s: FATAL PostgreDQL ERROR! Memory FAILED!\n", mod_name);
            return 1;
        }
        // if (PQ_EXEC_SUCCESS(res)) {
        status = PQresultStatus(res);
        if (status == PGRES_COMMAND_OK) {
            // -- Successful completion of a command returning NO data
            PQclear(res);
        } else if (status == PGRES_TUPLES_OK) {
            // -- The query successfully executed
            int nFields = PQnfields(res);   // get COLUMN count of query
            int nRows   = PQntuples(res);   // get ROW count of query
            if (nFields && nRows && sqlcb) {
                int i, j;
                // char* PQgetvalue(const PGresult *res, int tup_num, int field_num);
                char **cols = new char *[nFields+1];
                char **argv = new char *[nFields+1];
                // fill in the COLUMN names
                for (i = 0; i < nFields; i++) {
                    cols[i] = PQfname(res,i);
                } 
                cols[i] = 0;
                // for each ROW of data
                for (i = 0; i < nRows; i++) {
                    // for each COLUMN in that ROW
                    for (j = 0; j < nFields; j++) {
                        argv[j] = PQgetvalue(res, i, j);
                    }
                    argv[j] = 0;
                    // pass DATA back
                    if (sqlcb(vp,nFields,argv,cols))
                        break;  // end call backs
                }
                delete cols;
                delete argv;
            }
            PQclear(res);
        } else {
	   	    SPRTF("%s: PQexec(%s)\n FAILED:\n[%s]\n", mod_name, sql, PQerrorMessage(conn));
            PQclear(res);
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

// eof - cf_postgres.cxx

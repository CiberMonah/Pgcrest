// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _PGPOOL_H_
#define _PGPOOL_H_
#include <uv.h>
#include <libpq-fe.h>

/* Initializes PostgreSQL connection pool */
int pgpool_init(uv_loop_t* loop, const char* connstring, int max_conn, int timeout);


typedef int (*pgpool_result_cb)(PGconn* conn, void* data);

/* Requests connection */
int pgpool_execute(pgpool_result_cb query, pgpool_result_cb cb, pgpool_result_cb failure, void* data);



/* Release connection */
int pgpool_release(PGconn* conn);


/* Finalizes connections */
int pgpool_finalize();

#endif /* _PGPOOL_H_ */

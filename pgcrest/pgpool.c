// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdlib.h>
#include <string.h>
#include "pgpool.h"
#include <uv.h>
#include "logging.h"

static uv_loop_t* _loop;
static char _connstr[1024];
static unsigned int maxconn;
static int _timeout = 2;


typedef struct _pgpool_query {
	pgpool_result_cb querycb;
	pgpool_result_cb resultcb;
	pgpool_result_cb failurecb;
	void* data;
	struct _pgpool_query* next;

} pgpool_query_t;

typedef struct _pgpool_conn {
	PGconn* conn;
	int status;
	struct _pgpool_conn* next;
	uv_poll_t poll;
	pgpool_query_t* q;
	time_t lastupd;

} pgpool_conn_t;


uv_async_t msg;
uv_async_t check_queue;

static pgpool_query_t* query_queue = NULL;
static pgpool_query_t* reverse_queue = NULL;
static pgpool_conn_t* busy_conn_list = NULL;
static pgpool_conn_t* free_conn_list = NULL;
static pgpool_conn_t* tran_conn_list = NULL;

static unsigned int connections = 0;
static unsigned int associated  = 0;
static unsigned int released = 0;
static unsigned int freed = 0;
static unsigned int failed = 0;

static void reverse() {
	if ( reverse_queue ) return;
	log_debug("Reversing...");
	while (query_queue) {
		pgpool_query_t* head = query_queue;
		query_queue = head->next;
		head->next = reverse_queue;
		reverse_queue = head;
		log_debug("--reversed");
	}
}

static int queue_len(pgpool_conn_t* q) {
	if (!q) return 0;
	return 1+queue_len(q->next);
}

static int query_queue_len(pgpool_query_t* q) {
	if (!q) return 0;
	return 1+query_queue_len(q->next);
}


static void pgpool_connection_ready(uv_poll_t* handle, int status, int events);


void conn_insert(pgpool_conn_t** lst, pgpool_conn_t* c) {
	c->next = *lst;
	*lst = c;
}

void conn_remove(pgpool_conn_t** lst, pgpool_conn_t* c) {
	if ( !lst || !*lst) return;
	if ( *lst == c ) *lst = c->next;
	else conn_remove(&(*lst)->next, c);
}

/* find conn in lst and pop it out */
int conn_find(pgpool_conn_t** lst, pgpool_conn_t** c, PGconn* conn) {
	if ( !lst || !*lst) return 0;
	if ( (*lst)->conn == conn ) { 
		*c = *lst;
		*lst = (*lst)->next;
		return 1;
	} 
	return conn_find(&(*lst)->next, c, conn);
}

void connection_ready(pgpool_conn_t* c) {
	freed++;
	conn_insert(&free_conn_list, c);
	uv_async_send(&check_queue);	
}

void connection_failed(pgpool_conn_t* c) {
	failed++;
	conn_remove(&busy_conn_list, c);
	PQfinish(c->conn);
	free(c);
}


static void pgpool_connection_poll(uv_poll_t* poll) {
	pgpool_conn_t* c = (pgpool_conn_t*)poll->data;
	switch(PQconnectPoll(c->conn)) {
	case PGRES_POLLING_READING: log_debug("READING"); uv_poll_start(poll, UV_READABLE, pgpool_connection_ready); break;
	case PGRES_POLLING_WRITING: log_debug("WRITING"); uv_poll_start(poll, UV_WRITABLE, pgpool_connection_ready); break;
	case PGRES_POLLING_FAILED: log_debug("FAILED"); connection_failed(c); break;
	case PGRES_POLLING_OK: log_debug("READY"); connection_ready(c); break;
	default: break;
	}

}

void pgpool_connection_ready(uv_poll_t* handle, int status, int events) {
	log_debug("%s: status=%d events=%x", __FUNCTION__, status, events);
	uv_poll_stop(handle);
	if ( status == 0 && (events == UV_READABLE || events == UV_WRITABLE))
		pgpool_connection_poll(handle);

}


static void watch_connection_state(pgpool_conn_t *c) {
	if ( !c || ! c->conn ) return;
	c->poll.data = c; 
	pgpool_connection_poll(&c->poll);
}



pgpool_conn_t* create_conn() {
	PGconn* conn = PQconnectStart(_connstr);
	log_debug("Connecting with '%s'", _connstr);
	if (conn) {
		if ( PQstatus(conn) == CONNECTION_BAD ) {
			log_error("Connection bad");
			PQfinish(conn);
			return NULL;
		}
		pgpool_conn_t* c  = malloc(sizeof(pgpool_conn_t));
		if ( !c ) {
			log_warning("Cannot allocation pgpool_conn_t structure");
			PQfinish(conn);
			return NULL;
		}
		*c = (pgpool_conn_t){conn, PQstatus(conn), tran_conn_list};
		tran_conn_list = c;
		log_debug("Initialize socket %d polling for conn %p", PQsocket(conn), conn);
		uv_poll_init_socket(_loop, &c->poll, PQsocket(conn));
		connections++;
		log_debug("pool: conn _created_ %p", c);
		watch_connection_state(c);
		return NULL;
	}
	log_error("Cannot start connection");
	return NULL;
}

pgpool_conn_t* get_free_conn() {
	pgpool_conn_t* fc = free_conn_list;
	if ( !fc ) 
		return connections < maxconn? create_conn(): NULL;
	free_conn_list = fc->next;
	return fc;
}

void pgpool_read_result(uv_poll_t* poll, int status, int events) {
	log_debug("pgpool_read_result got with status %d and events %x", status, events);
	if ( status == 0 && events == UV_READABLE ) {
		pgpool_conn_t *c = (pgpool_conn_t*)poll->data;
		if ( PQconsumeInput(c->conn) == 0 ) {
			uv_poll_stop(poll);
			if ( c->q->failurecb ) c->q->failurecb(c->conn, c->q->data);
			else if ( c->q->resultcb) c->q->resultcb(c->conn, c->q->data);
			poll->data = NULL;
			free(c->q);
			c->q = NULL;
			return;
		}
		if (PQisBusy(c->conn) == 0) {
			uv_poll_stop(poll);
			log_debug("Calling result cb for %p", c->conn);
			if ( c->q->resultcb ) c->q->resultcb(c->conn, c->q->data);
			free(c->q);
			c->q = NULL;
		}
	}
}

static int _release(pgpool_conn_t* c) {
	uv_poll_stop(&c->poll);
	/* eat up all result if it is */
	for(PGresult *res = PQgetResult(c->conn); res != NULL; res = PQgetResult(c->conn))
		PQclear(res);
	log_debug("Cleared conn %p", c->conn);	
	if ( c->q ) {
		free(c->q);
		c->q = NULL;
	}
	switch(PQtransactionStatus(c->conn)) {
	case PQTRANS_IDLE: log_debug("Transaction is idle"); connection_ready(c); break;
	default: PQreset(c->conn); log_debug("Resetting current transation"); watch_connection_state(c); break;
	}
	return 0;
}

int pgpool_release(PGconn* conn) {
	pgpool_conn_t* c;
	if ( conn_find(&busy_conn_list, &c, conn) ) {
		released++;
		return _release(c);
	}
	return 0;
}

/* Associate connection c and query q */
int assoc_query_conn(pgpool_conn_t* c, pgpool_query_t* q) {
	c->lastupd = time(NULL);
	log_debug("Making con %p busy, q: %p", c->conn, q);
	associated++;
	conn_insert(&busy_conn_list, c);
	c->q = q;
	log_debug("Calling query %p cb for %p and %p, cb: %p", c, c->conn, q, q->querycb);	
	if ( q->querycb ) q->querycb(c->conn, q->data);
	log_debug("Start polling on %p", c->conn);
	uv_poll_start(&c->poll, UV_READABLE, pgpool_read_result);
	return 0;

}

static void check_queue_cb(uv_async_t* handle) {
	pgpool_query_t* head;
	reverse();
	if ( ! reverse_queue ) return;

	while ( (head = reverse_queue) != NULL ) {
		/* take next free conn */
		pgpool_conn_t* fc = get_free_conn();
		if (!fc) {
			/* no free connection
			 * so put head back to query queue
			 */
			head->next = query_queue;
			query_queue = head;
			reverse_queue = reverse_queue->next;
			return;
		}
		log_debug("Connection found: %p", fc->conn);
		/* pop from queue */
		reverse_queue = head->next;
		log_debug("Associating...");
		assoc_query_conn(fc, head);
	}
}

static uv_timer_t periodic;

static void periodic_cb(uv_timer_t* handle) {
	pgpool_conn_t** c;
	time_t t = time(NULL);
	log_debug("pool: C:%u, A: %u, B: %u, F: %u, R:%u, BQ: %d, FQ: %d, QQ: %d, RQ: %d",
			connections, associated, failed, freed, released,
			queue_len(busy_conn_list),
			queue_len(free_conn_list),
			query_queue_len(query_queue),
			query_queue_len(reverse_queue));
	for ( c = &busy_conn_list; *c ;c = &(*c)->next ) {
		log_debug("Checking conn %p with lastupd %lu and %lu", (*c)->conn, (*c)->lastupd+_timeout, t);
		if ( (*c)->lastupd+_timeout < t ) {
			pgpool_conn_t* next = (*c)->next; // store next pointer
			log_debug("Releasing conn %p next pointer is %p", (*c)->conn, next);
			_release(*c);
			*c = next;
			if ( !next ) return;
		}
	}

}


int pgpool_init(uv_loop_t* loop, const char* connstring, int max_conn, int timeout) {
	_loop = loop;
	maxconn = max_conn;
	_timeout = timeout;
	log_debug("Initializing connection string with '%s' and maxconn %d", connstring, max_conn);
	strncpy(_connstr, connstring, sizeof(_connstr));
	uv_async_init(loop, &check_queue, check_queue_cb);
	uv_timer_init(loop, &periodic);
	uv_timer_start(&periodic, periodic_cb, 1000, 10000);
	return 0;
}


int pgpool_execute(pgpool_result_cb query, pgpool_result_cb cb, pgpool_result_cb failure, void* data) {
	pgpool_query_t* q = malloc(sizeof(pgpool_query_t));
	if ( !q) return -1;
	
	*q = (pgpool_query_t){query, cb, failure, data, query_queue};
	query_queue = q;
	log_debug("Query cb: %p", query);
	uv_async_send(&check_queue);
	return 0;
}

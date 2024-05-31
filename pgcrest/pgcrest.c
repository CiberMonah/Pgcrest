// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include<stdio.h>
#include<string.h>
#include<uv.h>
#include<jwt.h>
#include <http_parser.h>
#include <libpq-fe.h>
#include <iniparser.h>
#include "pgpool.h"
#include "request.h"
#include "response.h"
#include "logging.h"


dictionary* _config;


#ifndef CONFIG_FILE
#define CONFIG_FILE "etc/pgcrest.conf"
#endif

#define CLIENTDATA(d) client_t* client = (client_t*)((d)->data)

#define RESPONSE                  \
  "HTTP/1.1 200 OK\r\n"           \
  "Content-Type: text/plain\r\n"  \
  "Content-Length: 14\r\n"        \
  "\r\n"                          \
  "Hello, World!\n"

#define UNAUTHORIZED_RESPONSE		\
	"HTTP/1.1 403 Unauthorized\r\n" \
	"\r\n"

#define SERVER_ERROR_RESPONSE		\
	"HTTP/1.1 500 Internal Server Error\r\n" \
	"Content-Type: text/plain\r\n" \
	"Content-Length: 15\r\n"	\
	"\r\n"                      \
	"Error occured!\n"

#define BAD_REQUEST_RESPONSE	\
	"HTTP/1.1 400 Bad Request\r\n" \
	"Content-Type: text/plain\r\n" \
	"Content-Length: 15\r\n"	\
	"\r\n"                      \
	"Error occured!\n"




#define CLST_INIT 0
#define CLST_READING_HEADERS 1
#define CLST_HEADERS_DONE 2
#define CLST_IMPERSONATION 3
#define CLST_SENT	4
#define CLST_CLOSED 5


#define MAX_HEADER 1024
#define HDR_SKIP 1
#define HDR_AUTHORIZATION 2
#define HDR_CONTENT_TYPE 3
#define HDR_CONTENT_LENGTH 4


/* types */
typedef struct {
	uv_tcp_t handle;
	http_parser parser;
	PGconn* conn;
	int clst;
	request_t request;
	response_t response;
	int final_result;
	int request_initialized;
	int response_initialized;
} client_t;

/* Parse main options */
typedef struct {
	char* string;
	int num_of_workers;
} main_opt;

/*init Global struct with main options*/
main_opt options = {CONFIG_FILE, 1};

/* to init default options */
#define INIT_DEFAULT_MAIN_OPT(string, num_of_workers) (main_opt){string, num_of_workers}	

/* variables */
static uv_tcp_t server;
static http_parser_settings parser_settings;

/* forward declaration */
static void close_cb(uv_handle_t *);
static void shutdown_cb(uv_shutdown_t *, int);
static void alloc_cb(uv_handle_t*, size_t, uv_buf_t*);
static void connection_cb(uv_stream_t *, int);
static void read_cb(uv_stream_t*, ssize_t, const uv_buf_t*);
static void write_cb(uv_write_t*, int);

static int headers_complete_cb(http_parser*);
static int url_cb(http_parser*, const char* at, size_t length);
static int header_field_cb(http_parser*, const char* at, size_t length);
static int header_value_cb(http_parser*, const char* at, size_t length);
static int body_cb(http_parser*, const char* at, size_t length);


static int on_pgpool_result(PGconn* conn, void* data);
static int on_pgpool_failure(PGconn* conn, void* data);


static int on_pgpool_global_result(PGconn* conn, void* data);


static void client_free(client_t* client) {
	if (client->request_initialized )
		request_free(&client->request);
	if (client->response_initialized)
		response_free(&client->response);
	free(client);
}

/* tcp callbacks */
void close_cb(uv_handle_t *handle) {
	client_t *client = (client_t *) handle->data;
	if ( client->clst == CLST_SENT ) {
		// closing and freeing
		client_free(client);
	} else { 
		client->clst = CLST_CLOSED;
	}
}

void shutdown_cb(uv_shutdown_t *shutdown_req, int status) {
	if(!uv_is_closing((uv_handle_t*) shutdown_req->handle))
		uv_close((uv_handle_t *) shutdown_req->handle, close_cb);
	free(shutdown_req);
}

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
	buf->base = malloc(suggested_size);
	buf->len = suggested_size;
}

void connection_cb(uv_stream_t *server, int status) {

	client_t *client = malloc(sizeof(client_t));
	if ( !client ) {
		log_error("Cannot allocate client");
		return;
	}
	int r = uv_tcp_init(server->loop, &client->handle);
	client->handle.data = client;
	client->request_initialized = 0;
	client->response_initialized = 0;
	r = uv_accept(server, (uv_stream_t *) &client->handle);
	if (r) {
		uv_shutdown_t *shutdown_req = malloc(sizeof(uv_shutdown_t));
		uv_shutdown(shutdown_req, (uv_stream_t *) &client->handle, shutdown_cb);
	}

	http_parser_init(&client->parser, HTTP_REQUEST);
	client->parser.data = client;
	uv_read_start((uv_stream_t *) &client->handle, alloc_cb, read_cb);
}

void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
	int r = 0;
	client_t *client = (client_t *) handle->data;

	if (nread >= 0) {
		size_t parsed = http_parser_execute(&client->parser, &parser_settings, buf->base, nread);

		if (parsed < nread) {
			log_error("parse error");
			uv_close((uv_handle_t *) handle, close_cb);
		}

	} else {
		if (nread == UV_EOF) {
			// do nothing
		} else {
			log_info("read: %s\n", uv_strerror(nread));
		}

		uv_shutdown_t *shutdown_req = malloc(sizeof(uv_shutdown_t));
		r = uv_shutdown(shutdown_req, handle, shutdown_cb);
	}
	free(buf->base);
}

void write_cb(uv_write_t* write_req, int status) {
	/* may be do not close here
	 * keep connection alive
	 * */
	uv_close((uv_handle_t *) write_req->handle, close_cb);
	if ( write_req->data )
		free(write_req->data);
	free(write_req);
}


int send_const_response(client_t* client, char* resp) {
	uv_write_t *write_req = malloc(sizeof(uv_write_t));
	if (!write_req) {
		log_error("Cannot allocate memory for write request");
		return -1;
	}
	uv_buf_t buf = uv_buf_init(resp, strlen(resp));
	write_req->data = NULL;
	uv_write(write_req, (uv_stream_t*)&client->handle, &buf, 1, write_cb);
	return 0;
}

int raise_unauthorized(client_t* client) {
	return send_const_response(client, UNAUTHORIZED_RESPONSE);
}

int raise_server_error(client_t* client) {
	return send_const_response(client, SERVER_ERROR_RESPONSE);
}

int raise_bad_request(client_t* client) {
	return send_const_response(client, BAD_REQUEST_RESPONSE);
}




/* The first callback in the protocol
 * on_pgpool_client_start -> on_pgpool_result
 */
static int on_pgpool_client_start(PGconn* conn, void* data) {
	client_t* client = (client_t*)data;	
	client->clst = CLST_IMPERSONATION;
	int err = 0;
	err = request_build_query(&client->request, conn);
	return err;
}

/* http callback
 *
 * headers_complete_cb -> on_pgpool_client_start -> on_pgpool_result -> write 
 *
 *
 *
 */
int headers_complete_cb(http_parser* parser) {
	CLIENTDATA(parser);
	client->clst = CLST_HEADERS_DONE;
	/* trying to authorize */
	if ( request_try_authorize(&client->request) ) {
		/* TODO: add more details about auth error */
		return raise_unauthorized(client);
	}
	/* start SQL querying */
	log_debug("start sql querying");
	pgpool_execute(on_pgpool_client_start, on_pgpool_result, on_pgpool_failure, client);
	log_info("parsing done");

	return 0;
}


int body_cb(http_parser* parser, const char* at, size_t length) {
	CLIENTDATA(parser);
	return request_body(&client->request, at, length);
}

int url_cb(http_parser* parser, const char* at, size_t length) {
	CLIENTDATA(parser);
	request_init(&client->request, parser->method, _config);
	client->request_initialized = 1;
	return request_parse_url(&client->request, at, length);
}


int header_field_cb(http_parser* parser, const char* at, size_t length) {
	CLIENTDATA(parser);
	return request_header_field(&client->request, at, length);

}

int header_value_cb(http_parser* parser, const char* at, size_t length) {
	CLIENTDATA(parser);
	return request_header_value(&client->request, at, length);
}

/* server */
void server_init(uv_loop_t *loop) {
	int r = uv_tcp_init_ex(loop, &server, AF_INET);
	int port = iniparser_getint(_config, "main:port", 9933);

	struct sockaddr_in addr;
	if (r) {
		log_error("Cannot initialize tcp: error %d", errno);
		return;
	}
	uv_ip4_addr(iniparser_getstring(_config, "main:bind", "127.0.0.1"),
					port, &addr);
	
	int  on = 1;
  	if(setsockopt(server.io_watcher.fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) {
		log_error("Cannot set SO_REUSEPORT for sock %d option: %d, %s", server.io_watcher.fd, errno, strerror(errno) );
	}

	r = uv_tcp_bind(&server, (struct sockaddr *) &addr, 0);
	if (!r) log_info("Server address bound");

	r = uv_listen((uv_stream_t *) &server,
				iniparser_getint(_config, "main:backlog", 1024), connection_cb);
	if (!r) log_info("Start listening... on port %d", port);
}

int load_config(main_opt* opt) {
	_config = iniparser_load(opt->string);
	if ( _config == NULL ) {
		log_error("Cannot parse config file");
		return -1;
	}
	log_debug("Load sections: main: %d, pgpool: %d",
			iniparser_getsecnkeys(_config, "main"),
			iniparser_getsecnkeys(_config, "pgpool"));
	return 0;
}

int global_state = 0;

int on_pgpool_global_connect(PGconn* conn, void* data) {
	if ( data == NULL ) {
		log_info("global connection got %p", conn);
		global_state  = 0; /* impersonating */
		PQsendQuery(conn, "begin; set local role webuser; select * from api.todos; commit;");
	}
	return 0;
}


int on_pgpool_failure(PGconn* conn, void* data) {
	log_warning("On Failure got from conn %p data %p", conn, data);
	return 0;
}



static int send_response(client_t* client) {
	char* resp;
	size_t sz;
	uv_write_t *write_req = malloc(sizeof(uv_write_t));
	if ( !write_req ) {
		log_error("Cannot allocate memory for response");
		return -1;
	}
	FILE* f = open_memstream(&resp, &sz);
	if ( !f ) {
		log_error("Cannot start building response");
		free(write_req);
		return -2;
	}
	response_dump(&client->response, f);
	fclose(f);
	uv_buf_t buf = uv_buf_init(resp, sz);
	write_req->data = resp;
	uv_write(write_req, (uv_stream_t*)&client->handle, &buf, 1, write_cb);
	// move it to the sent state
	client->clst = CLST_SENT;
	return 0;

}

int on_pgpool_global_result(PGconn* conn, void* data) {
	log_info("On global Result got from conn %p data %p", conn, data);
	PGresult* res;
	while ( (res = PQgetResult(conn)) ) {
		ExecStatusType status_type = PQresultStatus(res);
		log_info("Result status: %s", PQresStatus(status_type));
		log_warning("Result error messgae: %s", PQresultErrorMessage(res));
		//PQprintOpt opt = {1,1,0,0,0,0,"\t"};
		//PQprint(stdout, res, &opt);
		PQclear(res);
	}
	pgpool_release(conn);
	return 0;
}

int on_pgpool_result(PGconn* conn, void* data) {
	client_t* client = (client_t*)data;
	PGresult* res;
	
	log_info("On Result got from conn %p data %p", conn, data);
	
	// Check whether the client in a right state:
	if (client->clst != CLST_IMPERSONATION ) {
		log_warning("The client in a wrong state -- do nothing, just close");
		// readup all and close 
		while ((res= PQgetResult(conn))) PQclear(res);
		pgpool_release(conn);
		client_free(client);
		return -1;
	}

	response_init(&client->response, client->request.final_result);
	client->response_initialized = 1;
	while ( (res = PQgetResult(conn)) ) {
		response_result(&client->response, res);
		ExecStatusType status_type = PQresultStatus(res);
		log_info("Result status: %s", PQresStatus(status_type));
		log_warning("Result error message: %s", PQresultErrorMessage(res));
		//if ( global_state == 0  && status_type == PGRES_COMMAND_OK ) {
		//	pgpool_execute(on_pgpool_global_impersonated, on_pgpool_result, on_pgpool_failure, NULL);
		//} else if ( global_state == 1 && status_type == PGRES_TUPLES_OK ) {
		//	log_info("Releasing...");
		//	pgpool_release(conn);
		//} else {
		//if ( status_type == PGRES_TUPLES_OK ) {
		//	PQprintOpt opt = {1,1,0,0,0,0,"\t"};
		//	PQprint(stdout, res, &opt);
		//}
		PQclear(res);
	}
	log_debug("No more results...");
	/* all results got, sending response */
	send_response(client);
	log_debug("Releasing conn %p", conn);
	pgpool_release(conn);
	return 0;
}

void periodic_cb(uv_timer_t* handle) {
	log_debug("Periodic call");
	pgpool_execute(on_pgpool_global_connect, on_pgpool_global_result, on_pgpool_failure, NULL);	
}

void usage()
{
    printf( "Usage: pgcrest [-h] [-f CONFIG] [-w number_of_workers]\n"
            "-f CONFIG  load configuration from  CONFIG path instead  of default '%s'\n"
			"-w NUMBER put number of paralel processes\n", CONFIG_FILE);
	exit(1);
    return;
}

// Puts options to the Global struct options
void parse_main_opt(int argc, char *argv[]) 
{	
	char opt;
        
    while((opt = getopt(argc, argv, "w:hf:")) != -1) 
    {
        switch(opt){
            case 'f':
                options.string = strdup(optarg);
                break;
            case 'h':
				usage();
                break;
			case 'w':
				options.num_of_workers = atoi(optarg);
            default:
                break;
        }
    }
}

uv_signal_t term_signal;

void handler(uv_signal_t* handle, int signum) {
    pid_t pid;

	switch(signum) {
		case SIGTERM:
			exit(0);
	}
}

int main(int argc, char *argv[]) {
	parse_main_opt(argc, argv);

	if (load_config(&options)) return -log_error("Server start failed");
	
	uv_loop_t *loop = uv_default_loop();
	parser_settings.on_url = url_cb;
	parser_settings.on_header_field = header_field_cb;
	parser_settings.on_header_value = header_value_cb;
	parser_settings.on_headers_complete = headers_complete_cb;
	parser_settings.on_body = body_cb;
	server_init(loop);

	if (pgpool_init(loop,
				iniparser_getstring(_config, "pgpool:connstr", "host=localhost;dbname=test1;"),
				iniparser_getint(_config, "pgpool:maxconn", 10),
				iniparser_getint(_config, "pgpool:timeout", 10)))
		return -log_error("pgpool initialization failed");
	log_debug("pgpool initialized");

	uv_timer_t periodic;

	uv_timer_init(loop, &periodic);

	uv_signal_init(loop, &term_signal);

    uv_signal_start(&term_signal, handler, SIGTERM);

	//uv_timer_start(&periodic, periodic_cb, 1000, 1000*iniparser_getint(_config, "pgpool:pingrate", 10));

	int r = uv_run(loop, UV_RUN_DEFAULT);
	
	/* If no more events here, cleanup loop */
	iniparser_freedict(_config);
	return 0;
}
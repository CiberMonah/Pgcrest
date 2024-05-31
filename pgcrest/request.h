// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _REQUEST_H_
#define _REQUEST_H_
#include <http_parser.h>
#include <iniparser.h>
#include <libpq-fe.h>
#include "auth.h"
#include "header.h"


#define MAX_PATH_SEGMENTS 3
#define MAX_QUERY_PARAMS 10
#define MAX_PARAMS 9



typedef struct _request  {
	struct http_parser_url parsed_url;
	int method; /* HTTP method */
	char* url;
	char* body;
	dictionary* config;
	header_t* headers;
	char* qnames[MAX_QUERY_PARAMS];
	char* qvalues[MAX_QUERY_PARAMS];
	int nqs; /* number of query string params */
	char* path[MAX_PATH_SEGMENTS];
	int npath; /* number of non-empty path segments */
	int final_result; /* number of the result in a series of sql requests */
	auth_t auth; /* authorization state */
} request_t;

/* initializes request with http method
 * The method is the code taken from http_parser.h HTTP_XXX
 */
int request_init(request_t* r, int method, dictionary* config);

int request_parse_url(request_t* r, const char* at, size_t length);

/** Takes PGconn and sends queries */
int request_build_query(request_t* r, PGconn* conn);

int request_header_field(request_t* r, const char* at, size_t length);
int request_header_value(request_t* r, const char* at, size_t length);
/** Set the request body */
int request_body(request_t* r, const char* at, size_t length);

/** Try to authorize user
 * @returns AUTH_OK (zero) in case of successfull auth
 */
int request_try_authorize(request_t* r);

void request_free(request_t* r);


#endif /* _REQUEST_H_ */

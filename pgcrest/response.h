// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _RESPONSE_H_
#define _RESPONSE_H_
#include <libpq-fe.h>
#include <jansson.h>
#include "header.h"

typedef struct _response {
	int final_result;
	int current_result;
	header_t* headers;
	json_t* json;
	int status;
	int error;
} response_t;


int response_init(response_t*, int final_result);
void response_free(response_t* );
/** Builds final response with headers */
int response_dump(response_t*, FILE* f);
/* Add pg result to response */
int response_result(response_t*, PGresult* r);


#endif /* _RESPONSE_H_ */

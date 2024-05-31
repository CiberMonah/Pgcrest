// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <libpq-fe.h>
#include "response.h"
#include "logging.h"
#include "header.h"


int response_init(response_t* r, int final_result) {
	memset(r, 0, sizeof(response_t));
	r->final_result = final_result;
	r->status = 200; /* default HTTP status -- 200 OK */
	return 0;
}


void response_free(response_t* r) {
	header_free(r->headers);
	if (r->json) json_decref(r->json);
}

static json_t* _value_to_json(PGresult* res, int nrow, int j) {
	json_t* value;
	char* sqlv = PQgetvalue(res, nrow, j);
	int sqllen = PQgetlength(res, nrow, j);
	if(PQgetisnull(res, nrow, j)) 
		value = json_null();
	else {
		switch(PQftype(res, j)) {
		case 16: //BOOLOID
			value = json_boolean(sqlv && sqlv[0] == 'T'); break;
		case 18: case 19: case 25: case 1043: // CHAR
			value = json_string(sqlv); break;
		case 1082: case 1083: // DATE
			value = json_string(sqlv); break;
		case 3802: case 114:
			value = json_loads(sqlv, sqllen, NULL); break;
		case 20: case 21: case 23: // 
			value = json_integer(atoi(sqlv)); break;
		case 700: case 701: case 1700: // numeric 
			value = json_real(atof(sqlv)); break;
		default:
			value = json_loads(sqlv, sqllen, NULL); break;
		}
	}
	return value;

}

static int _row_to_json(json_t* row, PGresult* res, int nrow) {
	for( int j = 0; j < PQnfields(res); j++ ) {
		char* name = PQfname(res, j);
		json_t* value = _value_to_json(res, nrow, j);
		json_object_set_new(row, name, value);
	}
	return 0;
}

int response_result(response_t* r, PGresult* res) {
	/* for any result check where it is failed or not */
	ExecStatusType status_type = PQresultStatus(res);
	log_info("Adding response status: %s (%d of %d)", PQresStatus(status_type),
			r->current_result,
			r->final_result);
	if ( status_type != PGRES_TUPLES_OK
			&& status_type != PGRES_COMMAND_OK ) {
		/** error occured */
		r->status=400;
		r->json =  json_pack("{s:s,s:i,s:s,s:s}",
				"error", PQresStatus(status_type),
				"status", status_type,
				"message", PQresultErrorMessage(res),
				"details", PQresultVerboseErrorMessage(res,2,1));
		log_warning("Setting up error flag!");
		r->error = 1;
		return -1;
	}
	if ( !r->error && r->current_result++ == r->final_result ) {
		log_info("Awaited result found");
		if (status_type == PGRES_TUPLES_OK ) {
			/* we have tuples, put them as json */
			/* special case for single column single value json response */
			if ( PQntuples(res) == 1 && PQnfields(res) == 1
					&& PQftype(res, 0) == 3802 ) {
				r->json = json_loads(PQgetvalue(res, 0, 0),
						PQgetlength(res, 0, 0), NULL);
				return 0;
			}
			/* compact form */
			r->json = json_object();
			json_t* head = json_array();
			json_t* data = json_array();
			json_t* types = json_array();
			for ( int h = 0; h < PQnfields(res); h++) {
				json_array_append_new(head, json_string(PQfname(res, h)));
				json_array_append_new(types, json_integer(PQftype(res, h)));
				json_t* col = json_array();
				for ( int r = 0; r < PQntuples(res); r++ )
					json_array_append_new(col, _value_to_json(res, r, h));	
				json_array_append_new(data, col);
			
			}
			json_object_set_new(r->json, "head", head);
			json_object_set_new(r->json, "count", json_integer(PQntuples(res)));
			json_object_set_new(r->json, "types", types);
			json_object_set_new(r->json, "data", data);
			/*
			r->json = json_array();
			for( int i = 0; i < PQntuples(res); i++ ) {
				json_t* row = json_object();
				_row_to_json(row, res, i);
				json_array_append_new(r->json, row);
			}
			*/
		} else if ( status_type == PGRES_COMMAND_OK ) {
			/* THe result is just command */
			r->json = json_pack("{s:b}", "ok", 1);
		} else {
			/* Unonwn result */
			r->status=401;
			r->json = json_pack("{s:b, s,s, s:i}",
						"ok", 1,
						"message", "unkonwn status",
						"status", status_type);
		}
		return 0;
		
	}
	/* otherwise -- do nothing */
	log_debug("Do nothing for result...");
	return 0;
}

int response_dump(response_t* r, FILE* f) {
	log_debug("Building response");
	fprintf(f, "HTTP/1.1 %d OK\r\n", r->status);
	if (r->json) {
		fprintf(f, "Content-Type: application/json;charset=utf-8\r\n");
		fprintf(f, "Content-Length: %lu\r\n", json_dumpb(r->json, NULL, 0, 0));
		fprintf(f, "\r\n");
		json_dumpf(r->json, f, 0);
	} else {
		fprintf(f, "Content-Type: plain/text;charset=utf-8\r\n"
				"Content-Length: 12\r\n"
				"\r\n"
				"Unknown type");
	}
	return 0;
}


// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <http_parser.h>
#include <iniparser.h>
#include "auth.h"
#include "request.h"
#include "logging.h"
#include "header.h"

#define MAX(a, b) ((a)>(b)?(a):(b))
#define MIN(a, b) ((a)<(b)?(a):(b))

static char* unescape(char* x) {
	char* result = malloc(strlen(x)+1);
	if ( !result) {
		log_error("Cannot allocate memory for unescape");
		return NULL;
	}
	char* r = result;
	char a;
	int esc = 0;
	unsigned char byte = 0;
	while ((a = *x)) {
		if ( esc ) {
			if (a >= '0' && a <= '9') byte = (byte << 4) | a-'0';
			else if ( a >='a' && a <= 'f') byte = (byte << 4) | a+10-'a';
			else if ( a >='A' && a <= 'F') byte = (byte << 4) | a+10-'A';
			if ( !(--esc) ) 
				*(r++) = byte;
		} else {
			if ( a == '%' ) esc=2, byte=0;
			else if ( a == '+') byte = ' ';
			else byte = a;
			*(r++) = byte;
		}
		x++;
	}
	*r = 0;
	return result;
}


int request_init(request_t* r, int method, dictionary* config) {
	memset(r, 0, sizeof(request_t));
	r->method = method;
	r->config = config;
	return 0;
}

int strlncpy(char* dst, const char* src, size_t dst_size, size_t src_size) {
	char* x = dst;
	const char* y = src;
	while (x < dst+dst_size-1 && y < src+src_size && *y)
		*(x++) = *(y++);
	*x = 0;
	return x-dst;
}


int request_parse_url(request_t* r, const char* at, size_t length) {
	char* tok;
	char buf[1024];
	http_parser_url_init(&r->parsed_url);
	if ( http_parser_parse_url(at, length, 0, &r->parsed_url) ){
		log_warning("Error while parsing url %.*s", length, at);
		return -1;
	}
	r->url = strndup(at, length);
	
	if ( !r->url) {
		log_error("Cannot allocate memory for url");
		return -2;
	}

	/* split path by segments */
	strlncpy(buf, r->url + r->parsed_url.field_data[UF_PATH].off,
			1023, r->parsed_url.field_data[UF_PATH].len);
	
	char* s = buf;
	log_debug("Parsing PATH: '%s', length %d", buf, r->parsed_url.field_data[UF_PATH].len);
	for(r->npath =0, tok = strsep(&s, "/"); 
			tok && r->npath < MAX_PATH_SEGMENTS; 
			tok = strsep(&s, "/")) 
		if ( *tok ) r->path[r->npath++] = unescape(tok); /* do not store empty path */

	/* split query string by name=value pairs */
	strlncpy(buf, r->url + r->parsed_url.field_data[UF_QUERY].off,
			1023, r->parsed_url.field_data[UF_QUERY].len);
	for(r->nqs = 0, s = buf, tok = strsep(&s, "&;");
			tok && r->nqs < MAX_QUERY_PARAMS;
			tok = strsep(&s, "&;"))
		if ( *tok ) {
			char* q = tok;
			r->qnames[r->nqs] = unescape(strsep(&q, "="));
			r->qvalues[r->nqs++]  = q?unescape(q):NULL;
		}
	return 0;
}

int request_header_field(request_t* r, const char* at, size_t length) {
	return header_push(&r->headers, strndup(at, length));
}

int request_header_value(request_t* r, const char* at, size_t length) {
	int  err =  header_set(r->headers, strndup(at, length));
	if ( err ) {
		log_error("Cannot set header value %.*s", (int)length, at);
	}
	return err;
}

int request_body(request_t* r, const char* at, size_t length) {
	r->body = strndup(at, length);
	log_debug("Body got: %s", r->body);
	return 0;
}




/** Returns body content type */
const char* get_content_type(header_t* h) {
	const char* ct;
	if ( header_find(h, "content-type", &ct) ){
		if (!strcasecmp(ct, "application/json") ) return "jsonb";
		else if ( !strcasecmp(ct, "plain/text")) return "text";
		else return "text";
	} 
	return NULL;

}


/* Parses url
 * GET a/b -- first search the table a.b, then view, then function a.b()
 * GET a/b?select=a,b,c&col1=qe&col2=qwe;
 *
 */
static int url_to_query(request_t* r, PGconn* conn, FILE* f) {
	char* params[MAX_PARAMS];
	int nparams = 0;
	const char* schema = iniparser_getstring(r->config, "pgpool:schema", "");
	char* name;
	char* select = "*";
	const char* ctype = get_content_type(r->headers);
	if(r->npath < 1)
		return -1;
	
	if(r->npath > 1) {
		schema = strchr(r->path[0], ';')?NULL:r->path[0];
		name = r->path[1];
	} else {
		name = r->path[0];
	}

	if ( r->nqs > 0 && !strcmp(r->qnames[0], "select") 
		&& r->qvalues[0] && !strchr(r->qvalues[0], ';') )
			select = r->qvalues[0];

	fprintf(f, "select %s from ", select);
	
	if ( schema && *schema ) 
		fprintf(f, "%s.", schema);

	switch(r->method) {
	case HTTP_GET: fprintf(f, "get_"); break;
	case HTTP_POST: fprintf(f, "post_"); break;
	case HTTP_PATCH: fprintf(f, "patch_"); break;
	case HTTP_PUT: fprintf(f, "put_"); break;
	case HTTP_DELETE: fprintf(f, "delete_"); break;
	default: fprintf(f, "any_"); break;
	}
	
	fprintf(f, "%s", name);
	if (ctype && r->body) {
		char* esc = PQescapeLiteral(conn, r->body, strlen(r->body));
		fprintf(f, "(%s::%s)", esc, ctype);
		PQfreemem(esc);
	}

	/* finalize */
	fprintf(f, ";\n");
	return 0;
}




int request_build_query(request_t *r, PGconn* conn) {
	const char* pre_exec;
	char *buf;
	size_t sz;
	FILE* f = open_memstream(&buf, &sz);
	if ( !f ) {
		log_error("Cannot start building response");
		return -1;
	}
	r->final_result = 5;

	/* Switch into pipeline mode */
	//if(!PQenterPipelineMode(conn))
	//	log_warning("Cannot set pipeline mode");
	//
	fprintf(f, "BEGIN; ");
	/* Impersonate */
	fprintf(f,
			"set local role %s;\n",
			r->auth.role?r->auth.role:iniparser_getstring(r->config, "sql:anonrole", "webuser"));


	/* setting configuration */
	const char * ct = NULL;
	const char * accept = NULL;
	header_find(r->headers, "content-type", &ct);
	header_find(r->headers, "accept", &accept);
	char* _claims = r->auth.claims?PQescapeLiteral(conn, r->auth.claims, strlen(r->auth.claims)):NULL;
	char* _accept = accept?PQescapeLiteral(conn, accept, strlen(accept)):NULL;
	char* _ct = ct?PQescapeLiteral(conn, ct, strlen(ct)):NULL;
	fprintf(f, "select set_config('request.jwt.claims', %s::text, true);", _claims?_claims:"NULL");
	fprintf(f, "select set_config('request.content_type', %s::text, true);", _ct?_ct:"NULL");
	fprintf(f, "select set_config('request.accept', %s::text, true);", _accept?_accept:"NULL");
	if(_accept) PQfreemem(_accept);
	if(_claims) PQfreemem(_claims);
	if(_ct) PQfreemem(_ct);
	/* add pre-exec stage */
	if ( (pre_exec = iniparser_getstring(r->config, "sql:pre-exec", NULL)) ) { 
		fprintf(f, "select %s();", pre_exec);               /* do some pre-exec if needed */
		r->final_result ++;
	}
	if (url_to_query(r, conn, f)) {
		fclose(f);
		free(buf);
		return -1;
	}

	/* Gather possible headers */
	fprintf(f, "select current_setting('response.headers', true);");
	fprintf(f, "COMMIT;");
	fclose(f);
	log_debug("Finalizing query...%s", buf);
	PQsendQuery(conn, buf);
	free(buf);
	return 0;

}

int request_try_authorize(request_t* r) {
	const char* auth;
	auth_init(&r->auth);
	log_debug("Try authorize");
	if ( header_find(r->headers, "authorization", &auth))
		 return auth_parse(&r->auth, auth, r->config);
	log_info("Authorization header not found");
	return 0; /* may be not authorized, but still unpriveleged */
}

void request_free(request_t* r) {
	if (r->url) free(r->url);
	if (r->body) free(r->body);
	header_free(r->headers);
	auth_free(&r->auth);
	for ( int i = 0; i< r->nqs; i++ ) {
		if ( r->qnames[i]) free(r->qnames[i]);
		if ( r->qvalues[i]) free(r->qvalues[i]);
	}
	for ( int i = 0; i < r->npath; i++ )
		if (r->path[i]) free(r->path[i]);

}

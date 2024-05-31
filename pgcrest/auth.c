// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <jwt.h>
#include <string.h>
#include <iniparser.h>
#include "auth.h"
#include "logging.h"


int auth_init(auth_t* a) {
	memset(a, 0, sizeof(auth_t));
	return 0;
}

/* Parses Authorization Header */
auth_status auth_parse(auth_t* a, const char* auth, dictionary* config) {
	size_t bearer_sz;
	const char* s = strpbrk(auth, " \t");
	if ( !s ) {
		log_error("Bad header");
		return AUTH_ERR_BADHEADER;
	}
	if ( strncasecmp(auth, "Bearer", s-auth) ) {
		log_warning("Bearer keyword expected but '%.*s' got", s-auth, auth);
		return AUTH_ERR_NOBEARER;
	}
	/* TODO: skip spaces? */
	
	/* PARSING jwt */
	jwt_t* JWT;
	const char* jwt_key = iniparser_getstring(config, "jwt:key", NULL);
	if( jwt_decode(&JWT, s+1, jwt_key, jwt_key?strlen(jwt_key):0) ) {
		log_error("Cannot parse jwt token");
		return AUTH_ERR_BADJWT;
	}
	long exp = jwt_get_grant_int(JWT, "exp");

	if (!exp) {
		log_warning("Expiration exp field must be set");
		return AUTH_ERR_NOEXP;
	}

	if (exp < time(NULL)) {
		log_error("Token expired");
		return AUTH_ERR_EXPIRED;
	}
	if (jwt_get_grant(JWT, "role")) {
		a->role = strdup(jwt_get_grant(JWT, "role"));
		log_debug("A role found: %s", a->role);
	}
	a->claims = jwt_dump_grants_str(JWT, 0);
	a->authorized = 1;	
	jwt_free(JWT);
	return AUTH_OK;
}

void auth_free(auth_t* a) {
	if ( a->role ) free(a->role);
	if ( a->claims) free(a->claims);
}	


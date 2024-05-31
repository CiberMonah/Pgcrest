// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _AUTH_H_
#define _AUTH_H_
#include <iniparser.h>


typedef struct _auth {
	char* role;
	char* claims;
	int authorized;

} auth_t;

typedef enum {
	AUTH_OK = 0,
	AUTH_ERR_BADHEADER,
	AUTH_ERR_NOBEARER,
	AUTH_ERR_BADJWT,
	AUTH_ERR_NOEXP,
	AUTH_ERR_EXPIRED,

} auth_status;

int auth_init(auth_t* auth);

auth_status auth_parse(auth_t* auth, const char* authorization, dictionary* config);

void auth_free(auth_t* auth);

#endif /* _AUTH_H_*/

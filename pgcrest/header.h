// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _HEADER_H_
#define _HEADER_H_
typedef struct _header {
	char* name;
	char* value;
	struct _header* next;
} header_t;



int header_find(header_t* h, const char* field, const char** value);

int header_push(header_t** h, char* name);

int header_set(header_t* h, char* value);


void header_free(header_t* h);

#endif /* _HEADER_H_ */

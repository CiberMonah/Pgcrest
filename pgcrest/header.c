// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include <stdlib.h>
#include <string.h>
#include "header.h"

int header_push(header_t** h, char* name) {
	header_t* n = malloc(sizeof(header_t));
	if ( !n ) return -1;
	*n = (header_t){name, NULL, *h};
	*h = n;
	return 0;
}

int header_set(header_t* h, char* value) {
	if ( !h ) return -1; 
	h->value = value;
	return 0;
}


void header_free(header_t* h) {
	if ( !h ) return;
	header_t* n = h->next;
	free(h->name);
	free(h->value);
	free(h);
	header_free(n);
}

int header_find(header_t* h, const char* name, const char** value) {
	if ( !h) return 0;
	if ( !strcasecmp(name, h->name)) return (*value = h->value, 1);
	return header_find(h->next, name, value);
}	

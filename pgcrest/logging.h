// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#ifndef _LOGGING_H_
#define _LOGGING_H_

#include<stdio.h>

int log_entry(int level, const char* format, ...);


#define log_info(format, ...) log_entry(1, format, ##__VA_ARGS__)
#define log_warning(format, ...) log_entry(2, format, ##__VA_ARGS__)
#define log_error(format, ...) log_entry(3, format, ##__VA_ARGS__)
#define log_debug(format, ...) log_entry(0, format, ##__VA_ARGS__)

#endif

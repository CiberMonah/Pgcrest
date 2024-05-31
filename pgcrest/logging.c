// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static int _log_entry(int level, const char* format, va_list ap) {
	struct timespec ts;
	struct tm tm;
	char buf[1024];
	char outbuf[1024];
	size_t written;
	time_t tx = time(NULL);
	localtime_r(&tx, &tm);
	written = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S ", &tm);
	written += snprintf(buf+written, sizeof(buf)-written, "[%7s] ", level == 0? "DEBUG" : level==1? "INFO" : level==2? "WARNING":"ERROR");
	written += vsnprintf(buf+written, sizeof(buf)-written, format, ap);
	sprintf(buf+written, "\n");
	return fputs(buf, stderr);
}

int log_entry(int level, const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	int res = _log_entry(level, format, ap);
	va_end(ap);
	return res;
}

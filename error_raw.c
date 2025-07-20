#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include "error_raw.h"

void perror_raw(const char *msg)
{
	fprintf(stderr, "%s: %s\n\r", msg, strerror(errno));
}

void perror_raw_die(const char *msg)
{
	perror_raw(msg);
	exit(EXIT_FAILURE);
}

void ferror_raw(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n\r");
	va_end(args);
}

void ferror_raw_die(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n\r");
	va_end(args);
	exit(EXIT_FAILURE);
}

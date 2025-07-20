#ifndef ERROR_RAW_H
#define ERROR_RAW_H

#define NORETURN __attribute__((noreturn))

/* Since we are in *raw* mode, we can't print \n like usual.
 * Replace \n to \n\r in *raw* mode */

void perror_raw(const char *msg);

NORETURN void perror_raw_die(const char *msg);

void ferror_raw(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

NORETURN void ferror_raw_die(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

#endif
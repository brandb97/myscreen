/*
 * copied from git-compat-util.h in git
 *
 * Not interested in make this project compatible with all the
 * platforms (i.e. Windows, BSD, etc.). I am using MacOS now,
 * so I just want to make it compatible with MacOS and Linux.
 */

#ifndef COMPAT_UTIL_H
#define COMPAT_UTIL_H

#include <stdlib.h>
#include <string.h>

#ifndef FLEX_ARRAY
#if defined(__GNUC__) && (__GNUC__ < 3)
#define FLEX_ARRAY 0
#else
#define FLEX_ARRAY /* empty */
#endif
#endif

#define FLEX_ALLOC_MEM(x, flexname, buf, len) do { \
	size_t flex_array_len_ = (len); \
	(x) = calloc(1, sizeof(*(x)) + flex_array_len_ + 1); \
	memcpy((void *)(x)->flexname, (buf), flex_array_len_); \
} while (0)
#define FLEX_ALLOC_STR(x, flexname, str) \
	FLEX_ALLOC_MEM((x), flexname, (str), strlen(str))

#define alloc_nr(x) (((x)+16)*3/2)

#define ALLOC_ARRAY(x, alloc) (x) = malloc(sizeof(*(x)) * (alloc))
#define CALLOC_ARRAY(x, alloc) (x) = calloc((alloc), sizeof(*(x)))
#define REALLOC_ARRAY(x, alloc) (x) = realloc((x), sizeof(*(x)) * (alloc))

#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

#endif

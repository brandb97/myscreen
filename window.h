#ifndef WINDOW_H
#define WINDOW_H

#include <sys/types.h>

struct window {
	char *name; /* Name of the window */
	char *device; /* Device associated with the window
		       * only used for debug */
	char *socket; /* Socket associated with the window */
	pid_t pid; /* Process ID of the window task */
};

struct window_vec {
	struct window **windows;
	size_t nr;
	size_t alloc;
};

/* start a new window task */
struct window *window_xstart(char *name, struct termios *termios,
			     struct winsize *ws, char **argv);
void window_free(struct window *win);

struct window_vec *window_vec_xalloc();
void window_vec_free(struct window_vec *vec);
void window_vec_add(struct window_vec *vec, struct window *win);
struct window *window_vec_get(struct window_vec *vec, size_t idx);
struct window *window_vec_find(struct window_vec *vec, const char *name);
void window_vec_load(struct window_vec *vec, const char *file);
void window_vec_save(struct window_vec *vec, const char *file);

#endif

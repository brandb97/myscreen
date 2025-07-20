#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include "compat_util.h"
#include "error_raw.h"
#include "pty.h"
#include "window.h"
#include "socket.h"

enum { CHAR_MODE = 'c', WINCH_MODE = 'w' };

static void pty_xset_winsize(int fd, char buf[4])
{
	struct winsize ws;
	uint16_t *p = (uint16_t *)buf;

	ws.ws_row = p[0];
	ws.ws_col = p[1];
	/* We don't care about pixels in Linux and MacOs */

	if (ioctl(fd, TIOCSWINSZ, &ws) < 0)
		perror_raw_die("Error setting window size on pty master");
}

/*
 * a window task does two things
 *   - reads from a pty master and writes to its socket.
 *   - reads from its socket and writes to the pty master.
 */
static void do_window_task(struct pty_info *pty_info, char *socket_path,
			   struct termios *termios, struct winsize *ws,
			   char **argv)
{
	int master_fd, socket_fd;

	if (setsid() <= 0)
		perror_raw_die("Error creating new session in window task");

	master_fd = pty_info->master_fd;
	/* Start a socket daemon listen on socket_path */
	socket_fd = socket_server_xstart(socket_path);
	/* Start a child process runs on pty */
	pty_xfork(pty_info, termios, ws, argv);

	/*
	 * This for loop never breaks, this daemon only exit when receive
	 * a SIGKILL signal.
	 */
	for (;;) {
		int cfd, nfds;
		char socket_buf[4];
		char pty_buf[257];
		fd_set read_fds;

		/* Start a connection */
		cfd = socket_server_xaccept(socket_fd);
		nfds = cfd > master_fd ? cfd + 1 : master_fd + 1;

		for (;;) {
			int n;

			FD_ZERO(&read_fds);
			FD_SET(cfd, &read_fds);
			FD_SET(master_fd, &read_fds);
			if (select(nfds, &read_fds, NULL, NULL, NULL) < 0) {
				if (errno == EINTR)
					continue; /* Interrupted by signal,
						     retry select */
				perror_raw(
					"Error in select on socket and pty master");
			}

			if (FD_ISSET(cfd, &read_fds)) {
				/* Read from socket */
				n = read(cfd, socket_buf, 1);
				if (n < 0)
					perror_raw_die(
						"Error reading from socket");
				else if (n == 0) {
					/*
					 * This means `myscreen` detach from
					 * this window, so break and wait for
					 * the next connection
					 */
					close(cfd);
					break;
				}

				switch (socket_buf[0]) {
				case CHAR_MODE:
					if (read(cfd, socket_buf, 1) != 1)
						perror_raw_die(
							"Error reading char from socket");
					if (write(master_fd, socket_buf, 1) !=
					    1)
						perror_raw_die(
							"Error writing char to pty master");
					break;
				case WINCH_MODE:
					if (read(cfd, socket_buf, 4) != 4)
						perror_raw_die(
							"Error reading window size from socket");
					pty_xset_winsize(master_fd, socket_buf);
					break;
				default:
					ferror_raw_die(
						"Unknown command from socket: %c",
						socket_buf[0]);
				}
			}

			if (FD_ISSET(master_fd, &read_fds)) {
				/* Read from pty master */
				n = read(master_fd, pty_buf,
					 sizeof(pty_buf) - 1);
				if (n < 0)
					perror_raw_die(
						"Error reading from pty master");
				else if (n == 0) {
					ferror_raw("PTY closed");
					exit(EXIT_SUCCESS);
				}
				if (write(cfd, pty_buf, n) != n)
					perror_raw_die(
						"Error writing to socket from pty master");
			}
		}
	}
}

/*
 * start a new window task
 *
 * start a new window task in a new process, return task info to parent
 * process by return a struct window *.
 */
struct window *window_xstart(char *name, struct termios *termios,
			     struct winsize *ws, char **argv)
{
	struct window *win;
	struct pty_info *pty_info = NULL;
	char *socket_path = NULL;
	pid_t pid;

	/* Create socket for communication */
	socket_path = socket_path_xcreate();
	/* Open pty device */
	pty_info = pty_info_xalloc();

	pid = fork();
	if ((pid) < 0)
		ferror_raw_die("Error forking process for window task");
	else if (pid > 0) {
		win = (struct window *)calloc(1, sizeof(struct window));
		if (win == NULL)
			ferror_raw_die(
				"Error allocating memory for window struct");
		win->name = strdup(name);
		win->device = strdup(pty_info->slave_name);
		win->socket = socket_path;
		win->pid = pid;
		pty_info_free(pty_info);
		return win;
	}

	/* Window task start here */
	do_window_task(pty_info, socket_path, termios, ws, argv);
	/*
	 * Since do_window_task() never returns, no need to free
	 * socket_path and pty_info here
	 */
	return NULL;
}

void window_free(struct window *win)
{
	if (win == NULL)
		return;
	if (win->name != NULL)
		free(win->name);
	if (win->device != NULL)
		free(win->device);
	if (win->socket != NULL)
		free(win->socket);
	free(win);
}

struct window_vec *window_vec_xalloc()
{
	struct window_vec *vec;

	vec = (struct window_vec *)malloc(sizeof(struct window_vec));
	if (vec == NULL)
		ferror_raw_die("Error allocating memory for window vector");
	vec->windows = NULL;
	vec->nr = 0;
	vec->alloc = 0;

	return vec;
}

void window_vec_free(struct window_vec *vec)
{
	if (vec == NULL)
		return;
	for (size_t i = 0; i < vec->nr; i++) {
		struct window *win = vec->windows[i];
		window_free(win);
	}
}

void window_vec_add(struct window_vec *vec, struct window *win)
{
	assert(win);
	assert(vec);
	ALLOC_GROW(vec->windows, vec->nr + 1, vec->alloc);
	if (vec->windows == NULL)
		ferror_raw_die("Error reallocating memory for window vector");
	vec->windows[vec->nr++] = win;
}

struct window *window_vec_get(struct window_vec *vec, size_t idx)
{
	assert(vec);
	assert(idx < vec->nr);
	return vec->windows[idx];
}

struct window *window_vec_find(struct window_vec *vec, const char *name)
{
	assert(vec);
	for (size_t i = 0; i < vec->nr; i++) {
		assert(vec->windows[i]);
		if (strcmp(vec->windows[i]->name, name) == 0)
			return vec->windows[i];
	}
	return NULL;
}

void window_vec_load(struct window_vec *vec, const char *file)
{
	/* WIP */
	(void)vec; /* Unused parameter */
	(void)file; /* Unused parameter */
}

void window_vec_save(struct window_vec *vec, const char *file)
{
	/* WIP */
	(void)vec; /* Unused parameter */
	(void)file; /* Unused parameter */
}

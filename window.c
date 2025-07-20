#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
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
		if (win->name == NULL || win->device == NULL ||
		    win->socket == NULL)
			ferror_raw_die("Error allocating memory for window");
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

void window_vec_remove(struct window_vec *vec, struct window *win)
{
	assert(win);
	assert(vec);
	for (size_t i = 0; i < vec->nr; i++) {
		if (vec->windows[i] == win) {
			/* Found the window, remove it */
			window_free(win);
			vec->nr--;
			if (i < vec->nr)
				vec->windows[i] = vec->windows[vec->nr];
			vec->windows[vec->nr] = NULL;
			return;
		}
	}

	/* Should never reach here */
	ferror_raw_die("Error removing window: window not found in vector");
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
	if (idx < vec->nr)
		return vec->windows[idx];
	return NULL;
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

static char *read_line(int fd)
{
	char **line_vec = NULL;
	int nr = 0, alloc = 0;
	char *line = NULL, *line_end;
	size_t line_len;
#define BUFSZ 256
	static char buf[BUFSZ];
	static int buf_len = 0;

	/* See if we can get a line from buf first */
	if (buf_len > 0) {
		line_end = memchr(buf, '\n', buf_len);
		if (line_end) {
			/* Found a line */
			size_t len = line_end - buf + 1; /* include '\n' */
			line = (char *)calloc(1, len + 1);
			memcpy(line, buf, len);

			/* remove line from buffer */
			buf_len -= len;
			memmove(buf, line_end + 1, buf_len);
			return line;
		}
	}

	/*
	 * Read a line from fd, the line is split into multiple
	 * BUFSZ sized chunks, and stored in line_vec.
	 */
	for (;;) {
		int n;

		if ((n = read(fd, buf + buf_len, BUFSZ - buf_len)) < 0) {
			fprintf(stderr, "Error reading from file descriptor\n");
			goto cleanup;
		}

		buf_len += n;
		line_end = memchr(buf, '\n', buf_len);
		if (!line_end) {
			if (buf_len != BUFSZ) {
				line = (char *)calloc(1, buf_len + 2);
				if (line == NULL) {
					fprintf(stderr,
						"Error allocating memory for line\n");
					goto cleanup;
				}
				memcpy(line, buf, buf_len);
				line[buf_len] = '\n';
				line[buf_len + 1] = '\0';
				ALLOC_GROW(line_vec, nr + 1, alloc);
				line_vec[nr++] = line;
				buf_len = 0; /* reset buffer */
				break; /* no more data to read */
			} else {
				line = (char *)calloc(1, BUFSZ);
				if (line == NULL) {
					fprintf(stderr,
						"Error allocating memory for line\n");
					goto cleanup;
				}
				memcpy(line, buf, BUFSZ);
				ALLOC_GROW(line_vec, nr + 1, alloc);
				line_vec[nr++] = line;
				buf_len = 0; /* reset buffer */
			}
		} else {
			/* Found a line */
			size_t len = line_end - buf + 1; /* include '\n' */
			line = (char *)calloc(1, len + 1);
			if (line == NULL) {
				fprintf(stderr,
					"Error allocating memory for line\n");
				goto cleanup;
			}

			memcpy(line, buf, len);

			/* remove line from buffer */
			buf_len -= len;
			memmove(buf, line_end + 1, buf_len);
			ALLOC_GROW(line_vec, nr + 1, alloc);
			line_vec[nr++] = line;
			break;
		}
	}

	/*
	 * Now we finished reading a line, concatenate the lines in
	 * line_vec into a single line.
	 */
	line_len = 0;
	for (int i = 0; i < nr; i++) {
		assert(line_vec[i]);
		if (i != nr - 1)
			line_len += BUFSZ;
		else
			/* 1 for trailing "\0" */
			line_len += strlen(line_vec[i]) + 1;
	}
	line = (char *)calloc(1, line_len);
	if (line == NULL) {
		fprintf(stderr, "Error allocating memory for line\n");
		goto cleanup;
	}
	for (int i = 0; i < nr; i++) {
		char *start = line;
		start += i * BUFSZ;
		if (i != nr - 1)
			memcpy(start, line_vec[i], BUFSZ);
		else {
			size_t len = line_len;
			len -= (size_t)i * BUFSZ;
			memcpy(start, line_vec[i], len - 1);
			start[len - 1] = '\0';
		}
	}
cleanup:
	for (int i = 0; i < nr; i++)
		free(line_vec[i]);
	free(line_vec);
	if (line && *line == '\n') {
		free(line);
		return NULL;
	}
	return line;
}

void window_vec_load(struct window_vec *vec, const char *file)
{
	char *line = NULL;
	int fd;
	/*
	 * when loading window_vec, we still in the normal mode. So no need
	 * to use ferror_raw() or perror_raw() here.
	 */

	assert(vec);
	fd = open(file, O_RDONLY | O_CREAT, 0644);
	if (fd < 0)
		fprintf(stderr, "Error opening myscreen file: %s\n", file);
	while ((line = read_line(fd))) {
		char *name, *device, *socket, *start, *end;
		pid_t pid;
		int n;
		struct window *win;

		start = line;
		end = strchr(start, ' ');
		if (end == NULL) {
			fprintf(stderr, "Error parsing myscreen file: %s\n",
				file);
			goto cleanup;
		}
		*end = '\0';
		name = start;

		start = end + 1;
		end = strchr(start, ' ');
		if (end == NULL) {
			fprintf(stderr, "Error parsing myscreen file: %s\n",
				file);
			goto cleanup;
		}
		*end = '\0';
		device = start;

		start = end + 1;
		end = strchr(start, ' ');
		if (end == NULL) {
			fprintf(stderr, "Error parsing myscreen file: %s\n",
				file);
			goto cleanup;
		}
		*end = '\0';
		socket = start;

		start = end + 1;
		n = sscanf(start, "%d\n", &pid);
		if (n != 1) {
			fprintf(stderr, "Error parsing myscreen file: %s\n",
				file);
			goto cleanup;
		}

		win = (struct window *)malloc(sizeof(struct window));
		if (win == NULL) {
			fprintf(stderr,
				"Error allocating memory for window struct\n");
			goto cleanup;
		}
		win->name = strdup(name);
		win->device = strdup(device);
		win->socket = strdup(socket);
		win->pid = pid;
		if (win->name == NULL || win->device == NULL ||
		    win->socket == NULL) {
			fprintf(stderr, "Error allocating memory for window\n");
			window_free(win);
			goto cleanup;
		}
		window_vec_add(vec, win);

		free(line);
		line = NULL;
	}

cleanup:
	free(line);
	close(fd);
}

void window_vec_save(struct window_vec *vec, const char *file)
{
	/* we are definitely in raw mode here */
	int fd;

	assert(vec);
	fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		ferror_raw("Error opening myscreen file for writing: %s", file);
		return;
	}

	for (size_t i = 0; i < vec->nr; i++) {
		struct window *win = vec->windows[i];
		char *buf;
		size_t len;
		int n;

		assert(win);
		assert(win->name && win->device && win->socket);
		len = strlen(win->name) + 1 + strlen(win->device) + 1 +
		      strlen(win->socket) + 1 + 10 +
		      2; /* 10 for pid, 1 for \n, 1 for trailing '\0' */
		buf = (char *)calloc(1, len);
		if (!buf) {
			ferror_raw("Error allocating memory for window string");
			goto cleanup;
		}
		n = snprintf(buf, len, "%s %s %s %d\n", win->name, win->device,
			     win->socket, win->pid);
		/* snprintf must succeed */
		assert(n > 0 && (size_t)n < len);
		if (write(fd, buf, n) != n) {
			ferror_raw("Error writing window to myscreen file: %s",
				   file);
			free(buf);
			goto cleanup;
		}
	}

cleanup:
	close(fd);
}

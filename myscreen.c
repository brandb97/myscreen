#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include "socket.h"
#include "tty.h"
#include "window.h"
#include "error_raw.h"

/* Default screen store is .myscreen in $HOME directory */
#define DEFAULT_SCREEN_STORE ".myscreen"

#define CTRL_A 1

static char screen_store[256];

static int window_ch = 0;

static struct termios origin_termios;
static int raw_mode = 0;

static void usage()
{
	/* Here we are not in the *raw* mode */
	fprintf(stderr, "myscreen: simple window manager\n");
	fprintf(stderr, "myscreen -l|--list\n");
	fprintf(stderr, "myscreen -a|--attach winspec\n");
	fprintf(stderr, "myscreen [cmd [arg0...]]\n");
	exit(EXIT_FAILURE);
}

enum { LIST, ATTACH, START } mode;

enum { DETACH = 'd', KILL = 'k' } control_char;

/* return 0 if we want to retain this window task, -1 if we want to
 * we want to discard this window */
static int do_interact_window(struct window *win);

static void sigwinch_handler(int sig);

static void reset_tty(int sig)
{
	if (!raw_mode)
		return;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &origin_termios) < 0)
		perror_raw_die("Error resetting terminal attributes");
	if (sig == 0)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	char *home;
	char *arg;
	int home_len;
	int task_ret;
	struct window_vec *windows;

	mode = START;
	argc--;
	argv++;
	while (argc > 0) {
		arg = *argv;
		if (!arg || *arg != '-')
			break;
		if (!strcmp(arg, "-l") || !strcmp(arg, "--list")) {
			argc--;
			argv++;
			mode = LIST;
			break;
		}
		if (!strcmp(arg, "-a") || !strcmp(arg, "--attach")) {
			argc--;
			argv++;
			mode = ATTACH;
			break;
		}
		usage();
	}

	home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "HOME environment variable not set\n");
		exit(EXIT_FAILURE);
	}
	home_len = strlen(home);
	if (home_len == 0) {
		fprintf(stderr, "HOME environment variable is empty\n");
		exit(EXIT_FAILURE);
	}
	if (home[home_len - 1] == '/')
		home[home_len - 1] = '\0';

	if (snprintf(screen_store, 256, "%s/%s", home, DEFAULT_SCREEN_STORE) >=
	    256) {
		fprintf(stderr, "Error: screen store path too long\n");
		exit(EXIT_FAILURE);
	}
	signal(SIGWINCH, sigwinch_handler);
	signal(SIGABRT, reset_tty);
	signal(SIGKILL, reset_tty);
	signal(SIGINT, reset_tty);
	signal(SIGTERM, reset_tty);

	windows = window_vec_xalloc();
	window_vec_load(windows, screen_store);
	if (mode == START) {
		struct winsize ws;
		struct window *win;
		char window_name[32] = { 0 };

		tty_set_raw(STDIN_FILENO, &origin_termios);
		raw_mode = 1;
		tty_get_winsize(STDIN_FILENO, &ws);
		snprintf(window_name, 32, "myscreen.%u",
			 (unsigned int)windows->nr);
		win = window_xstart(window_name, &origin_termios, &ws, argv);

		task_ret = do_interact_window(win);
		if (task_ret == 0)
			window_vec_add(windows, win);
		else
			/* Failed or killed */
			window_free(win);
	} else
		fprintf(stderr, "unimplemented --list and --attach options");
	window_vec_save(windows, screen_store);
	window_vec_free(windows);

	reset_tty(0);
}

#define FAIL(p)               \
	do {                  \
		(p);          \
		ret = -1;     \
		goto cleanup; \
	} while (0)

static int do_interact_window(struct window *win)
{
	int sock_fd, nfds;
	char sock_buf[256];
	fd_set read_set;
	int ret;

	sock_fd = socket_client_xstart(win->socket);
	nfds = sock_fd > STDIN_FILENO ? sock_fd + 1 : STDIN_FILENO + 1;
	for (;;) {
		char c;

		if (window_ch) {
			struct winsize ws;
			char winch_buf[5] = { [0] = 'w' };
			uint16_t *p = (uint16_t *)(winch_buf + 1);

			tty_get_winsize(STDIN_FILENO, &ws);
			window_ch = 0;
			p[0] = ws.ws_row;
			p[1] = ws.ws_col;
			if (write(sock_fd, winch_buf, 5) != 5)
				FAIL(perror_raw(
					"Error sending window change to socket"));
		}

		FD_ZERO(&read_set);
		FD_SET(STDIN_FILENO, &read_set);
		FD_SET(sock_fd, &read_set);
		if (select(nfds, &read_set, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue; /* Interrupted by signal, retry select
					   */
			FAIL(perror_raw(
				"Error in select from STDIN and socket"));
		}

		if (FD_ISSET(sock_fd, &read_set)) {
			ssize_t n = read(sock_fd, sock_buf, sizeof(sock_buf));
			if (n < 0)
				FAIL(perror_raw("Error reading from socket"));
			else if (n == 0)
				FAIL(perror_raw("Socket closed by server"));
			if (write(STDOUT_FILENO, sock_buf, n) != n)
				FAIL(perror_raw("Error writing to STDOUT"));
		} else if (FD_ISSET(STDIN_FILENO, &read_set)) {
			if (read(STDIN_FILENO, &c, 1) != 1)
				FAIL(perror_raw(
					"Error reading char from STDIN"));
			if (c != CTRL_A) {
				char char_buf[2] = { 'c', c };
				if (write(sock_fd, char_buf, 2) != 2)
					FAIL(perror_raw(
						"Error sending char to socket"));
				continue;
			}

			/*
			 * c is CTRL-A, if the next char is 'd' or 'k', we
			 * detach or kill the window. Otherwise we ignore the
			 * next character.
			 *
			 * NEEDSWORK: we should use select here to wait for the
			 * next character, but for now we just read it directly
			 * from stdin.
			 */
			if (read(STDIN_FILENO, &c, 1) != 1)
				FAIL(perror_raw(
					"Error reading char from STDIN after CTRL-A"));
			switch (c) {
			case DETACH:
				ret = 0;
                ferror_raw("Detach from window %s: pid %d",
                           win->name, win->pid);
				goto cleanup;
			case KILL:
				kill(win->pid, SIGKILL);
				ret = -1;
                ferror_raw("Kill window %s: pid %d",
                           win->name, win->pid);
				goto cleanup;
			default:
				/* ignore unknown char */
				break;
			}
		}
	}

cleanup:
	close(sock_fd);
	return ret;
}

static void sigwinch_handler(int sig)
{
	assert(sig == SIGWINCH);
	window_ch = 1;
}

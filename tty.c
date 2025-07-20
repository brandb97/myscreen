/* copied from [tlpi](https://man7.org/tlpi/index.html) */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <termios.h>
#include <unistd.h>
#include "tty.h"
#include "error_raw.h"

int tty_set_raw(int fd, struct termios *prev)
{
	struct termios t;
	char *t_name;

	if (!(t_name = ttyname(fd))) {
		fprintf(stderr, "Error getting terminal name\n");
		return -1;
	}

	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "Error getting terminal %s attributes\n",
			t_name);
		return -1;
	}

	if (prev != NULL)
		*prev = t; /* Save current settings if requested */

	/*
	 * Noncanonical mode, disable signals, extended input processing,
	 * and echoing.
	 */
	t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);

	/*
	 * Disable special handling of CR, NL, and BREAK. No 8th-bit
	 * stripping or parity error checking. Disable START/STOP output
	 * flow control.
	 */
	t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR | INPCK |
		       ISTRIP | IXON | PARMRK);

	t.c_oflag &= ~(OPOST);

	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSAFLUSH, &t) < 0) {
		fprintf(stderr, "Error setting terminal %s to raw mode\n",
			t_name);
		return -1;
	}
	return 0;
}

void tty_get_winsize(int fd, struct winsize *ws)
{
	assert(ws != NULL);
	if (ioctl(fd, TIOCGWINSZ, ws) < 0)
		perror_raw_die("Error getting window size: %s");
}

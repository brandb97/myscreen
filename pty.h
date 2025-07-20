#ifndef PTY_H
#define PTY_H

#include <stddef.h> /* for size_t */
#include <sys/types.h> /* for pid_t */
#include <termios.h> /* for struct termios */
#include <sys/ioctl.h> /* for struct winsize */
#include "compat_util.h" /* for FLEX_ARRAY */

struct pty_info {
	int master_fd;
	char slave_name[FLEX_ARRAY];
};

struct pty_info *pty_info_xalloc();
void pty_info_free(struct pty_info *info);
pid_t pty_xfork(struct pty_info *info, struct termios *termios,
		struct winsize *ws, char **argv);

#endif

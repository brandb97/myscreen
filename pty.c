/* copied from [tlpi](https://man7.org/tlpi/index.html) */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <unistd.h>
#include "compat_util.h"
#include "pty.h"

struct pty_info *pty_info_xalloc()
{
	struct pty_info *info;
	int master_fd;
	char *slave_name;

	master_fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (master_fd == -1) {
		perror("Error opening master PTY");
		exit(EXIT_FAILURE);
	}

	if (grantpt(master_fd) == -1) {
		perror("Error granting PTY access");
		exit(EXIT_FAILURE);
	}

	if (unlockpt(master_fd) == -1) {
		perror("Error unlocking PTY");
		exit(EXIT_FAILURE);
	}

	slave_name = ptsname(master_fd);
	if (slave_name == NULL) {
		perror("Error getting slave PTY name");
		exit(EXIT_FAILURE);
	}

	FLEX_ALLOC_STR(info, slave_name, slave_name);
	if (info == NULL) {
		perror("Error allocating memory for PTY info");
		exit(EXIT_FAILURE);
	}
	info->master_fd = master_fd;
	return info;
}

void pty_info_free(struct pty_info *info)
{
	if (info == NULL)
		return;
	close(info->master_fd);
	free(info);
}

pid_t pty_xexec(struct pty_info *info, struct termios *termios,
		struct winsize *ws, char **argv)
{
	pid_t pid;
	int slave_fd;

	pid = fork();
	if (pid == -1) {
		perror("Error forking process");
		exit(EXIT_FAILURE);
	}

	if (pid > 0)
		return pid; /* Parent process returns child's PID */

	/* Child process */
	if (setsid() == -1) {
		perror("Error creating new session");
		exit(EXIT_FAILURE);
	}

	slave_fd = open(info->slave_name, O_RDWR);
	if (slave_fd == -1) {
		fprintf(stderr, "Error opening slave PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}

	assert(termios != NULL);
	assert(ws != NULL);
	/* Set terminal attributes */
	if (tcsetattr(slave_fd, TCSANOW, termios) == -1) {
		fprintf(stderr,
			"Error setting terminal attributes for PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* Set window size */
	if (ioctl(slave_fd, TIOCSWINSZ, ws) == -1) {
		fprintf(stderr, "Error setting window size for PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Redirect standard input/output/error to the PTY */
	if (dup2(slave_fd, STDIN_FILENO) != STDIN_FILENO) {
		fprintf(stderr, "Error redirecting stdin to PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (dup2(slave_fd, STDOUT_FILENO) != STDOUT_FILENO) {
		fprintf(stderr, "Error redirecting stdout to PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (dup2(slave_fd, STDERR_FILENO) != STDERR_FILENO) {
		fprintf(stderr, "Error redirecting stderr to PTY '%s': %s\n",
			info->slave_name, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Close the master PTY */
	close(info->master_fd);

	/* Execute the command */
	if (!argv || !*argv) {
		execlp("bash", NULL);
		perror("Error executing bash");
		exit(EXIT_FAILURE);
	}

	execvp(*argv, argv);

	fprintf(stderr, "Error executing command '%s", *argv++);
	while (*argv) {
		fprintf(stderr, " %s", *argv);
		argv++;
	}
	fprintf(stderr, "': %s\n", strerror(errno));
	exit(EXIT_FAILURE);
}

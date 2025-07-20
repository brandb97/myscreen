#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include "socket.h"
#include "error_raw.h"

#define SOCKET_BASE	"/tmp/myscreen"
#define SOCKET_BASE_LEN 12

/* we believe all function here will be called in *raw* mode */

char *socket_path_xcreate()
{
	/* pid_t is uint32_t, and log_10(2^32) < 10 */
	char *path = (char *)calloc(1, SOCKET_BASE_LEN + 11);
	if (path == NULL)
		perror_raw_die("Error allocating memory for socket path");

	snprintf(path, SOCKET_BASE_LEN + 11, "%s.%d", SOCKET_BASE, getpid());
	return path;
}

int socket_server_xstart(const char *path)
{
	int sockfd;
	struct sockaddr_un addr;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0)
		perror_raw_die("Error socket() failed");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	unlink(path); /* Remove any exist socket file */

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		perror_raw_die("Error bind() failed");

	if (listen(sockfd, 5) < 0)
		perror_raw_die("Error listen() failed");

	return sockfd;
}

int socket_server_xaccept(int sockfd)
{
	int fd;

	fd = accept(sockfd, NULL, NULL);
	if (fd < 0)
		perror_raw_die("Error accept() failed");

	return fd;
}

int socket_client_start(const char *path)
{
	int sockfd;
	struct sockaddr_un addr;
	fd_set write_set;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror_raw("Error socket() failed");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (access(path, F_OK) == -1)
		sleep(1);
	while (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errno != EINPROGRESS) {
			perror_raw("Error connect() failed");
			return -1;
		}

		/* Wait for the socket to be writable and retry */
		FD_ZERO(&write_set);
		FD_SET(sockfd, &write_set);
		if (select(sockfd + 1, NULL, &write_set, NULL, NULL) < 0) {
			perror_raw("Error waiting for connection");
			return -1;
		}
	}

	return sockfd;
}

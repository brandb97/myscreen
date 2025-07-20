#ifndef SOCKET_H
#define SOCKET_H

char *socket_path_xcreate();

int socket_server_xstart(const char *path);

int socket_server_xaccept(int sockfd);

int socket_client_start(const char *path);

#endif
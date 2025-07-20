#ifndef TTY_H
#define TTY_H

#include <termios.h>   /* for struct termios */
#include <sys/ioctl.h> /* for struct winsize */

/* Place terminal referred to by fd into raw mode. */
int tty_set_raw(int fd, struct termios *prev);

/* Only called in *raw* mode */
void tty_get_winsize(int fd, struct winsize *ws);

#endif

# Makefile for myscreen project

# Compiler
CC = clang

# Compiler flags
CFLAGS = -Wall -Wextra -g -fsanitize=address -O0

# Source files
SRCS = myscreen.c pty.c tty.c window.c socket.c error_raw.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = myscreen

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

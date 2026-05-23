#ifndef FIT_H
#define FIT_H

/* Feature test macros for POSIX and GNU extensions */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

/* Maximum path length */
#define MAX_PATH 4096
#define MAX_CONFIG_LINE 1024

/* Error codes */
#define FIT_OK 0
#define FIT_ERROR -1

#endif /* FIT_H */

/* Header file for helper.c so it can be used as a dependency in Makefile. */

#include <stdio.h>
#define MAXBUFFER 1024

FILE *open_file_in_dir(char *filename, char *dirname);
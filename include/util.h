#ifndef UTIL_H
#define UTIL_H

#include "common.h"

void *xmalloc(size_t size);
void log_debug(const char *fmt, ...);
int random_int(int min, int max);

#endif

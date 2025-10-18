#include "util.h"
#include <stdarg.h>
#include <stdio.h>

// Envuelve malloc verificando el resultado para evitar nulos silenciosos.
void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

// Registra mensajes de depuración.
void log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

// Devuelve un entero aleatorio inclusivo entre min y max.
int random_int(int min, int max) {
    return min + rand() % (max - min + 1);
}

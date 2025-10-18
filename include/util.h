#ifndef UTIL_H
#define UTIL_H

#include "common.h"

// Reserva memoria y termina el programa si la asignación falla.
void *xmalloc(size_t size);
// Imprime mensajes de depuración formateados en la salida estándar.
void log_debug(const char *fmt, ...);
// Genera un entero aleatorio dentro del rango [min, max].
int random_int(int min, int max);

#endif

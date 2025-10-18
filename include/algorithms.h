#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "sim_types.h"

// Inicializa el estado interno del algoritmo de reemplazo para este simulador.
void algorithms_init(Simulator *sim);
// Limpia el estado del algoritmo para iniciar una nueva simulación.
void algorithms_reset(Simulator *sim);
// Libera la memoria asociada al algoritmo configurado.
void algorithms_free(Simulator *sim);

// Registra que una página fue cargada en RAM para actualizar el algoritmo.
void algorithms_on_page_loaded(Simulator *sim, Page *page);
// Informa que una página fue expulsada para que el algoritmo actualice su estado.
void algorithms_on_page_evicted(Simulator *sim, Page *page);
// Notifica un acceso a página para actualizar contadores y pistas del algoritmo.
void algorithms_on_page_accessed(Simulator *sim, Page *page);

// Elige el identificador de la página víctima según la política activa.
sim_pageid_t choose_victim(Simulator *sim);

#endif

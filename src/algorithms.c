#include "algorithms.h"
#include "util.h"

#include <limits.h>
#include <string.h>

typedef struct {
	sim_pageid_t *data;
	size_t capacity;
	size_t head;
	size_t tail;
	size_t count;
} PageQueue;

typedef struct {
	PageQueue fifo_queue;
	int clock_hand;
} AlgorithmState;

// Devuelve un puntero al estado interno del algoritmo para el simulador.
static AlgorithmState *get_state(Simulator *sim) {
	return (AlgorithmState *)sim->alg_state;
}

// Reserva memoria para estructuras del algoritmo y detiene el programa si falla.
static void *alg_realloc(void *ptr, size_t size) {
	void *tmp = realloc(ptr, size);
	if (!tmp && size != 0) {
		fprintf(stderr, "Out of memory (alg_realloc)\n");
		exit(EXIT_FAILURE);
	}
	return tmp;
}

// Busca una página por id en la tabla global del simulador.
static Page *get_page(const Simulator *sim, sim_pageid_t id) {
	if (id == 0 || id >= sim->mmu.pages_capacity) {
		return NULL;
	}
	return sim->mmu.pages[id];
}

// Asegura que la cola pueda almacenar al menos la capacidad solicitada.
static void queue_reserve(PageQueue *queue, size_t min_capacity) {
	if (queue->capacity >= min_capacity) {
		return;
	}
	size_t old_capacity = queue->capacity;
	size_t new_capacity = old_capacity ? old_capacity * 2 : 32;
	while (new_capacity < min_capacity) {
		new_capacity *= 2;
	}

	sim_pageid_t *new_data = alg_realloc(NULL, new_capacity * sizeof(sim_pageid_t));
	if (queue->count > 0 && queue->data && old_capacity > 0) {
		for (size_t i = 0; i < queue->count; ++i) {
			size_t idx = (queue->head + i) % old_capacity;
			new_data[i] = queue->data[idx];
		}
	}
	free(queue->data);
	queue->data = new_data;
	queue->capacity = new_capacity;
	if (queue->count > 0) {
		queue->head = 0;
		queue->tail = queue->count - 1;
	} else {
		queue->head = queue->tail = 0;
	}
}

// Inserta un nuevo identificador de página al final de la cola FIFO.
static void queue_push(PageQueue *queue, sim_pageid_t value) {
	queue_reserve(queue, queue->count + 1);
	if (queue->capacity == 0) {
		return;
	}
	if (queue->count == 0) {
		queue->head = queue->tail = 0;
		queue->data[queue->tail] = value;
		queue->count = 1;
		return;
	}

	queue->tail = (queue->tail + 1) % queue->capacity;
	queue->data[queue->tail] = value;
	queue->count++;
}

// Extrae el elemento del frente de la cola si existe.
static void queue_pop_front(PageQueue *queue) {
	if (queue->count == 0) {
		return;
	}
	if (queue->count == 1) {
		queue->head = queue->tail = 0;
		queue->count = 0;
		return;
	}
	queue->head = (queue->head + 1) % queue->capacity;
	queue->count--;
}

// Consulta el valor en el frente sin retirarlo.
static sim_pageid_t queue_peek_front(const PageQueue *queue) {
	if (queue->count == 0) {
		return 0;
	}
	return queue->data[queue->head];
}

// Limpia la cola para dejarla vacía.
static void queue_clear(PageQueue *queue) {
	queue->count = 0;
	queue->head = queue->tail = 0;
}

// Genera un número pseudoaleatorio y actualiza la semilla del simulador.
static unsigned int sim_rand(Simulator *sim) {
	if (sim->rng_seed == 0) {
		sim->rng_seed = 1;
	}
	sim->rng_seed = sim->rng_seed * 1103515245u + 12345u;
	return (sim->rng_seed / 65536u) % 32768u;
}

// Obtiene la próxima referencia futura registrada para una página.
static size_t opt_next_use_index(const Page *page) {
	if (page->future_uses.cursor < page->future_uses.count) {
		return page->future_uses.positions[page->future_uses.cursor];
	}
	return SIZE_MAX;
}

// Avanza el cursor de usos futuros luego de que la página fue accedida.
static void opt_advance_future_use(Page *page) {
	if (page->future_uses.cursor < page->future_uses.count) {
		page->future_uses.cursor++;
	}
	page->next_use_pos = opt_next_use_index(page);
}

// Refresca el valor cacheado del próximo uso al actualizar la cola OPT.
static void opt_refresh_next_use(Page *page) {
	page->next_use_pos = opt_next_use_index(page);
}

// Selecciona la siguiente página víctima usando la política FIFO.
static sim_pageid_t fifo_choose(Simulator *sim, AlgorithmState *state) {
	if (!state) {
		return 0;
	}
	while (state->fifo_queue.count > 0) {
		sim_pageid_t candidate = queue_peek_front(&state->fifo_queue);
		Page *page = get_page(sim, candidate);
		if (page && page->in_ram) {
			queue_pop_front(&state->fifo_queue);
			return candidate;
		}
		queue_pop_front(&state->fifo_queue);
	}
	return 0;
}

// Implementa la política de segunda oportunidad recorriendo el reloj.
static sim_pageid_t sc_choose(Simulator *sim, AlgorithmState *state) {
	if (!state) {
		return 0;
	}
	int scanned = 0;
	int frames = RAM_FRAMES;
	if (state->clock_hand < 0 || state->clock_hand >= frames) {
		state->clock_hand = 0;
	}

	while (scanned < frames) {
		Frame *frame = &sim->mmu.frames[state->clock_hand];
		if (frame->occupied) {
			Page *page = get_page(sim, frame->page_id);
			if (page) {
				if (page->ref_bit == 0) {
					sim_pageid_t victim = frame->page_id;
					state->clock_hand = (state->clock_hand + 1) % frames;
					return victim;
				}
				page->ref_bit = 0;
			}
		}
		state->clock_hand = (state->clock_hand + 1) % frames;
		scanned++;
	}

	for (int i = 0; i < frames; ++i) {
		Frame *frame = &sim->mmu.frames[i];
		if (frame->occupied) {
			state->clock_hand = (i + 1) % frames;
			return frame->page_id;
		}
	}

	return 0;
}

// Devuelve la página más recientemente usada para el algoritmo MRU.
static sim_pageid_t mru_choose(Simulator *sim) {
	sim_pageid_t candidate = 0;
	sim_time_t best_time = 0;
	for (int i = 0; i < RAM_FRAMES; ++i) {
		Frame *frame = &sim->mmu.frames[i];
		if (!frame->occupied) {
			continue;
		}
		Page *page = get_page(sim, frame->page_id);
		if (!page) {
			continue;
		}
		if (candidate == 0 || page->last_used >= best_time) {
			candidate = page->id;
			best_time = page->last_used;
		}
	}
	return candidate;
}

// Elige una página víctima al azar entre los marcos ocupados.
static sim_pageid_t rnd_choose(Simulator *sim) {
	sim_pageid_t buffer[RAM_FRAMES];
	size_t count = 0;
	for (int i = 0; i < RAM_FRAMES; ++i) {
		Frame *frame = &sim->mmu.frames[i];
		if (frame->occupied) {
			buffer[count++] = frame->page_id;
		}
	}
	if (count == 0) {
		return 0;
	}
	unsigned int r = sim_rand(sim) % count;
	return buffer[r];
}

// Busca la página con uso más lejano según la política OPT.
static sim_pageid_t opt_choose(Simulator *sim) {
	sim_pageid_t best_page = 0;
	size_t farthest_use = 0;

	for (int i = 0; i < RAM_FRAMES; ++i) {
		Frame *frame = &sim->mmu.frames[i];
		if (!frame->occupied) {
			continue;
		}
		Page *page = get_page(sim, frame->page_id);
		if (!page) {
			continue;
		}

		size_t next_use = opt_next_use_index(page);
		if (next_use == SIZE_MAX) {
			return page->id;
		}
		if (best_page == 0 || next_use > farthest_use) {
			farthest_use = next_use;
			best_page = page->id;
		}
	}

	return best_page;
}

// Reserva y prepara el estado común de todos los algoritmos de reemplazo.
void algorithms_init(Simulator *sim) {
	if (!sim) {
		return;
	}
	if (sim->alg_state) {
		algorithms_reset(sim);
		return;
	}
	AlgorithmState *state = xmalloc(sizeof(*state));
	memset(state, 0, sizeof(*state));
	state->clock_hand = 0;
	sim->alg_state = state;
}

// Reinicia estructuras internas sin liberar memoria persistente.
void algorithms_reset(Simulator *sim) {
	if (!sim || !sim->alg_state) {
		return;
	}
	AlgorithmState *state = get_state(sim);
	queue_clear(&state->fifo_queue);
	state->clock_hand = 0;
}

// Libera completamente la memoria usada por el estado del algoritmo.
void algorithms_free(Simulator *sim) {
	if (!sim || !sim->alg_state) {
		return;
	}
	AlgorithmState *state = get_state(sim);
	free(state->fifo_queue.data);
	free(state);
	sim->alg_state = NULL;
}

// Actualiza la política elegida cuando una página se carga en RAM.
void algorithms_on_page_loaded(Simulator *sim, Page *page) {
	if (!sim || !page) {
		return;
	}
	AlgorithmState *state = get_state(sim);
	if (!state) {
		return;
	}
	switch (sim->algorithm) {
		case ALG_FIFO:
			queue_push(&state->fifo_queue, page->id);
			break;
		case ALG_OPT:
			opt_refresh_next_use(page);
			break;
		case ALG_SC:
		case ALG_MRU:
		case ALG_RND:
		default:
			if (sim->algorithm == ALG_OPT) {
				opt_refresh_next_use(page);
			}
			break;
	}
}

// Notifica que una página dejó la memoria física para sincronizar el algoritmo.
void algorithms_on_page_evicted(Simulator *sim, Page *page) {
	if (!sim || !page) {
		return;
	}
	if (sim->algorithm == ALG_OPT) {
		opt_refresh_next_use(page);
	}
}

// Marca el acceso a página para que cada política ajuste sus indicadores.
void algorithms_on_page_accessed(Simulator *sim, Page *page) {
	if (!sim || !page) {
		return;
	}
	if (sim->algorithm == ALG_OPT) {
		opt_advance_future_use(page);
	}
}

// Punto de entrada que invoca la política de reemplazo configurada.
sim_pageid_t choose_victim(Simulator *sim) {
	if (!sim) {
		return 0;
	}
	AlgorithmState *state = get_state(sim);

	switch (sim->algorithm) {
		case ALG_FIFO:
			return fifo_choose(sim, state);
		case ALG_SC:
			return sc_choose(sim, state);
		case ALG_MRU:
			return mru_choose(sim);
		case ALG_RND:
			return rnd_choose(sim);
		case ALG_OPT:
			return opt_choose(sim);
		default:
			return 0;
	}
}

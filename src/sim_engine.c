#include "sim_engine.h"
#include "algorithms.h"
#include "util.h"

#include <limits.h>
#include <string.h>

#define PROCESS_TABLE_INITIAL   16
#define PTR_TABLE_INITIAL       64
#define PAGE_TABLE_INITIAL      128

typedef struct {
    Process *process;
    PtrMap *ptr;
} PtrLookupResult;

// Variante segura de realloc para estructuras internas del simulador.
static void *sim_realloc(void *ptr, size_t size) {
    void *tmp = realloc(ptr, size);
    if (!tmp && size != 0) {
        fprintf(stderr, "Out of memory (sim_realloc)\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

// Suma el costo temporal de un acceso que encuentra la página en RAM.
static inline void record_page_hit(Simulator *sim) {
    if (!sim) {
        return;
    }
    sim->clock += 1;
    sim->stats.page_hits++;
}

// Suma el costo temporal de un acceso que requiere swap in y contabiliza thrashing.
static inline void record_page_fault(Simulator *sim, int account_thrashing) {
    if (!sim) {
        return;
    }
    sim->clock += 5;
    sim->stats.page_faults++;
    if (account_thrashing) {
        sim->thrashing_time += 5;
    }
}

// Marca todos los marcos como libres y rellena la pila de disponibles.
static void mmu_initialize_frames(MMU *mmu) {
    mmu->free_count = 0;
    for (int i = 0; i < RAM_FRAMES; ++i) {
        mmu->frames[i].occupied = 0;
        mmu->frames[i].page_id = 0;
        mmu->free_frames[mmu->free_count++] = i;
    }
}

// Amplía la tabla de páginas para aceptar el identificador solicitado.
static void mmu_ensure_page_capacity(MMU *mmu, sim_pageid_t id) {
    size_t needed = (size_t)id + 1;
    if (needed <= mmu->pages_capacity) {
        return;
    }
    size_t new_capacity = mmu->pages_capacity ? mmu->pages_capacity * 2 : PAGE_TABLE_INITIAL;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    mmu->pages = sim_realloc(mmu->pages, new_capacity * sizeof(Page *));
    for (size_t i = mmu->pages_capacity; i < new_capacity; ++i) {
        mmu->pages[i] = NULL;
    }
    mmu->pages_capacity = new_capacity;
}

// Reserva espacio en la tabla de procesos hasta cubrir el pid pedido.
static void ensure_process_capacity(Simulator *sim, sim_pid_t pid) {
    size_t needed = (size_t)pid + 1;
    if (needed <= sim->process_capacity) {
        return;
    }
    size_t new_capacity = sim->process_capacity ? sim->process_capacity * 2 : PROCESS_TABLE_INITIAL;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    sim->processes = sim_realloc(sim->processes, new_capacity * sizeof(Process *));
    for (size_t i = sim->process_capacity; i < new_capacity; ++i) {
        sim->processes[i] = NULL;
    }
    sim->process_capacity = new_capacity;
}

// Amplía la tabla global de punteros hasta incluir el identificador dado.
static void ensure_ptr_table_capacity(Simulator *sim, sim_ptr_t ptr_id) {
    size_t needed = (size_t)ptr_id + 1;
    if (needed <= sim->ptr_table_capacity) {
        return;
    }
    size_t new_capacity = sim->ptr_table_capacity ? sim->ptr_table_capacity * 2 : PTR_TABLE_INITIAL;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    sim->ptr_table = sim_realloc(sim->ptr_table, new_capacity * sizeof(PtrMap *));
    for (size_t i = sim->ptr_table_capacity; i < new_capacity; ++i) {
        sim->ptr_table[i] = NULL;
    }
    sim->ptr_table_capacity = new_capacity;
}

// Obtiene el proceso solicitado y lo crea si la bandera 'create' está activa.
static Process *sim_get_process(Simulator *sim, sim_pid_t pid, int create) {
    if (pid == 0) {
        return NULL;
    }
    ensure_process_capacity(sim, pid);
    Process *proc = sim->processes[pid];
    if (!proc && create) {
        proc = xmalloc(sizeof(*proc));
        proc->pid = pid;
        proc->ptrs = NULL;
        proc->ptr_count = 0;
        proc->ptr_capacity = 0;
        proc->killed = 0;
        sim->processes[pid] = proc;
        sim->process_count++;
    }
    return proc;
}

// Busca una estructura PtrMap asociada al identificador global dado.
static PtrMap *sim_lookup_ptrmap(const Simulator *sim, sim_ptr_t ptr_id) {
    if (ptr_id == 0 || ptr_id >= sim->ptr_table_capacity) {
        return NULL;
    }
    return sim->ptr_table[ptr_id];
}

// Recupera un puntero y su proceso dueño para operaciones posteriores.
static PtrLookupResult sim_lookup_ptr(Simulator *sim, sim_ptr_t ptr_id) {
    PtrLookupResult result = {0};
    PtrMap *ptr = sim_lookup_ptrmap(sim, ptr_id);
    if (!ptr) {
        return result;
    }
    Process *proc = sim_get_process(sim, ptr->owner_pid, 0);
    result.process = proc;
    result.ptr = ptr;
    return result;
}

// Registra un PtrMap en la tabla global para permitir búsquedas rápidas.
static void sim_register_ptrmap(Simulator *sim, PtrMap *ptr) {
    ensure_ptr_table_capacity(sim, ptr->id);
    if (!sim->ptr_table[ptr->id]) {
        sim->ptr_table_count++;
    }
    sim->ptr_table[ptr->id] = ptr;
}

// Elimina la referencia al PtrMap cuando deja de existir en la simulación.
static void sim_unregister_ptrmap(Simulator *sim, sim_ptr_t ptr_id) {
    if (ptr_id == 0 || ptr_id >= sim->ptr_table_capacity) {
        return;
    }
    if (sim->ptr_table[ptr_id]) {
        sim->ptr_table[ptr_id] = NULL;
        if (sim->ptr_table_count > 0) {
            sim->ptr_table_count--;
        }
    }
}

// Inserta un PtrMap en la lista del proceso expandiendo memoria si es necesario.
static void process_add_ptr(Process *proc, PtrMap *ptr) {
    if (proc->ptr_count == proc->ptr_capacity) {
        size_t new_capacity = proc->ptr_capacity ? proc->ptr_capacity * 2 : 4;
        proc->ptrs = sim_realloc(proc->ptrs, new_capacity * sizeof(PtrMap *));
        proc->ptr_capacity = new_capacity;
    }
    proc->ptrs[proc->ptr_count++] = ptr;
}

// Quita un PtrMap de la lista del proceso intercambiándolo con el último.
static void process_remove_ptr(Process *proc, PtrMap *ptr) {
    if (!proc || proc->ptr_count == 0) {
        return;
    }
    for (size_t i = 0; i < proc->ptr_count; ++i) {
        if (proc->ptrs[i] == ptr) {
            proc->ptrs[i] = proc->ptrs[proc->ptr_count - 1];
            proc->ptrs[proc->ptr_count - 1] = NULL;
            proc->ptr_count--;
            return;
        }
    }
}

// Devuelve la página asociada a un id si existe en la tabla del MMU.
static Page *sim_get_page(const Simulator *sim, sim_pageid_t page_id) {
    if (page_id == 0 || page_id >= sim->mmu.pages_capacity) {
        return NULL;
    }
    return sim->mmu.pages[page_id];
}

// Borra la entrada de la tabla de páginas y actualiza el conteo global.
static void mmu_remove_page_entry(Simulator *sim, Page *page) {
    if (!page) {
        return;
    }
    if (page->id < sim->mmu.pages_capacity && sim->mmu.pages[page->id]) {
        sim->mmu.pages[page->id] = NULL;
        if (sim->mmu.page_count > 0) {
            sim->mmu.page_count--;
        }
    }
}

// Marca un marco como libre y lo devuelve a la pila de disponibles.
static void mmu_release_frame(MMU *mmu, int frame_index) {
    if (frame_index < 0 || frame_index >= RAM_FRAMES) {
        return;
    }
    if (mmu->frames[frame_index].occupied) {
        mmu->frames[frame_index].occupied = 0;
        mmu->frames[frame_index].page_id = 0;
        mmu->free_frames[mmu->free_count++] = frame_index;
    }
}

// Extrae el índice de un marco libre; devuelve -1 si no quedan disponibles.
static int mmu_pop_free_frame(MMU *mmu) {
    if (mmu->free_count == 0) {
        return -1;
    }
    return mmu->free_frames[--mmu->free_count];
}

// Libera la cola de usos futuros asociada a una página OPT.
static void destroy_future_queue(FutureUseQueue *queue) {
    if (queue->positions) {
        free(queue->positions);
    }
    memset(queue, 0, sizeof(*queue));
}

// Destruye la estructura de página y su cola auxiliar.
static void destroy_page(Simulator *sim, Page *page) {
    (void)sim;
    if (!page) {
        return;
    }
    destroy_future_queue(&page->future_uses);
    free(page);
}

// Saca a la página de RAM o swap y libera su marco si correspondía.
static void detach_page_from_memory(Simulator *sim, Page *page) {
    if (!page) {
        return;
    }
    if (page->in_ram && page->frame_index >= 0) {
        algorithms_on_page_evicted(sim, page);
        mmu_release_frame(&sim->mmu, page->frame_index);
    } else if (!page->in_ram && sim->total_pages_in_swap > 0) {
        sim->total_pages_in_swap--;
    }
    page->in_ram = 0;
    page->frame_index = -1;
}

// Elimina una página de todas las estructuras de seguimiento.
static void remove_page_completely(Simulator *sim, Page *page) {
    if (!page) {
        return;
    }
    detach_page_from_memory(sim, page);
    mmu_remove_page_entry(sim, page);
    destroy_page(sim, page);
}

// Destruye un PtrMap liberando sus páginas asociadas y ajustando estadísticas.
static void remove_ptrmap(Simulator *sim, Process *proc, PtrMap *ptr) {
    if (!ptr) {
        return;
    }

    sim_unregister_ptrmap(sim, ptr->id);
    if (proc) {
        process_remove_ptr(proc, ptr);
    }

    size_t wasted = (size_t)ptr->num_pages * PAGE_SIZE - ptr->byte_size;
    if (sim->internal_fragmentation_bytes >= wasted) {
        sim->internal_fragmentation_bytes -= wasted;
    } else {
        sim->internal_fragmentation_bytes = 0;
    }

    for (uint32_t i = 0; i < ptr->num_pages; ++i) {
        sim_pageid_t page_id = ptr->pages[i];
        Page *page = sim_get_page(sim, page_id);
        if (page) {
            remove_page_completely(sim, page);
        }
    }

    free(ptr->pages);
    free(ptr);

    sim->stats.ptr_deletions++;
}

// Limpia todos los procesos, páginas y métricas del simulador.
static void sim_clear_state(Simulator *sim, int free_arrays) {
    if (!sim) {
        return;
    }

    if (sim->processes) {
        for (size_t pid = 0; pid < sim->process_capacity; ++pid) {
            Process *proc = sim->processes[pid];
            if (!proc) {
                continue;
            }
            while (proc->ptr_count > 0) {
                PtrMap *ptr = proc->ptrs[proc->ptr_count - 1];
                remove_ptrmap(sim, proc, ptr);
            }
            free(proc->ptrs);
            free(proc);
            sim->processes[pid] = NULL;
        }
    }

    sim->process_count = 0;

    if (sim->mmu.pages) {
        for (size_t idx = 0; idx < sim->mmu.pages_capacity; ++idx) {
            Page *page = sim->mmu.pages[idx];
            if (page) {
                destroy_page(sim, page);
                sim->mmu.pages[idx] = NULL;
            }
        }
    }
    sim->mmu.page_count = 0;

    if (sim->ptr_table) {
        for (size_t i = 0; i < sim->ptr_table_capacity; ++i) {
            sim->ptr_table[i] = NULL;
        }
    }
    sim->ptr_table_count = 0;

    mmu_initialize_frames(&sim->mmu);

    sim->clock = 0;
    sim->thrashing_time = 0;
    sim->total_pages_in_swap = 0;
    sim->stats = (SimStats){0};
    sim->internal_fragmentation_bytes = 0;
    sim->next_page_id = 1;
    sim->next_ptr_id = 1;

    algorithms_reset(sim);

    if (free_arrays) {
        free(sim->processes);
        sim->processes = NULL;
        sim->process_capacity = 0;

        free(sim->mmu.pages);
        sim->mmu.pages = NULL;
        sim->mmu.pages_capacity = 0;

        free(sim->ptr_table);
        sim->ptr_table = NULL;
        sim->ptr_table_capacity = 0;
    }
}

// Crea un nuevo PtrMap y reserva espacio para sus páginas virtuales.
static PtrMap *create_ptrmap(Simulator *sim, Process *proc, sim_ptr_t ptr_id, size_t byte_size, uint32_t num_pages) {
    (void)sim;
    PtrMap *ptr = xmalloc(sizeof(*ptr));
    ptr->id = ptr_id;
    ptr->owner_pid = proc ? proc->pid : 0;
    ptr->byte_size = (uint32_t)byte_size;
    ptr->num_pages = num_pages;
    ptr->pages_capacity = num_pages;
    ptr->pages = xmalloc(sizeof(sim_pageid_t) * num_pages);
    memset(ptr->pages, 0, sizeof(sim_pageid_t) * num_pages);
    return ptr;
}

static void load_future_use_data(Simulator *sim, Page *page) {
    if (!sim || !page || !sim->future_dataset || !sim->future_dataset->entries) {
        return;
    }
    if (page->id >= sim->future_dataset->capacity) {
        return;
    }
    const FutureUseEntry *entry = &sim->future_dataset->entries[page->id];
    if (!entry || entry->count == 0 || !entry->positions) {
        page->future_uses = (FutureUseQueue){0};
        page->next_use_pos = SIZE_MAX;
        return;
    }

    page->future_uses.positions = xmalloc(entry->count * sizeof(size_t));
    memcpy(page->future_uses.positions, entry->positions, entry->count * sizeof(size_t));
    page->future_uses.count = entry->count;
    page->future_uses.capacity = entry->count;
    page->future_uses.cursor = 0;
    page->next_use_pos = entry->positions[0];
}

// Construye una página virtual y la registra en la tabla global del MMU.
static Page *create_page(Simulator *sim, sim_pid_t owner_pid, sim_ptr_t owner_ptr, uint32_t page_index) {
    Page *page = xmalloc(sizeof(*page));
    memset(page, 0, sizeof(*page));
    page->id = sim->next_page_id++;
    page->owner_pid = owner_pid;
    page->owner_ptr = owner_ptr;
    page->page_index = page_index;
    page->in_ram = 0;
    page->frame_index = -1;
    page->ref_bit = 0;
    page->dirty = 0;
    page->last_used = 0;
    page->next_use_pos = SIZE_MAX;
    mmu_ensure_page_capacity(&sim->mmu, page->id);
    sim->mmu.pages[page->id] = page;
    sim->mmu.page_count++;
    load_future_use_data(sim, page);
    return page;
}

// Ubica una página en un marco físico y notifica al algoritmo de reemplazo.
static void place_page_in_frame(Simulator *sim, Page *page, int frame_index) {
    MMU *mmu = &sim->mmu;
    if (frame_index < 0 || frame_index >= RAM_FRAMES) {
        return;
    }
    mmu->frames[frame_index].occupied = 1;
    mmu->frames[frame_index].page_id = page->id;
    page->in_ram = 1;
    page->frame_index = frame_index;
    page->ref_bit = 1;
    page->last_used = sim->clock;
    algorithms_on_page_loaded(sim, page);
}

// Recupera la estructura de la página candidata a ser expulsada.
static Page *get_victim_page(Simulator *sim, sim_pageid_t victim_id) {
    Page *victim = sim_get_page(sim, victim_id);
    if (!victim || !victim->in_ram) {
        return NULL;
    }
    return victim;
}

// Determina qué página será desalojada según la política activa o una opción de respaldo.
static sim_pageid_t select_victim_page(Simulator *sim) {
    sim_pageid_t candidate = choose_victim(sim);

    if (candidate) {
        Page *page = get_victim_page(sim, candidate);
        if (page) {
            return candidate;
        }
    }

    for (int i = 0; i < RAM_FRAMES; ++i) {
        if (sim->mmu.frames[i].occupied) {
            return sim->mmu.frames[i].page_id;
        }
    }

    return 0;
}

// Expulsa una página de RAM y devuelve el índice del marco liberado.
static int evict_page(Simulator *sim) {
    sim_pageid_t victim_id = select_victim_page(sim);
    Page *victim = get_victim_page(sim, victim_id);
    if (!victim) {
        return -1;
    }

    int frame_index = victim->frame_index;
    detach_page_from_memory(sim, victim);
    victim->ref_bit = 0;
    victim->last_used = sim->clock;
    sim->total_pages_in_swap++;
    sim->stats.pages_evicted++;
    return frame_index;
}

// Obtiene un marco libre; si no hay, fuerza desalojos y marca fallas de página.
static int acquire_frame(Simulator *sim, int *was_fault) {
    int frame_index = mmu_pop_free_frame(&sim->mmu);
    if (frame_index >= 0) {
        if (was_fault) {
            *was_fault = 0;
        }
        return frame_index;
    }

    while (frame_index < 0) {
        int evicted_frame = evict_page(sim);
        if (evicted_frame < 0) {
            break;
        }
        frame_index = mmu_pop_free_frame(&sim->mmu);
    }

    if (was_fault) {
        *was_fault = 1;
    }
    return frame_index;
}

// Atiende la instrucción NEW reservando páginas para un proceso.
static void handle_new(Simulator *sim, const Instruction *ins) {
    Process *proc = sim_get_process(sim, ins->pid, 1);
    if (!proc) {
        return;
    }

    sim_ptr_t ptr_id = ins->ptr_id;
    if (ptr_id == 0) {
        ptr_id = sim->next_ptr_id++;
    } else if (ptr_id >= sim->next_ptr_id) {
        sim->next_ptr_id = ptr_id + 1;
    }

    size_t num_pages = (ins->size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (num_pages == 0) {
        num_pages = 1;
    }

    PtrMap *ptr = create_ptrmap(sim, proc, ptr_id, ins->size, (uint32_t)num_pages);
    sim_register_ptrmap(sim, ptr);
    process_add_ptr(proc, ptr);

    size_t fragmentation = num_pages * PAGE_SIZE - ins->size;
    sim->internal_fragmentation_bytes += fragmentation;
    sim->stats.ptr_allocations++;
    sim->stats.bytes_requested += ins->size;
    sim->stats.pages_created += num_pages;

    for (uint32_t i = 0; i < ptr->num_pages; ++i) {
        Page *page = create_page(sim, proc->pid, ptr_id, i);
        ptr->pages[i] = page->id;

        int was_fault = 0;
        int frame_index = acquire_frame(sim, &was_fault);
        if (frame_index < 0) {
            log_debug("[sim] Unable to allocate frame for new page %u\n", page->id);
            continue;
        }

        if (was_fault) {
            record_page_fault(sim, 1);
        } else {
            record_page_hit(sim);
        }

        place_page_in_frame(sim, page, frame_index);
        algorithms_on_page_accessed(sim, page);
    }
}

// Procesa una instrucción USE trayendo páginas a RAM si es necesario.
static void handle_use(Simulator *sim, const Instruction *ins) {
    PtrLookupResult lookup = sim_lookup_ptr(sim, ins->ptr_id);
    if (!lookup.ptr) {
        return;
    }

    PtrMap *ptr = lookup.ptr;
    for (uint32_t i = 0; i < ptr->num_pages; ++i) {
        sim_pageid_t page_id = ptr->pages[i];
        Page *page = sim_get_page(sim, page_id);
        if (!page) {
            continue;
        }

        if (page->in_ram) {
            record_page_hit(sim);
            page->last_used = sim->clock;
            page->ref_bit = 1;
            algorithms_on_page_accessed(sim, page);
        } else {
            int was_fault = 0;
            int frame_index = acquire_frame(sim, &was_fault);
            if (frame_index < 0) {
                log_debug("[sim] Unable to bring page %u into RAM\n", page_id);
                continue;
            }

            if (sim->total_pages_in_swap > 0) {
                sim->total_pages_in_swap--;
            }

            record_page_fault(sim, 1);

            place_page_in_frame(sim, page, frame_index);
            algorithms_on_page_accessed(sim, page);
        }
    }
}

// Maneja la instrucción DELETE liberando la memoria asociada al puntero.
static void handle_delete(Simulator *sim, const Instruction *ins) {
    PtrLookupResult lookup = sim_lookup_ptr(sim, ins->ptr_id);
    if (!lookup.ptr) {
        return;
    }
    remove_ptrmap(sim, lookup.process, lookup.ptr);
}

// Ejecuta la instrucción KILL eliminando todas las asignaciones del proceso.
static void handle_kill(Simulator *sim, const Instruction *ins) {
    Process *proc = sim_get_process(sim, ins->pid, 0);
    if (!proc) {
        return;
    }

    while (proc->ptr_count > 0) {
        PtrMap *ptr = proc->ptrs[proc->ptr_count - 1];
        remove_ptrmap(sim, proc, ptr);
    }

    proc->killed = 1;
    free(proc->ptrs);
    free(proc);
    sim->processes[ins->pid] = NULL;
    if (sim->process_count > 0) {
        sim->process_count--;
    }
}

// Inicializa el simulador y deja listo el MMU y el algoritmo requerido.
void sim_init(Simulator *sim, const char *name, AlgorithmType type) {
    if (!sim) {
        return;
    }

    memset(sim, 0, sizeof(*sim));
    if (name) {
        strncpy(sim->name, name, sizeof(sim->name) - 1);
    }
    sim->algorithm = type;
    sim->next_page_id = 1;
    sim->next_ptr_id = 1;
    sim->rng_seed = 0;
    sim->future_dataset = NULL;

    mmu_initialize_frames(&sim->mmu);
    algorithms_init(sim);
}

// Limpia el estado interno pero conserva las estructuras principales.
void sim_reset(Simulator *sim) {
    sim_clear_state(sim, 0);
}

// Libera definitivamente la memoria reservada por el simulador.
void sim_free(Simulator *sim) {
    sim_clear_state(sim, 1);
    algorithms_free(sim);
}

void sim_set_future_dataset(Simulator *sim, const FutureUseDataset *dataset) {
    if (!sim) {
        return;
    }
    sim->future_dataset = dataset;
}

// Ejecuta una instrucción del flujo global actualizando estadísticas.
void sim_process_instruction(Simulator *sim, const Instruction *ins, int global_index) {
    (void)global_index;

    if (!sim || !ins) {
        return;
    }

    sim->stats.total_instructions++;

    switch (ins->type) {
        case INS_NEW:
            handle_new(sim, ins);
            break;
        case INS_USE:
            handle_use(sim, ins);
            break;
        case INS_DELETE:
            handle_delete(sim, ins);
            break;
        case INS_KILL:
            handle_kill(sim, ins);
            break;
        default:
            break;
    }
}

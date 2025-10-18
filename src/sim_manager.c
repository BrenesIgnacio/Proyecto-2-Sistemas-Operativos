#include "sim_manager.h"
#include "sim_engine.h"
#include "util.h"

#include <string.h>

static void *mgr_realloc(void *ptr, size_t size) {
    void *tmp = realloc(ptr, size);
    if (!tmp && size != 0) {
        fprintf(stderr, "Out of memory (sim_manager realloc)\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

typedef struct PrePtrEntry {
    int valid;
    uint32_t num_pages;
    sim_pageid_t *pages;
} PrePtrEntry;

typedef struct PreProcessEntry {
    int alive;
    sim_ptr_t *ptrs;
    size_t count;
    size_t capacity;
} PreProcessEntry;

static void free_future_dataset(FutureUseDataset *dataset) {
    if (!dataset || !dataset->entries) {
        return;
    }
    for (size_t i = 0; i < dataset->capacity; ++i) {
        free(dataset->entries[i].positions);
        dataset->entries[i].positions = NULL;
        dataset->entries[i].count = 0;
        dataset->entries[i].capacity = 0;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->capacity = 0;
}

static void ensure_event_capacity(SimManager *mgr, size_t needed) {
    if (needed <= mgr->event_capacity) {
        return;
    }
    size_t new_capacity = mgr->event_capacity ? mgr->event_capacity * 2 : 128;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    mgr->events = mgr_realloc(mgr->events, new_capacity * sizeof(AccessEvent));
    mgr->event_capacity = new_capacity;
}

static void append_event(SimManager *mgr, size_t instr_index, sim_pageid_t page_id) {
    ensure_event_capacity(mgr, mgr->event_count + 1);
    mgr->events[mgr->event_count].instruction_index = instr_index;
    mgr->events[mgr->event_count].page_id = page_id;
    mgr->event_count++;
}

static void ensure_ptr_entry_capacity(PrePtrEntry **table, size_t *capacity, sim_ptr_t ptr_id) {
    size_t needed = (size_t)ptr_id + 1;
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = *capacity ? *capacity * 2 : 128;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *table = mgr_realloc(*table, new_capacity * sizeof(PrePtrEntry));
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*table)[i].valid = 0;
        (*table)[i].num_pages = 0;
        (*table)[i].pages = NULL;
    }
    *capacity = new_capacity;
}

static void ensure_process_entry_capacity(PreProcessEntry **table, size_t *capacity, sim_pid_t pid) {
    size_t needed = (size_t)pid + 1;
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = *capacity ? *capacity * 2 : 16;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *table = mgr_realloc(*table, new_capacity * sizeof(PreProcessEntry));
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*table)[i].alive = 0;
        (*table)[i].ptrs = NULL;
        (*table)[i].count = 0;
        (*table)[i].capacity = 0;
    }
    *capacity = new_capacity;
}

static void process_add_ptr_id(PreProcessEntry *proc, sim_ptr_t ptr_id) {
    if (!proc) {
        return;
    }
    if (proc->count == proc->capacity) {
        size_t new_capacity = proc->capacity ? proc->capacity * 2 : 4;
    proc->ptrs = mgr_realloc(proc->ptrs, new_capacity * sizeof(sim_ptr_t));
        proc->capacity = new_capacity;
    }
    proc->ptrs[proc->count++] = ptr_id;
    proc->alive = 1;
}

static void process_remove_ptr_id(PreProcessEntry *proc, sim_ptr_t ptr_id) {
    if (!proc || proc->count == 0) {
        return;
    }
    for (size_t i = 0; i < proc->count; ++i) {
        if (proc->ptrs[i] == ptr_id) {
            proc->ptrs[i] = proc->ptrs[proc->count - 1];
            proc->count--;
            return;
        }
    }
}

static void destroy_ptr_entry(PrePtrEntry *entry) {
    if (!entry || !entry->valid) {
        return;
    }
    free(entry->pages);
    entry->pages = NULL;
    entry->num_pages = 0;
    entry->valid = 0;
}

static void future_entry_append(FutureUseEntry *entry, size_t position) {
    if (entry->count == entry->capacity) {
        size_t new_capacity = entry->capacity ? entry->capacity * 2 : 4;
    entry->positions = mgr_realloc(entry->positions, new_capacity * sizeof(size_t));
        entry->capacity = new_capacity;
    }
    entry->positions[entry->count++] = position;
}

static void shrink_future_entries(FutureUseDataset *dataset) {
    if (!dataset || !dataset->entries) {
        return;
    }
    for (size_t i = 0; i < dataset->capacity; ++i) {
        FutureUseEntry *entry = &dataset->entries[i];
        if (entry->count == 0) {
            free(entry->positions);
            entry->positions = NULL;
            entry->capacity = 0;
        } else if (entry->capacity > entry->count) {
            entry->positions = mgr_realloc(entry->positions, entry->count * sizeof(size_t));
            entry->capacity = entry->count;
        }
    }
}

static void build_future_dataset(SimManager *mgr, sim_pageid_t max_page_id) {
    free_future_dataset(&mgr->future_dataset);

    size_t capacity = (size_t)max_page_id + 1;
    if (capacity == 0) {
        capacity = 1;
    }
    mgr->future_dataset.entries = xmalloc(capacity * sizeof(FutureUseEntry));
    mgr->future_dataset.capacity = capacity;
    for (size_t i = 0; i < capacity; ++i) {
        mgr->future_dataset.entries[i].positions = NULL;
        mgr->future_dataset.entries[i].count = 0;
        mgr->future_dataset.entries[i].capacity = 0;
    }

    for (size_t idx = 0; idx < mgr->event_count; ++idx) {
        sim_pageid_t page_id = mgr->events[idx].page_id;
        if (page_id >= capacity) {
            continue;
        }
        FutureUseEntry *entry = &mgr->future_dataset.entries[page_id];
        future_entry_append(entry, idx);
    }

    shrink_future_entries(&mgr->future_dataset);

    if (mgr->sim_opt) {
        sim_set_future_dataset(mgr->sim_opt, &mgr->future_dataset);
    }
    if (mgr->sim_user) {
        sim_set_future_dataset(mgr->sim_user, &mgr->future_dataset);
    }
}

static void precompute_events(SimManager *mgr) {
    mgr->event_count = 0;

    PrePtrEntry *ptr_table = NULL;
    size_t ptr_capacity = 0;
    PreProcessEntry *proc_table = NULL;
    size_t proc_capacity = 0;

    sim_pageid_t next_page_id = 1;

    for (size_t i = 0; i < mgr->instr_count; ++i) {
        Instruction *ins = &mgr->instructions[i];
        switch (ins->type) {
            case INS_NEW: {
                size_t num_pages = (ins->size + PAGE_SIZE - 1) / PAGE_SIZE;
                if (num_pages == 0) {
                    num_pages = 1;
                }
                ensure_ptr_entry_capacity(&ptr_table, &ptr_capacity, ins->ptr_id);
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                destroy_ptr_entry(entry);
                entry->pages = xmalloc(sizeof(sim_pageid_t) * num_pages);
                entry->num_pages = (uint32_t)num_pages;
                entry->valid = 1;
                for (size_t p = 0; p < num_pages; ++p) {
                    entry->pages[p] = next_page_id++;
                    append_event(mgr, i, entry->pages[p]);
                }

                ensure_process_entry_capacity(&proc_table, &proc_capacity, ins->pid);
                process_add_ptr_id(&proc_table[ins->pid], ins->ptr_id);
                break;
            }
            case INS_USE: {
                if (ins->ptr_id >= ptr_capacity) {
                    break;
                }
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                if (!entry->valid) {
                    break;
                }
                for (uint32_t p = 0; p < entry->num_pages; ++p) {
                    append_event(mgr, i, entry->pages[p]);
                }
                break;
            }
            case INS_DELETE: {
                if (ins->ptr_id >= ptr_capacity) {
                    break;
                }
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                if (!entry->valid) {
                    break;
                }
                destroy_ptr_entry(entry);
                if (ins->pid < proc_capacity) {
                    process_remove_ptr_id(&proc_table[ins->pid], ins->ptr_id);
                }
                break;
            }
            case INS_KILL: {
                if (ins->pid >= proc_capacity) {
                    break;
                }
                PreProcessEntry *proc = &proc_table[ins->pid];
                if (!proc->alive) {
                    break;
                }
                for (size_t p = 0; p < proc->count; ++p) {
                    sim_ptr_t ptr_id = proc->ptrs[p];
                    if (ptr_id < ptr_capacity) {
                        destroy_ptr_entry(&ptr_table[ptr_id]);
                    }
                }
                free(proc->ptrs);
                proc->ptrs = NULL;
                proc->count = 0;
                proc->capacity = 0;
                proc->alive = 0;
                break;
            }
            default:
                break;
        }
    }

    for (size_t idx = 0; idx < ptr_capacity; ++idx) {
        destroy_ptr_entry(&ptr_table[idx]);
    }
    free(ptr_table);

    if (proc_table) {
        for (size_t idx = 0; idx < proc_capacity; ++idx) {
            free(proc_table[idx].ptrs);
        }
    }
    free(proc_table);

    sim_pageid_t max_page_id = next_page_id ? (next_page_id - 1) : 0;
    build_future_dataset(mgr, max_page_id);
}

// Inicializa las referencias base del administrador, las instrucciones y precomputaciones OPT.
void sim_manager_init(SimManager *mgr, Instruction *instrs, size_t count, AlgorithmType user_alg) {
    if (!mgr) {
        return;
    }

    memset(mgr, 0, sizeof(*mgr));
    mgr->instructions = instrs;
    mgr->instr_count = count;
    mgr->current_index = 0;
    mgr->running = 0;
    mgr->user_algorithm = user_alg;

    precompute_events(mgr);
}

// Avanza la simulación un paso cuando se integre la lógica.
void sim_manager_step(SimManager *mgr) {
    (void)mgr;
}

// Libera estructuras del administrador al finalizar la simulación.
void sim_manager_free(SimManager *mgr) {
    if (!mgr) {
        return;
    }

    free(mgr->events);
    mgr->events = NULL;
    mgr->event_count = 0;
    mgr->event_capacity = 0;

    free_future_dataset(&mgr->future_dataset);

    if (mgr->sim_opt) {
        sim_free(mgr->sim_opt);
        free(mgr->sim_opt);
        mgr->sim_opt = NULL;
    }

    if (mgr->sim_user) {
        sim_free(mgr->sim_user);
        free(mgr->sim_user);
        mgr->sim_user = NULL;
    }
}

#ifndef SIM_TYPES_H
#define SIM_TYPES_H

#include "common.h"

typedef struct FutureUseQueue {
    size_t *positions;       // índices de eventos absolutos donde se usará esta página
    size_t count;            // total de usos futuros registrados
    size_t capacity;         // capacidad de almacenamiento para posiciones
    size_t cursor;           // índice del próximo uso futuro (relativo al array de posiciones)
} FutureUseQueue;

typedef struct FutureUseEntry {
    size_t *positions;       // lista inmutable de índices de acceso futuro para una página
    size_t count;
    size_t capacity;
} FutureUseEntry;

typedef struct FutureUseDataset {
    FutureUseEntry *entries;
    size_t capacity;
} FutureUseDataset;

typedef struct Page {
    sim_pageid_t id;
    sim_pid_t owner_pid;
    sim_ptr_t owner_ptr;
    uint32_t page_index;
    int in_ram;
    int frame_index;
    int ref_bit;
    int dirty;
    sim_time_t last_used;
    size_t next_use_pos;     // índice de evento absoluto en caché para OPT (SIZE_MAX si no hay)
    FutureUseQueue future_uses;
} Page;

typedef struct Frame {
    int occupied;
    sim_pageid_t page_id;
} Frame;

typedef struct PtrMap {
    sim_ptr_t id;
    sim_pid_t owner_pid;
    uint32_t byte_size;
    uint32_t num_pages;
    uint32_t pages_capacity;
    sim_pageid_t *pages;
} PtrMap;

typedef struct Process {
    sim_pid_t pid;
    PtrMap **ptrs;
    size_t ptr_count;
    size_t ptr_capacity;
    int killed;
} Process;

typedef enum {
    ALG_OPT,
    ALG_FIFO,
    ALG_SC,
    ALG_MRU,
    ALG_RND
} AlgorithmType;

typedef struct MMU {
    Frame frames[RAM_FRAMES];
    Page **pages;
    size_t page_count;
    size_t pages_capacity;
    int free_frames[RAM_FRAMES];
    size_t free_count;
} MMU;

typedef struct SimStats {
    size_t total_instructions;
    size_t page_faults;
    size_t page_hits;
    size_t pages_created;
    size_t pages_evicted;
    size_t ptr_allocations;
    size_t ptr_deletions;
    size_t bytes_requested;
} SimStats;

typedef struct Simulator {
    char name[32];
    MMU mmu;
    Process **processes;
    size_t process_count;
    size_t process_capacity;
    PtrMap **ptr_table;
    size_t ptr_table_capacity;
    size_t ptr_table_count;
    sim_time_t clock;
    sim_time_t thrashing_time;
    size_t total_pages_in_swap;
    AlgorithmType algorithm;
    void *alg_state;
    SimStats stats;
    sim_pageid_t next_page_id;
    sim_ptr_t next_ptr_id;
    size_t internal_fragmentation_bytes;
    unsigned int rng_seed;
    const FutureUseDataset *future_dataset;
} Simulator;

#endif

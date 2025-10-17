#ifndef SIM_TYPES_H
#define SIM_TYPES_H

#include "common.h"

typedef struct Page {
    sim_pageid_t id;
    sim_pid_t owner_pid;
    sim_ptr_t owner_ptr;
    uint32_t page_index;
    int in_ram;
    int frame_index;
    int ref_bit;
    sim_time_t last_used;
    uint64_t next_use_pos;
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
    sim_pageid_t *pages;
} PtrMap;

typedef struct Process {
    sim_pid_t pid;
    PtrMap **ptrs;
    size_t ptr_count;
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
    size_t pages_capacity;
} MMU;

typedef struct Simulator {
    char name[32];
    MMU mmu;
    Process **processes;
    size_t process_count;
    sim_time_t clock;
    sim_time_t thrashing_time;
    size_t total_pages_in_swap;
    AlgorithmType algorithm;
    void *alg_state;
} Simulator;

#endif

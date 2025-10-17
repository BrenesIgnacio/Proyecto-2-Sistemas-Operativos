#ifndef SIM_MANAGER_H
#define SIM_MANAGER_H

#include "sim_types.h"
#include "instr_parser.h"

typedef struct SimManager {
    Simulator *sim_opt;
    Simulator *sim_user;
    Instruction *instructions;
    size_t instr_count;
    int current_index;
    int running;
} SimManager;

void sim_manager_init(SimManager *mgr, Instruction *instrs, size_t count, AlgorithmType user_alg);
void sim_manager_step(SimManager *mgr);
void sim_manager_free(SimManager *mgr);

#endif

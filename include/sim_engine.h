#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include "sim_types.h"
#include "instr_parser.h"

void sim_init(Simulator *sim, const char *name, AlgorithmType type);
void sim_reset(Simulator *sim);
void sim_free(Simulator *sim);
void sim_process_instruction(Simulator *sim, const Instruction *ins, int global_index);

#endif

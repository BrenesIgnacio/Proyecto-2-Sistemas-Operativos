#include "sim_engine.h"

void sim_init(Simulator *sim, const char *name, AlgorithmType type) {
    (void)sim; (void)name; (void)type;
}

void sim_reset(Simulator *sim) {
    (void)sim;
}

void sim_free(Simulator *sim) {
    (void)sim;
}

void sim_process_instruction(Simulator *sim, const Instruction *ins, int global_index) {
    (void)sim; (void)ins; (void)global_index;
}

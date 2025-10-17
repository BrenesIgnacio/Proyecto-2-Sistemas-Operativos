#include "config.h"
#include <stdio.h>

void config_load_defaults(Config *cfg) {
    cfg->seed = 1234;
    cfg->process_count = 10;
    cfg->op_count = 500;
    cfg->algorithm = 1; // por ejemplo FIFO por defecto
}

void config_print(const Config *cfg) {
    printf("Seed: %u | Processes: %d | Ops: %d | Algorithm: %d\n",
           cfg->seed, cfg->process_count, cfg->op_count, cfg->algorithm);
}

#include "config.h"
#include <stdio.h>

// Carga parámetros base pensados para una demostración rápida.
void config_load_defaults(Config *cfg) {
    cfg->seed = 1234;
    cfg->process_count = 10;
    cfg->op_count = 500;
    cfg->algorithm = 1; //FIFO por defecto
}

// Imprime los valores de configuración activos para depuración.
void config_print(const Config *cfg) {
    printf("Seed: %u | Processes: %d | Ops: %d | Algorithm: %d\n",
           cfg->seed, cfg->process_count, cfg->op_count, cfg->algorithm);
}

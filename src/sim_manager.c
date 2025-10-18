#include "sim_manager.h"

// Inicializa las referencias base del administrador y selecciona algoritmo de usuario.
void sim_manager_init(SimManager *mgr, Instruction *instrs, size_t count, AlgorithmType user_alg) {
    (void)mgr; (void)instrs; (void)count; (void)user_alg;
}

// Avanza la simulación un paso cuando se integre la lógica.
void sim_manager_step(SimManager *mgr) {
    (void)mgr;
}

// Libera estructuras del administrador al finalizar la simulación.
void sim_manager_free(SimManager *mgr) {
    (void)mgr;
}

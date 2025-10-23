#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include "sim_types.h"
#include "instr_parser.h"

// Inicializa estructuras básicas del simulador y selecciona el algoritmo de reemplazo.
void sim_init(Simulator *sim, const char *name, AlgorithmType type);
// Restablece el simulador dejando memoria y estadísticas en cero.
void sim_reset(Simulator *sim);
// Libera todos los recursos asociados al simulador.
void sim_free(Simulator *sim);
// Ejecuta una instrucción y actualiza el estado y métricas de la simulación.
void sim_process_instruction(Simulator *sim, const Instruction *ins, int global_index);
void sim_set_future_dataset(Simulator *sim, const FutureUseDataset *dataset);

#endif

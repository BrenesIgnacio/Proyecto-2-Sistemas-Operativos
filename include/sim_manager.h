#ifndef SIM_MANAGER_H
#define SIM_MANAGER_H

#include "sim_types.h"
#include "instr_parser.h"
typedef struct AccessEvent {
    size_t instruction_index;
    sim_pageid_t page_id;
} AccessEvent;

typedef struct SimManager {
    Simulator *sim_opt;
    Simulator *sim_user;
    Instruction *instructions;
    size_t instr_count;
    int current_index;
    int running;
    AlgorithmType user_algorithm;
    AccessEvent *events;
    size_t event_count;
    size_t event_capacity;
    FutureUseDataset future_dataset;
} SimManager;

// Configura el administrador con las instrucciones cargadas y el algoritmo del usuario.
void sim_manager_init(SimManager *mgr, Instruction *instrs, size_t count, AlgorithmType user_alg);
// Avanza la simulación un paso respetando el ritmo elegido por la interfaz.
void sim_manager_step(SimManager *mgr);
// Libera memoria y limpia punteros asociados al administrador de simulación.
void sim_manager_free(SimManager *mgr);

#endif

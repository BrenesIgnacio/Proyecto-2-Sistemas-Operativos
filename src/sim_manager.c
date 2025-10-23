#include "sim_manager.h"
#include "sim_engine.h"
#include "util.h"

#include <string.h>

// Función auxiliar para reasignar memoria de forma segura
// Si falla la reasignación, termina el programa con un error
static void *mgr_realloc(void *ptr, size_t size) {
    void *tmp = realloc(ptr, size);
    if (!tmp && size != 0) {
        fprintf(stderr, "Out of memory (sim_manager realloc)\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

// Estructura temporal para rastrear punteros durante el preprocesamiento
// Almacena las páginas asociadas a cada puntero antes de la simulación
typedef struct PrePtrEntry {
    int valid;              // Indica si la entrada es válida
    uint32_t num_pages;     // Número de páginas que ocupa este puntero
    sim_pageid_t *pages;    // Array de IDs de páginas asociadas
} PrePtrEntry;

// Estructura temporal para rastrear procesos durante el preprocesamiento
// Mantiene una lista de punteros activos por proceso
typedef struct PreProcessEntry {
    int alive;              // Indica si el proceso está vivo
    sim_ptr_t *ptrs;        // Array de IDs de punteros del proceso
    size_t count;           // Cantidad de punteros activos
    size_t capacity;        // Capacidad del array de punteros
} PreProcessEntry;

// Libera la memoria del dataset de usos futuros (usado para el algoritmo OPT)
// Recorre todas las entradas y libera sus arrays de posiciones
static void free_future_dataset(FutureUseDataset *dataset) {
    if (!dataset || !dataset->entries) {
        return;
    }
    for (size_t i = 0; i < dataset->capacity; ++i) {
        free(dataset->entries[i].positions);
        dataset->entries[i].positions = NULL;
        dataset->entries[i].count = 0;
        dataset->entries[i].capacity = 0;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->capacity = 0;
}

// Asegura que haya suficiente capacidad en el array de eventos
// Duplica la capacidad cuando es necesario (crecimiento exponencial)
static void ensure_event_capacity(SimManager *mgr, size_t needed) {
    if (needed <= mgr->event_capacity) {
        return;
    }
    size_t new_capacity = mgr->event_capacity ? mgr->event_capacity * 2 : 128;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    mgr->events = mgr_realloc(mgr->events, new_capacity * sizeof(AccessEvent));
    mgr->event_capacity = new_capacity;
}

// Agrega un nuevo evento de acceso a página al registro
// Cada evento vincula una instrucción con una página específica
static void append_event(SimManager *mgr, size_t instr_index, sim_pageid_t page_id) {
    ensure_event_capacity(mgr, mgr->event_count + 1);
    mgr->events[mgr->event_count].instruction_index = instr_index;
    mgr->events[mgr->event_count].page_id = page_id;
    mgr->event_count++;
}

// Asegura que la tabla de punteros tenga capacidad para almacenar el ptr_id dado
// Expande la tabla dinámicamente e inicializa nuevas entradas
static void ensure_ptr_entry_capacity(PrePtrEntry **table, size_t *capacity, sim_ptr_t ptr_id) {
    size_t needed = (size_t)ptr_id + 1;
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = *capacity ? *capacity * 2 : 128;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *table = mgr_realloc(*table, new_capacity * sizeof(PrePtrEntry));
    // Inicializa las nuevas entradas
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*table)[i].valid = 0;
        (*table)[i].num_pages = 0;
        (*table)[i].pages = NULL;
    }
    *capacity = new_capacity;
}

// Asegura que la tabla de procesos tenga capacidad para almacenar el pid dado
// Expande la tabla dinámicamente e inicializa nuevas entradas de proceso
static void ensure_process_entry_capacity(PreProcessEntry **table, size_t *capacity, sim_pid_t pid) {
    size_t needed = (size_t)pid + 1;
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = *capacity ? *capacity * 2 : 16;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *table = mgr_realloc(*table, new_capacity * sizeof(PreProcessEntry));
    // Inicializa las nuevas entradas de proceso
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*table)[i].alive = 0;
        (*table)[i].ptrs = NULL;
        (*table)[i].count = 0;
        (*table)[i].capacity = 0;
    }
    *capacity = new_capacity;
}

// Agrega un ID de puntero a la lista de punteros activos de un proceso
// Expande la lista si es necesario y marca el proceso como vivo
static void process_add_ptr_id(PreProcessEntry *proc, sim_ptr_t ptr_id) {
    if (!proc) {
        return;
    }
    if (proc->count == proc->capacity) {
        size_t new_capacity = proc->capacity ? proc->capacity * 2 : 4;
    proc->ptrs = mgr_realloc(proc->ptrs, new_capacity * sizeof(sim_ptr_t));
        proc->capacity = new_capacity;
    }
    proc->ptrs[proc->count++] = ptr_id;
    proc->alive = 1;
}

// Elimina un ID de puntero de la lista de punteros activos de un proceso
// Usa swap-and-pop para mantener eficiencia O(1)
static void process_remove_ptr_id(PreProcessEntry *proc, sim_ptr_t ptr_id) {
    if (!proc || proc->count == 0) {
        return;
    }
    for (size_t i = 0; i < proc->count; ++i) {
        if (proc->ptrs[i] == ptr_id) {
            // Mueve el último elemento a la posición actual (swap-and-pop)
            proc->ptrs[i] = proc->ptrs[proc->count - 1];
            proc->count--;
            return;
        }
    }
}

// Destruye y libera los recursos de una entrada de puntero
// Marca la entrada como inválida después de liberar la memoria
static void destroy_ptr_entry(PrePtrEntry *entry) {
    if (!entry || !entry->valid) {
        return;
    }
    free(entry->pages);
    entry->pages = NULL;
    entry->num_pages = 0;
    entry->valid = 0;
}

// Agrega una posición de uso futuro a la entrada de una página
// Utilizado para construir el dataset de predicción del algoritmo OPT
static void future_entry_append(FutureUseEntry *entry, size_t position) {
    if (entry->count == entry->capacity) {
        size_t new_capacity = entry->capacity ? entry->capacity * 2 : 4;
    entry->positions = mgr_realloc(entry->positions, new_capacity * sizeof(size_t));
        entry->capacity = new_capacity;
    }
    entry->positions[entry->count++] = position;
}

// Reduce el tamaño de memoria de las entradas de usos futuros
// Libera entradas vacías y ajusta la capacidad al tamaño real usado
static void shrink_future_entries(FutureUseDataset *dataset) {
    if (!dataset || !dataset->entries) {
        return;
    }
    for (size_t i = 0; i < dataset->capacity; ++i) {
        FutureUseEntry *entry = &dataset->entries[i];
        if (entry->count == 0) {
            // Libera entradas sin usos futuros
            free(entry->positions);
            entry->positions = NULL;
            entry->capacity = 0;
        } else if (entry->capacity > entry->count) {
            // Reduce al tamaño exacto necesario
            entry->positions = mgr_realloc(entry->positions, entry->count * sizeof(size_t));
            entry->capacity = entry->count;
        }
    }
}

// Construye el dataset de usos futuros para el algoritmo OPT
// Recorre todos los eventos y registra cuándo se usará cada página
static void build_future_dataset(SimManager *mgr, sim_pageid_t max_page_id) {
    free_future_dataset(&mgr->future_dataset);

    // Crea un array con una entrada por cada página posible
    size_t capacity = (size_t)max_page_id + 1;
    if (capacity == 0) {
        capacity = 1;
    }
    mgr->future_dataset.entries = xmalloc(capacity * sizeof(FutureUseEntry));
    mgr->future_dataset.capacity = capacity;
    for (size_t i = 0; i < capacity; ++i) {
        mgr->future_dataset.entries[i].positions = NULL;
        mgr->future_dataset.entries[i].count = 0;
        mgr->future_dataset.entries[i].capacity = 0;
    }

    // Recorre todos los eventos y registra las posiciones de acceso para cada página
    for (size_t idx = 0; idx < mgr->event_count; ++idx) {
        sim_pageid_t page_id = mgr->events[idx].page_id;
        if (page_id >= capacity) {
            continue;
        }
        FutureUseEntry *entry = &mgr->future_dataset.entries[page_id];
        future_entry_append(entry, idx);
    }

    // Optimiza el uso de memoria
    shrink_future_entries(&mgr->future_dataset);
}

// Precomputa todos los eventos de acceso a páginas analizando las instrucciones
// Simula la ejecución para determinar qué páginas se acceden en cada paso
static void precompute_events(SimManager *mgr) {
    mgr->event_count = 0;
    mgr->current_event_index = 0;

    // Tablas temporales para rastrear punteros y procesos durante el preprocesamiento
    PrePtrEntry *ptr_table = NULL;
    size_t ptr_capacity = 0;
    PreProcessEntry *proc_table = NULL;
    size_t proc_capacity = 0;

    sim_pageid_t next_page_id = 1;  // Contador de IDs de página

    // Limpia offsets anteriores si existen
    if (mgr->instr_event_offsets) {
        free(mgr->instr_event_offsets);
        mgr->instr_event_offsets = NULL;
    }

    // Procesa cada instrucción para generar eventos de acceso a páginas
    for (size_t i = 0; i < mgr->instr_count; ++i) {
        Instruction *ins = &mgr->instructions[i];
        switch (ins->type) {
            case INS_NEW: {  // Asignación de memoria (new)
                // Calcula cuántas páginas se necesitan para el tamaño solicitado
                size_t num_pages = (ins->size + PAGE_SIZE - 1) / PAGE_SIZE;
                if (num_pages == 0) {
                    num_pages = 1;
                }
                ensure_ptr_entry_capacity(&ptr_table, &ptr_capacity, ins->ptr_id);
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                destroy_ptr_entry(entry);  // Limpia si ya existía
                entry->pages = xmalloc(sizeof(sim_pageid_t) * num_pages);
                entry->num_pages = (uint32_t)num_pages;
                entry->valid = 1;
                // Crea y registra un evento de acceso para cada página del puntero
                for (size_t p = 0; p < num_pages; ++p) {
                    entry->pages[p] = next_page_id++;
                    append_event(mgr, i, entry->pages[p]);
                }

                // Asocia el puntero con el proceso propietario
                ensure_process_entry_capacity(&proc_table, &proc_capacity, ins->pid);
                process_add_ptr_id(&proc_table[ins->pid], ins->ptr_id);
                break;
            }
            case INS_USE: {  // Uso de memoria (use)
                if (ins->ptr_id >= ptr_capacity) {
                    break;
                }
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                if (!entry->valid) {
                    break;
                }
                // Registra un evento de acceso para cada página del puntero usado
                for (uint32_t p = 0; p < entry->num_pages; ++p) {
                    append_event(mgr, i, entry->pages[p]);
                }
                break;
            }
            case INS_DELETE: {  // Liberación de memoria (delete)
                if (ins->ptr_id >= ptr_capacity) {
                    break;
                }
                PrePtrEntry *entry = &ptr_table[ins->ptr_id];
                if (!entry->valid) {
                    break;
                }
                // Destruye la entrada del puntero y lo desvincula del proceso
                destroy_ptr_entry(entry);
                if (ins->pid < proc_capacity) {
                    process_remove_ptr_id(&proc_table[ins->pid], ins->ptr_id);
                }
                break;
            }
            case INS_KILL: {  // Terminación de proceso (kill)
                if (ins->pid >= proc_capacity) {
                    break;
                }
                PreProcessEntry *proc = &proc_table[ins->pid];
                if (!proc->alive) {
                    break;
                }
                for (size_t p = 0; p < proc->count; ++p) {
                    sim_ptr_t ptr_id = proc->ptrs[p];
                    if (ptr_id < ptr_capacity) {
                        destroy_ptr_entry(&ptr_table[ptr_id]);
                    }
                }
                free(proc->ptrs);
                proc->ptrs = NULL;
                proc->count = 0;
                proc->capacity = 0;
                proc->alive = 0;
                break;
            }
            default:
                break;
        }
    }

    // Limpia todas las entradas de la tabla de punteros
    for (size_t idx = 0; idx < ptr_capacity; ++idx) {
        destroy_ptr_entry(&ptr_table[idx]);
    }
    free(ptr_table);

    // Limpia la tabla de procesos
    if (proc_table) {
        for (size_t idx = 0; idx < proc_capacity; ++idx) {
            free(proc_table[idx].ptrs);
        }
    }
    free(proc_table);

    // Construye el dataset de usos futuros para el algoritmo OPT
    sim_pageid_t max_page_id = next_page_id ? (next_page_id - 1) : 0;
    build_future_dataset(mgr, max_page_id);

    // Construye el array de offsets que mapea cada instrucción a sus eventos
    // Permite búsqueda rápida de los eventos asociados a una instrucción
    mgr->instr_event_offsets = xmalloc((mgr->instr_count + 1) * sizeof(size_t));
    size_t evt_index = 0;
    for (size_t i = 0; i < mgr->instr_count; ++i) {
        mgr->instr_event_offsets[i] = evt_index;
        while (evt_index < mgr->event_count && mgr->events[evt_index].instruction_index == i) {
            ++evt_index;
        }
    }
    mgr->instr_event_offsets[mgr->instr_count] = evt_index;  // Sentinel
}

// Inicializa el administrador de simulación con las instrucciones y el algoritmo del usuario
// Crea dos simuladores: uno con OPT (óptimo) y otro con el algoritmo elegido por el usuario
void sim_manager_init(SimManager *mgr, Instruction *instrs, size_t count, AlgorithmType user_alg) {
    if (!mgr) {
        return;
    }

    // Inicializa la estructura a cero
    memset(mgr, 0, sizeof(*mgr));
    mgr->instructions = instrs;
    mgr->instr_count = count;
    mgr->current_index = 0;
    mgr->current_event_index = 0;
    mgr->running = 0;
    mgr->user_algorithm = user_alg;

    // Precomputa todos los eventos de acceso a páginas y construye el dataset de usos futuros
    precompute_events(mgr);

    // Crea el simulador con algoritmo OPT (óptimo) para comparación
    mgr->sim_opt = xmalloc(sizeof(Simulator));
    sim_init(mgr->sim_opt, "OPT", ALG_OPT);
    sim_set_future_dataset(mgr->sim_opt, &mgr->future_dataset);

    // Crea el simulador con el algoritmo seleccionado por el usuario
    mgr->sim_user = xmalloc(sizeof(Simulator));
    const char *user_name = "USER";
    sim_init(mgr->sim_user, user_name, user_alg);
    sim_set_future_dataset(mgr->sim_user, &mgr->future_dataset);
}

// Avanza la simulación un paso, procesando la siguiente instrucción
// Ejecuta la instrucción en ambos simuladores (OPT y usuario) para comparación
void sim_manager_step(SimManager *mgr) {
    if (!mgr || !mgr->sim_opt || !mgr->sim_user) {
        return;
    }
    if (mgr->current_index >= mgr->instr_count) {
        mgr->running = 0;
        return;
    }

    // Obtiene la instrucción actual
    Instruction *ins = &mgr->instructions[mgr->current_index];
    // Calcula el rango de eventos asociados a esta instrucción
    size_t event_start = mgr->instr_event_offsets ? mgr->instr_event_offsets[mgr->current_index] : mgr->current_event_index;
    size_t event_end = mgr->instr_event_offsets ? mgr->instr_event_offsets[mgr->current_index + 1] : event_start;

    // Procesa la instrucción en ambos simuladores
    sim_process_instruction(mgr->sim_opt, ins, (int)event_start);
    sim_process_instruction(mgr->sim_user, ins, (int)event_start);

    // Avanza al siguiente paso
    mgr->current_index++;
    mgr->current_event_index = event_end;

    // Punto de enganche para actualizar la interfaz de usuario (notificar observadores)
}

// Libera todos los recursos del administrador de simulación
// Debe llamarse al finalizar para evitar fugas de memoria
void sim_manager_free(SimManager *mgr) {
    if (!mgr) {
        return;
    }

    // Libera el array de eventos precomputados
    free(mgr->events);
    mgr->events = NULL;
    mgr->event_count = 0;
    mgr->event_capacity = 0;

    // Libera el array de offsets de eventos por instrucción
    free(mgr->instr_event_offsets);
    mgr->instr_event_offsets = NULL;

    // Libera el dataset de usos futuros
    free_future_dataset(&mgr->future_dataset);

    // Libera el simulador OPT
    if (mgr->sim_opt) {
        sim_free(mgr->sim_opt);
        free(mgr->sim_opt);
        mgr->sim_opt = NULL;
    }

    // Libera el simulador del usuario
    if (mgr->sim_user) {
        sim_free(mgr->sim_user);
        free(mgr->sim_user);
        mgr->sim_user = NULL;
    }

    // Reinicia todos los campos a valores seguros
    mgr->instructions = NULL;
    mgr->instr_count = 0;
    mgr->current_index = 0;
    mgr->current_event_index = 0;
    mgr->running = 0;
}

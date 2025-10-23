#ifndef UI_INIT_H
#define UI_INIT_H

#include <gtk/gtk.h>
#include "sim_manager.h"
#include "instr_parser.h"

typedef enum {
    RUN_STATE_IDLE = 0,
    RUN_STATE_RUNNING,
    RUN_STATE_PAUSED,
    RUN_STATE_STEP
} RunState;

typedef struct AppContext {
    GtkWidget *main_window;
    GtkWidget *root_box;
    GtkWidget *start_button;
    GtkWidget *pause_button;
    GtkWidget *step_button;
    GtkWidget *reset_button;
    GtkWidget *generate_button;
    GtkWidget *algorithm_selector;
    GtkWidget *status_label;
    GtkWidget *opt_stats_box;
    GtkWidget *user_stats_box;
    SimManager manager;
    Instruction *instructions;
    size_t instruction_count;
    guint tick_source;
    RunState run_state;
} AppContext;

// Inicializa GTK y prepara la estructura principal de la aplicación.
void ui_init(AppContext *app, int *argc, char ***argv);
// Entra al loop principal de GTK para mostrar la interfaz.
void ui_run(AppContext *app);

#endif

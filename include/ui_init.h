#ifndef UI_INIT_H
#define UI_INIT_H

#include <gtk/gtk.h>
#include "sim_manager.h"

typedef struct AppContext {
    GtkWidget *main_window;
    GtkWidget *start_button;
    GtkWidget *file_chooser;
    GtkWidget *algorithm_selector;
    GtkWidget *seed_entry;
    SimManager manager;
} AppContext;

// Inicializa GTK y prepara la estructura principal de la aplicación.
void ui_init(AppContext *app, int *argc, char ***argv);
// Entra al loop principal de GTK para mostrar la interfaz.
void ui_run(AppContext *app);

#endif

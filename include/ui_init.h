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

void ui_init(AppContext *app, int *argc, char ***argv);
void ui_build_main_window(AppContext *app);
void ui_run(AppContext *app);

#endif

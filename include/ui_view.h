#ifndef UI_VIEW_H
#define UI_VIEW_H

#include <gtk/gtk.h>

typedef struct AppContext AppContext;

// Construye la ventana principal con los widgets base de la aplicación.
void ui_view_build_main_window(AppContext *app);
// Prepara la ventana que mostrará el estado de la simulación.
void ui_view_build_simulation_window(AppContext *app);
void on_start_simulation_clicked(GtkButton *button, gpointer user_data);
void on_load_instructions_clicked(GtkButton *button, gpointer user_data);
void on_save_instructions_clicked(GtkButton *button, gpointer user_data);
void on_generate_instructions_clicked(GtkButton *button, gpointer user_data);
void ui_view_build_setup_window(AppContext *app);

#endif

#include "ui_init.h"

// Arranca GTK y deja lista la estructura de contexto de la aplicación.
void ui_init(AppContext *app, int *argc, char ***argv) {
    (void)app;
    gtk_init(argc, argv);
}

// Muestra la ventana principal y entra en el loop de eventos.
void ui_run(AppContext *app) {
    gtk_widget_show_all(app->main_window);
    gtk_main();
}

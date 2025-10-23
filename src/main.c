#include "ui_init.h"
#include "ui_view.h"

// Punto de entrada que arranca la interfaz gráfica del simulador.
int main(int argc, char **argv) {
    AppContext app;
    ui_init(&app, &argc, &argv);
    ui_view_build_main_window(&app);
    ui_run(&app);
    if (app.tick_source) {
        g_source_remove(app.tick_source);
        app.tick_source = 0;
    }
    sim_manager_free(&app.manager);
    free(app.instructions);
    app.instructions = NULL;
    app.instruction_count = 0;
    return 0;
}

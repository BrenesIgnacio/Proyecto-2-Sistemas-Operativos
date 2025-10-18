#include "ui_init.h"
#include "ui_view.h"

// Punto de entrada que arranca la interfaz gráfica del simulador.
int main(int argc, char **argv) {
    AppContext app;
    ui_init(&app, &argc, &argv);
    ui_view_build_main_window(&app);
    ui_run(&app);
    return 0;
}

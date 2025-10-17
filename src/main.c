#include "ui_init.h"

int main(int argc, char **argv) {
    AppContext app;
    ui_init(&app, &argc, &argv);
    ui_build_main_window(&app);
    ui_run(&app);
    return 0;
}

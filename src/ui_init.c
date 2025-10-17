#include "ui_init.h"

void ui_init(AppContext *app, int *argc, char ***argv) {
    gtk_init(argc, argv);
}

void ui_build_main_window(AppContext *app) {
    app->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->main_window), "Paging Simulator");
    gtk_window_set_default_size(GTK_WINDOW(app->main_window), 800, 600);

    g_signal_connect(app->main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
}

void ui_run(AppContext *app) {
    gtk_widget_show_all(app->main_window);
    gtk_main();
}

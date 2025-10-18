#include "ui_view.h"
#include "ui_init.h"

// Construye una barra superior con título y subtítulo para la ventana.
static GtkWidget *create_header_bar(void) {
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Paging Simulator");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Step 1 scaffolding");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    return header;
}

// Crea la ventana principal y coloca elementos básicos de la interfaz.
void ui_view_build_main_window(AppContext *app) {
    app->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app->main_window), 1024, 768);
    gtk_window_set_icon_name(GTK_WINDOW(app->main_window), "applications-system");

    GtkWidget *header = create_header_bar();
    gtk_window_set_titlebar(GTK_WINDOW(app->main_window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(app->main_window), root);

    GtkWidget *placeholder = gtk_label_new("Simulation UI placeholder)");
    gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0);
    gtk_box_pack_start(GTK_BOX(root), placeholder, TRUE, TRUE, 0);

    g_signal_connect(app->main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
}

//vista detallada de la simulación.
void ui_view_build_simulation_window(AppContext *app) {
    (void)app;
    // aqui va el gui de la sim.
}

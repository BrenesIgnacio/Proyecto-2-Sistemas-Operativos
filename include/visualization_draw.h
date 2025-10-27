#ifndef VISUALIZATION_DRAW_H
#define VISUALIZATION_DRAW_H

#include <gtk/gtk.h>
#include "sim_types.h"

// Callback que pinta el estado actual de la RAM en el lienzo GTK.
gboolean draw_ram_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data);
// Actualiza etiquetas de la interfaz con las estadísticas del simulador.
void update_stats_labels(GtkWidget *container, const Simulator *sim);
gboolean draw_ram_bar_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data);
void update_visual_stats(GtkWidget *container, const Simulator *sim);

#endif

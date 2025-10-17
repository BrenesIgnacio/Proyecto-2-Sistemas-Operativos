#ifndef VISUALIZATION_DRAW_H
#define VISUALIZATION_DRAW_H

#include <gtk/gtk.h>
#include "sim_types.h"

gboolean draw_ram_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data);
void update_stats_labels(GtkWidget *container, const Simulator *sim);

#endif

#include "visualization_draw.h"

gboolean draw_ram_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)cr; (void)user_data;
    return FALSE;
}

void update_stats_labels(GtkWidget *container, const Simulator *sim) {
    (void)container; (void)sim;
}

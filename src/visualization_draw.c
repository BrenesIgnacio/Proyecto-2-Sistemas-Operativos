#include "visualization_draw.h"

// Dibuja el estado visual de la RAM en el canvas.
gboolean draw_ram_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)cr; (void)user_data;
    return FALSE;
}

// Actualiza las etiquetas del contenedor con métricas actuales del simulador.
void update_stats_labels(GtkWidget *container, const Simulator *sim) {
    (void)container; (void)sim;
}

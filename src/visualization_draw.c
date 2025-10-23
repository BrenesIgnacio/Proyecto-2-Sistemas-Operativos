#include "visualization_draw.h"

#include <stdarg.h>

static GtkWidget *lookup_label(GtkWidget *container, const char *key);
static void set_label(GtkWidget *container, const char *key, const char *value);
static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...);
static const char *algorithm_name(AlgorithmType type);

// Dibuja el estado visual de la RAM en el canvas.
gboolean draw_ram_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    (void)cr;
    (void)user_data;
    return FALSE;
}

static GtkWidget *lookup_label(GtkWidget *container, const char *key) {
    if (!container || !GTK_IS_WIDGET(container) || !key) {
        return NULL;
    }
    gpointer stored = g_object_get_data(G_OBJECT(container), key);
    if (!stored) {
        return NULL;
    }
    GtkWidget *label = GTK_WIDGET(stored);
    if (!GTK_IS_LABEL(label)) {
        return NULL;
    }
    return label;
}

static void set_label(GtkWidget *container, const char *key, const char *value) {
    GtkWidget *label = lookup_label(container, key);
    if (!label) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(label), value ? value : "--");
}

static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...) {
    GtkWidget *label = lookup_label(container, key);
    if (!label || !fmt) {
        return;
    }
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    g_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    gtk_label_set_text(GTK_LABEL(label), buffer);
}

static const char *algorithm_name(AlgorithmType type) {
    switch (type) {
        case ALG_OPT: return "OPT";
        case ALG_FIFO: return "FIFO";
        case ALG_SC: return "Second Chance";
        case ALG_MRU: return "MRU";
        case ALG_RND: return "Random";
        default: return "Unknown";
    }
}

// Actualiza las etiquetas del contenedor con métricas actuales del simulador.
void update_stats_labels(GtkWidget *container, const Simulator *sim) {
    if (!container) {
        return;
    }

    if (!sim) {
        set_label(container, "stat::name", "--");
        set_label(container, "stat::algorithm", "--");
        set_label(container, "stat::clock", "0");
        set_label(container, "stat::thrashing", "0");
        set_label(container, "stat::swap", "0");
        set_label(container, "stat::total_instr", "0");
        set_label(container, "stat::faults", "0");
        set_label(container, "stat::hits", "0");
        set_label(container, "stat::pages_created", "0");
        set_label(container, "stat::evicted", "0");
        set_label(container, "stat::ptr_alloc", "0");
        set_label(container, "stat::ptr_delete", "0");
        set_label(container, "stat::bytes", "0");
        set_label(container, "stat::fragment", "0");
        return;
    }

    set_label(container, "stat::name", sim->name);
    set_label(container, "stat::algorithm", algorithm_name(sim->algorithm));
    set_label_fmt(container, "stat::clock", "%llu", (unsigned long long)sim->clock);
    set_label_fmt(container, "stat::thrashing", "%llu", (unsigned long long)sim->thrashing_time);
    set_label_fmt(container, "stat::swap", "%zu", sim->total_pages_in_swap);

    set_label_fmt(container, "stat::total_instr", "%zu", sim->stats.total_instructions);
    set_label_fmt(container, "stat::faults", "%zu", sim->stats.page_faults);
    set_label_fmt(container, "stat::hits", "%zu", sim->stats.page_hits);
    set_label_fmt(container, "stat::pages_created", "%zu", sim->stats.pages_created);
    set_label_fmt(container, "stat::evicted", "%zu", sim->stats.pages_evicted);
    set_label_fmt(container, "stat::ptr_alloc", "%zu", sim->stats.ptr_allocations);
    set_label_fmt(container, "stat::ptr_delete", "%zu", sim->stats.ptr_deletions);
    set_label_fmt(container, "stat::bytes", "%zu", sim->stats.bytes_requested);
    set_label_fmt(container, "stat::fragment", "%zu", sim->internal_fragmentation_bytes);
}

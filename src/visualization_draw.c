#include "visualization_draw.h"

#include <stdarg.h>
#include <cairo.h>
#include <math.h>
#include <stdio.h>

#define RAM_COLS 10
#define RAM_ROWS 10

static GtkWidget *lookup_label(GtkWidget *container, const char *key);
static void set_label(GtkWidget *container, const char *key, const char *value);
static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...);
static const char *algorithm_name(AlgorithmType type);

gboolean draw_ram_bar_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    Simulator *sim = (Simulator *)user_data;
    if (!sim)
        return FALSE;

    double w = gtk_widget_get_allocated_width(widget);
    double h = gtk_widget_get_allocated_height(widget);
    if (w <= 0 || h <= 0)
        return FALSE;

    double cw = w / (double)RAM_FRAMES;
    cairo_set_font_size(cr, fmax(8.0, h * 0.4));

    for (int i = 0; i < RAM_FRAMES; ++i)
    {
        double x = i * cw;
        double y = 0;
        Frame *f = &sim->mmu.frames[i];

        if (f->occupied)
        {
            // color según PID (o page_id si lo prefieres)
            double hue = (f->page_id % 15) / 15.0;
            double rC = 0.45 + 0.45 * sin(6.28 * hue);
            double gC = 0.45 + 0.45 * sin(6.28 * (hue + 0.33));
            double bC = 0.45 + 0.45 * sin(6.28 * (hue + 0.66));
            cairo_set_source_rgb(cr, rC, gC, bC);
        }
        else
        {
            cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
        }

        cairo_rectangle(cr, x, y, cw, h);
        cairo_fill(cr);
    }

    return FALSE;
}

void update_visual_stats(GtkWidget *container, const Simulator *sim)
{
    if (!container || !sim)
        return;

    char buf[64];

    double ram_kb = sim->mmu.page_count * (PAGE_SIZE / 1024.0);
    double ram_total_kb = (RAM_FRAMES * PAGE_SIZE) / 1024.0;
    double ram_percent = (ram_kb / ram_total_kb) * 100.0;

    double vram_kb = sim->total_pages_in_swap * (PAGE_SIZE / 1024.0);
    double vram_percent = ram_total_kb > 0 ? (vram_kb / ram_total_kb) * 100.0 : 0.0;

    double thrash_percent = (sim->clock > 0) ? ((double)sim->thrashing_time / sim->clock) * 100.0 : 0.0;

    set_label_fmt(container, "stat::processes", "%zu", sim->process_count);
    set_label_fmt(container, "stat::clock", "%llu", (unsigned long long)sim->clock);

    snprintf(buf, sizeof(buf), "%.1f KB (%.1f%%)", ram_kb, ram_percent);
    set_label(container, "stat::ram", buf);

    snprintf(buf, sizeof(buf), "%.1f KB (%.1f%%)", vram_kb, vram_percent);
    set_label(container, "stat::vram", buf);

    set_label_fmt(container, "stat::loaded", "%zu", sim->stats.page_hits);
    set_label_fmt(container, "stat::unloaded", "%zu", sim->stats.page_faults);

    // thrashing con color
    GtkWidget *thr_label = lookup_label(container, "stat::thrashing");
    if (thr_label)
    {
        char thr_text[64];
        snprintf(thr_text, sizeof(thr_text), "%llu (%.1f%%)",
                 (unsigned long long)sim->thrashing_time, thrash_percent);
        gtk_label_set_text(GTK_LABEL(thr_label), thr_text);

        if (thrash_percent > 50.0)
            gtk_widget_override_color(thr_label, GTK_STATE_FLAG_NORMAL, &(GdkRGBA){1, 0, 0, 1});
        else
            gtk_widget_override_color(thr_label, GTK_STATE_FLAG_NORMAL, NULL);
    }

    snprintf(buf, sizeof(buf), "%zu B", sim->internal_fragmentation_bytes);
    set_label(container, "stat::fragment", buf);
}

static GtkWidget *lookup_label(GtkWidget *container, const char *key)
{
    if (!container || !GTK_IS_WIDGET(container) || !key)
    {
        return NULL;
    }
    gpointer stored = g_object_get_data(G_OBJECT(container), key);
    if (!stored)
    {
        return NULL;
    }
    GtkWidget *label = GTK_WIDGET(stored);
    if (!GTK_IS_LABEL(label))
    {
        return NULL;
    }
    return label;
}

static void set_label(GtkWidget *container, const char *key, const char *value)
{
    GtkWidget *label = lookup_label(container, key);
    if (!label)
    {
        return;
    }
    gtk_label_set_text(GTK_LABEL(label), value ? value : "--");
}

static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...)
{
    GtkWidget *label = lookup_label(container, key);
    if (!label || !fmt)
    {
        return;
    }
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    g_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    gtk_label_set_text(GTK_LABEL(label), buffer);
}

static const char *algorithm_name(AlgorithmType type)
{
    switch (type)
    {
    case ALG_OPT:
        return "OPT";
    case ALG_FIFO:
        return "FIFO";
    case ALG_SC:
        return "Second Chance";
    case ALG_MRU:
        return "MRU";
    case ALG_RND:
        return "Random";
    default:
        return "Unknown";
    }
}

// Actualiza las etiquetas del contenedor con métricas actuales del simulador.
void update_stats_labels(GtkWidget *container, const Simulator *sim)
{
    if (!container)
    {
        return;
    }

    if (!sim)
    {
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

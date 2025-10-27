#include "visualization_draw.h"

#include <stdarg.h>
#include <cairo.h>
#include <math.h>
#include <stdio.h>

#define RAM_COLS 10
#define RAM_ROWS 10

// --- Prototipos internos ---
static GtkWidget *lookup_label(GtkWidget *container, const char *key);
static void set_label(GtkWidget *container, const char *key, const char *value);
static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...);
static const char *algorithm_name(AlgorithmType type);
static void apply_label_color(GtkWidget *label, double r, double g, double b);

// --- Utilidad para color de PID ---
void pid_to_color(sim_pid_t pid, double *r, double *g, double *b)
{
    if (pid == 0)
    {
        *r = *g = *b = 0.8;
        return;
    }
    double hue = (pid * 97 % 360) / 360.0;
    *r = 0.5 + 0.5 * sin(6.283 * hue);
    *g = 0.5 + 0.5 * sin(6.283 * (hue + 0.33));
    *b = 0.5 + 0.5 * sin(6.283 * (hue + 0.66));
}

// --- Dibujo de barra RAM ---
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
        Frame *f = &sim->mmu.frames[i];

        if (f->occupied)
        {
            // Color según PID del propietario
            Page *page = NULL;
            for (size_t j = 0; j < sim->mmu.page_count; ++j)
            {
                if (sim->mmu.pages[j] && sim->mmu.pages[j]->id == f->page_id)
                {
                    page = sim->mmu.pages[j];
                    break;
                }
            }

            double rC = 0.4, gC = 0.4, bC = 0.4;
            if (page)
                pid_to_color(page->owner_pid, &rC, &gC, &bC);
            cairo_set_source_rgb(cr, rC, gC, bC);
        }
        else
        {
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.9); // gris claro para libre
        }

        cairo_rectangle(cr, x, 0, cw, h);
        cairo_fill(cr);
    }

    return FALSE;
}

// --- Cambiar color de texto con CSS moderno ---
static void apply_label_color(GtkWidget *label, double r, double g, double b)
{
    if (!GTK_IS_WIDGET(label))
        return;

    GtkStyleContext *context = gtk_widget_get_style_context(label);
    gchar *css = g_strdup_printf("label { color: rgb(%d,%d,%d); }",
                                 (int)(r * 255), (int)(g * 255), (int)(b * 255));
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(context,
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    g_free(css);
}

// --- Actualiza métricas visuales ---
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

    double thrash_percent = (sim->clock > 0)
                                ? ((double)sim->thrashing_time / sim->clock) * 100.0
                                : 0.0;

    set_label_fmt(container, "stat::processes", "%zu", sim->process_count);
    set_label_fmt(container, "stat::clock", "%llu", (unsigned long long)sim->clock);

    snprintf(buf, sizeof(buf), "%.1f KB (%.1f%%)", ram_kb, ram_percent);
    set_label(container, "stat::ram", buf);

    snprintf(buf, sizeof(buf), "%.1f KB (%.1f%%)", vram_kb, vram_percent);
    set_label(container, "stat::vram", buf);

    set_label_fmt(container, "stat::loaded", "%zu", sim->stats.page_hits);
    set_label_fmt(container, "stat::unloaded", "%zu", sim->stats.page_faults);

    // thrashing en color si > 50%
    GtkWidget *thr_label = lookup_label(container, "stat::thrashing");
    if (thr_label)
    {
        char thr_text[64];
        snprintf(thr_text, sizeof(thr_text), "%llu (%.1f%%)",
                 (unsigned long long)sim->thrashing_time, thrash_percent);
        gtk_label_set_text(GTK_LABEL(thr_label), thr_text);

        if (thrash_percent > 50.0)
            apply_label_color(thr_label, 1.0, 0.0, 0.0);
        else
            apply_label_color(thr_label, 0.0, 0.0, 0.0);
    }

    snprintf(buf, sizeof(buf), "%zu B", sim->internal_fragmentation_bytes);
    set_label(container, "stat::fragment", buf);
}

// --- Tabla de páginas ---
GtkWidget *create_page_table(const Simulator *sim)
{
    if (!sim)
        return gtk_label_new("No data");

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, FALSE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_size_request(scrolled, -1, 180);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_add(GTK_CONTAINER(scrolled), grid);

    const char *headers[] = {"PAGE ID", "PID", "LOADED", "L-ADDR", "M-ADDR",
                             "D-ADDR", "LOADED-T", "MARK"};
    for (int c = 0; c < 8; ++c)
    {
        GtkWidget *lbl = gtk_label_new(headers[c]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), lbl, c, 0, 1, 1);
    }

    for (size_t i = 0; i < sim->mmu.page_count; ++i)
    {
        Page *p = sim->mmu.pages[i];
        if (!p)
            continue;

        char buf[64];
        double rC, gC, bC;
        pid_to_color(p->owner_pid, &rC, &gC, &bC);

        // PAGE ID con color
        snprintf(buf, sizeof(buf), "%u", p->id);
        GtkWidget *lbl_pid = gtk_label_new(buf);
        apply_label_color(lbl_pid, rC, gC, bC);
        gtk_grid_attach(GTK_GRID(grid), lbl_pid, 0, (gint)(i + 1), 1, 1);

        // PID
        snprintf(buf, sizeof(buf), "%u", p->owner_pid);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(buf), 1, (gint)(i + 1), 1, 1);

        // LOADED
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(p->in_ram ? "X" : ""),
                        2, (gint)(i + 1), 1, 1);

        // L-ADDR
        snprintf(buf, sizeof(buf), "%u", p->page_index);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(buf), 3, (gint)(i + 1), 1, 1);

        // M-ADDR (frame)
        snprintf(buf, sizeof(buf), "%d", p->frame_index);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(buf), 4, (gint)(i + 1), 1, 1);

        // D-ADDR
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(p->in_ram ? "-" : "SWAP"),
                        5, (gint)(i + 1), 1, 1);

        // LOADED-T
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)p->last_used);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(buf), 6, (gint)(i + 1), 1, 1);

        // MARK
        gtk_grid_attach(GTK_GRID(grid),
                        gtk_label_new(p->ref_bit ? "1" : "0"), 7, (gint)(i + 1), 1, 1);
    }

    gtk_widget_show_all(scrolled);
    return scrolled;
}

// --- Funciones auxiliares ---
static GtkWidget *lookup_label(GtkWidget *container, const char *key)
{
    if (!container || !GTK_IS_WIDGET(container) || !key)
        return NULL;
    gpointer stored = g_object_get_data(G_OBJECT(container), key);
    if (!stored)
        return NULL;
    GtkWidget *label = GTK_WIDGET(stored);
    return GTK_IS_LABEL(label) ? label : NULL;
}

static void set_label(GtkWidget *container, const char *key, const char *value)
{
    GtkWidget *label = lookup_label(container, key);
    if (label)
        gtk_label_set_text(GTK_LABEL(label), value ? value : "--");
}

static void set_label_fmt(GtkWidget *container, const char *key, const char *fmt, ...)
{
    GtkWidget *label = lookup_label(container, key);
    if (!label || !fmt)
        return;
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

// --- Actualiza etiquetas de métricas ---
void update_stats_labels(GtkWidget *container, const Simulator *sim)
{
    if (!container)
        return;

    if (!sim)
    {
        const char *keys[] = {"stat::name", "stat::algorithm", "stat::clock",
                              "stat::thrashing", "stat::swap", "stat::total_instr",
                              "stat::faults", "stat::hits", "stat::pages_created",
                              "stat::evicted", "stat::ptr_alloc", "stat::ptr_delete",
                              "stat::bytes", "stat::fragment"};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            set_label(container, keys[i], "--");
        return;
    }

    set_label(container, "stat::name", sim->name);
    set_label(container, "stat::algorithm", algorithm_name(sim->algorithm));
    set_label_fmt(container, "stat::clock", "%llu", (unsigned long long)sim->clock);
    set_label_fmt(container, "stat::thrashing", "%llu",
                  (unsigned long long)sim->thrashing_time);
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

#include "ui_view.h"
#include "ui_init.h"
#include "config.h"
#include "instr_parser.h"
#include "visualization_draw.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TICK_MS 40

typedef struct {
    const char *label;
    const char *key;
} StatRowDesc;

static const StatRowDesc kStatRows[] = {
    { "Simulator", "stat::name" },
    { "Algorithm", "stat::algorithm" },
    { "Clock", "stat::clock" },
    { "Thrashing Time", "stat::thrashing" },
    { "Pages in Swap", "stat::swap" },
    { "Instructions", "stat::total_instr" },
    { "Page Faults", "stat::faults" },
    { "Page Hits", "stat::hits" },
    { "Pages Created", "stat::pages_created" },
    { "Pages Evicted", "stat::evicted" },
    { "Ptr Allocations", "stat::ptr_alloc" },
    { "Ptr Deletions", "stat::ptr_delete" },
    { "Bytes Requested", "stat::bytes" },
    { "Internal Fragmentation", "stat::fragment" }
};

static GtkWidget *create_header_bar(void);
static GtkWidget *create_stats_grid(void);
static GtkWidget *create_stats_frame(const char *title, GtkWidget **grid_out);
static void on_main_window_destroy(GtkWidget *widget, gpointer user_data);
static void on_generate_clicked(GtkButton *button, gpointer user_data);
static void on_start_clicked(GtkButton *button, gpointer user_data);
static void on_pause_clicked(GtkButton *button, gpointer user_data);
static void on_step_clicked(GtkButton *button, gpointer user_data);
static void on_reset_clicked(GtkButton *button, gpointer user_data);
static gboolean tick_simulation(gpointer user_data);
static void stop_simulation_timer(AppContext *app);
static void update_status(AppContext *app, const char *fmt, ...);
static void refresh_stats(AppContext *app);
static gboolean refresh_stats_idle(gpointer user_data);
static void update_controls(AppContext *app);
static void set_run_state(AppContext *app, RunState state);
static const char *algorithm_name(AlgorithmType type);
static AlgorithmType get_selected_algorithm(const AppContext *app);
static gboolean ensure_manager_config(AppContext *app, AlgorithmType alg, gboolean reset_position);
static void update_status_progress(AppContext *app, const char *prefix, size_t current, size_t total);

// Construye la barra superior con título y subtítulo.
static GtkWidget *create_header_bar(void) {
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Paging Simulator");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Comparación OPT vs algoritmo elegido");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    return header;
}

// Genera una grilla de métricas y registra los labels de valores en los data keys.
static GtkWidget *create_stats_grid(void) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    for (guint i = 0; i < G_N_ELEMENTS(kStatRows); ++i) {
        GtkWidget *label = gtk_label_new(kStatRows[i].label);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, (gint)i, 1, 1);

        GtkWidget *value = gtk_label_new("--");
        gtk_widget_set_halign(value, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid), value, 1, (gint)i, 1, 1);

        g_object_set_data(G_OBJECT(grid), kStatRows[i].key, value);
    }

    return grid;
}

// Crea un frame con la grilla de métricas y lo devuelve.
static GtkWidget *create_stats_frame(const char *title, GtkWidget **grid_out) {
    GtkWidget *frame = gtk_frame_new(title);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 8);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);

    GtkWidget *grid = create_stats_grid();
    gtk_container_add(GTK_CONTAINER(frame), grid);

    if (grid_out) {
        *grid_out = grid;
    }
    return frame;
}

static void update_status(AppContext *app, const char *fmt, ...) {
    if (!app || !GTK_IS_LABEL(app->status_label)) {
        return;
    }

    if (!fmt) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Idle");
        return;
    }

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    g_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    gtk_label_set_text(GTK_LABEL(app->status_label), buffer);
}

static void stop_simulation_timer(AppContext *app) {
    if (!app) {
        return;
    }
    if (app->tick_source) {
        g_source_remove(app->tick_source);
        app->tick_source = 0;
    }
}

static void refresh_stats(AppContext *app) {
    if (!app) {
        return;
    }
    g_idle_add(refresh_stats_idle, app);
}

static gboolean refresh_stats_idle(gpointer user_data) {
    AppContext *app = user_data;
    if (!app) {
        return G_SOURCE_REMOVE;
    }
    if (app->opt_stats_box) {
        update_stats_labels(app->opt_stats_box, app->manager.sim_opt);
    }
    if (app->user_stats_box) {
        update_stats_labels(app->user_stats_box, app->manager.sim_user);
    }
    return G_SOURCE_REMOVE;
}

static void update_controls(AppContext *app) {
    if (!app) {
        return;
    }

    gboolean has_workload = app->instruction_count > 0;
    gboolean finished = (app->manager.instr_count > 0 &&
                         app->manager.current_index >= app->manager.instr_count);

    if (app->start_button) {
        gboolean allow_start = (app->run_state != RUN_STATE_RUNNING) && has_workload;
        gtk_widget_set_sensitive(app->start_button, allow_start);
    }

    if (app->pause_button) {
        gboolean can_pause = (app->run_state == RUN_STATE_RUNNING);
        gboolean can_resume = (app->run_state == RUN_STATE_PAUSED || app->run_state == RUN_STATE_STEP) && !finished;
        gtk_widget_set_sensitive(app->pause_button, can_pause || can_resume);
        if (GTK_IS_BUTTON(app->pause_button)) {
            const char *label = "Pausar";
            if (app->run_state == RUN_STATE_PAUSED || app->run_state == RUN_STATE_STEP) {
                label = "Continuar";
            }
            gtk_button_set_label(GTK_BUTTON(app->pause_button), label);
        }
    }

    if (app->step_button) {
        gboolean can_step = has_workload && !finished && app->run_state != RUN_STATE_RUNNING;
        gtk_widget_set_sensitive(app->step_button, can_step);
    }

    if (app->generate_button) {
        gtk_widget_set_sensitive(app->generate_button, app->run_state != RUN_STATE_RUNNING);
    }

    if (app->algorithm_selector) {
        gtk_widget_set_sensitive(app->algorithm_selector, app->run_state == RUN_STATE_IDLE);
    }

    if (app->reset_button) {
        gtk_widget_set_sensitive(app->reset_button, has_workload || app->manager.sim_opt != NULL);
    }
}

static void set_run_state(AppContext *app, RunState state) {
    if (!app) {
        return;
    }
    app->run_state = state;
    update_controls(app);
}

static void on_pause_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppContext *app = user_data;
    if (!app) {
        return;
    }

    size_t current = app->manager.current_index;
    size_t total = app->manager.instr_count;

    switch (app->run_state) {
        case RUN_STATE_RUNNING:
            stop_simulation_timer(app);
            app->manager.running = 0;
            set_run_state(app, RUN_STATE_PAUSED);
            refresh_stats(app);
            update_status_progress(app, "Pausada.", current, total ? total : 0);
            break;
        case RUN_STATE_PAUSED:
        case RUN_STATE_STEP:
            if (total == 0 || current >= total) {
                update_status_progress(app, "Simulación completada.", total, total);
                set_run_state(app, RUN_STATE_IDLE);
                return;
            }
            app->manager.running = 1;
            set_run_state(app, RUN_STATE_RUNNING);
            update_status_progress(app, "En ejecución...", current, total);
            app->tick_source = g_timeout_add(DEFAULT_TICK_MS, tick_simulation, app);
            if (!app->tick_source) {
                app->manager.running = 0;
                set_run_state(app, RUN_STATE_PAUSED);
                update_status(app, "Error al reanudar el temporizador GTK.");
            }
            break;
        case RUN_STATE_IDLE:
        default:
            break;
    }
}

static const char *algorithm_name(AlgorithmType type) {
    switch (type) {
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

static AlgorithmType get_selected_algorithm(const AppContext *app) {
    if (!app || !GTK_IS_COMBO_BOX(app->algorithm_selector)) {
        return ALG_FIFO;
    }
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->algorithm_selector));
    if (!id) {
        return ALG_FIFO;
    }
    char *end = NULL;
    long value = strtol(id, &end, 10);
    AlgorithmType alg = (AlgorithmType)value;
    switch (alg) {
        case ALG_FIFO:
        case ALG_SC:
        case ALG_MRU:
        case ALG_RND:
            return alg;
        default:
            return ALG_FIFO;
    }
}

static void update_status_progress(AppContext *app, const char *prefix, size_t current, size_t total) {
    if (!app) {
        return;
    }

    const Simulator *opt = app->manager.sim_opt;
    const Simulator *user = app->manager.sim_user;

    unsigned long long opt_clock = opt ? (unsigned long long)opt->clock : 0ULL;
    unsigned long long user_clock = user ? (unsigned long long)user->clock : 0ULL;
    const char *user_alg = algorithm_name(app->manager.user_algorithm);

    const char *label = (prefix && *prefix) ? prefix : "Progreso";

    update_status(app, "%s %zu / %zu instrucciones | OPT t=%llu | %s t=%llu",
                  label,
                  current,
                  total,
                  opt_clock,
                  user_alg,
                  user_clock);
}

// Garantiza que el manager esté listo con el algoritmo y posición deseados.
static gboolean ensure_manager_config(AppContext *app, AlgorithmType alg, gboolean reset_position) {
    if (!app) {
        return FALSE;
    }
    if (!app->instructions || app->instruction_count == 0) {
        update_status(app, "Primero genera una carga de trabajo.");
        return FALSE;
    }

    gboolean needs_reset = reset_position;
    if (!app->manager.sim_opt || !app->manager.sim_user) {
        needs_reset = TRUE;
    } else if (app->manager.instructions != app->instructions ||
               app->manager.instr_count != app->instruction_count) {
        needs_reset = TRUE;
    } else if (app->manager.user_algorithm != alg) {
        needs_reset = TRUE;
    }

    if (needs_reset) {
        sim_manager_free(&app->manager);
        sim_manager_init(&app->manager, app->instructions, app->instruction_count, alg);
        app->manager.running = 0;
        refresh_stats(app);
        set_run_state(app, RUN_STATE_IDLE);
    }

    return TRUE;
}

static gboolean tick_simulation(gpointer user_data) {
    AppContext *app = user_data;
    if (!app) {
        return G_SOURCE_REMOVE;
    }

    if (app->run_state != RUN_STATE_RUNNING || !app->manager.running) {
        app->tick_source = 0;
        return G_SOURCE_REMOVE;
    }

    if (app->manager.current_index >= app->manager.instr_count) {
        app->manager.running = 0;
        app->tick_source = 0;
        set_run_state(app, RUN_STATE_IDLE);
        update_status_progress(app, "Simulación completada.", app->manager.instr_count, app->manager.instr_count);
        refresh_stats(app);
        return G_SOURCE_REMOVE;
    }

    sim_manager_step(&app->manager);
    refresh_stats(app);

    size_t current = app->manager.current_index;
    size_t total = app->manager.instr_count;

    if (current >= total) {
        app->manager.running = 0;
        app->tick_source = 0;
        set_run_state(app, RUN_STATE_IDLE);
        update_status_progress(app, "Simulación completada.", total, total);
        return G_SOURCE_REMOVE;
    }

    update_status_progress(app, "En ejecución...", current, total);
    return G_SOURCE_CONTINUE;
}

static void on_generate_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppContext *app = user_data;
    if (!app) {
        return;
    }

    stop_simulation_timer(app);
    app->manager.running = 0;
    sim_manager_free(&app->manager);
    set_run_state(app, RUN_STATE_IDLE);

    free(app->instructions);
    app->instructions = NULL;
    app->instruction_count = 0;

    Config cfg;
    config_load_defaults(&cfg);

    size_t count = 0;
    Instruction *list = generate_instructions(cfg.process_count, cfg.op_count, (unsigned int)cfg.seed, &count);
    if (!list || count == 0) {
        update_status(app, "No se pudo generar la carga de trabajo.");
        refresh_stats(app);
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }

    app->instructions = list;
    app->instruction_count = count;

    update_status(app, "Carga generada: %zu instrucciones (seed %u).", count, cfg.seed);
    refresh_stats(app);
    set_run_state(app, RUN_STATE_IDLE);
}

static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppContext *app = user_data;
    if (!app) {
        return;
    }

    stop_simulation_timer(app);

    AlgorithmType alg = get_selected_algorithm(app);
    if (!ensure_manager_config(app, alg, TRUE)) {
        return;
    }
    if (app->manager.instr_count == 0) {
        update_status(app, "No hay instrucciones para simular.");
        return;
    }

    app->manager.running = 1;
    refresh_stats(app);
    set_run_state(app, RUN_STATE_RUNNING);

    update_status_progress(app, "En ejecución...", 0, app->manager.instr_count);
    app->tick_source = g_timeout_add(DEFAULT_TICK_MS, tick_simulation, app);
    if (!app->tick_source) {
        app->manager.running = 0;
        set_run_state(app, RUN_STATE_IDLE);
        update_status(app, "Error al iniciar el temporizador GTK.");
    }
}

static void on_step_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppContext *app = user_data;
    if (!app) {
        return;
    }
    if (app->manager.running) {
        return;
    }

    stop_simulation_timer(app);
    app->manager.running = 0;

    AlgorithmType alg = get_selected_algorithm(app);
    if (!ensure_manager_config(app, alg, FALSE)) {
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }
    if (app->manager.instr_count == 0) {
        update_status(app, "No hay instrucciones para simular.");
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }
    if (app->manager.current_index >= app->manager.instr_count) {
        update_status_progress(app, "Simulación completada.", app->manager.instr_count, app->manager.instr_count);
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }

    sim_manager_step(&app->manager);
    refresh_stats(app);

    size_t current = app->manager.current_index;
    size_t total = app->manager.instr_count;
    if (current >= total) {
        set_run_state(app, RUN_STATE_IDLE);
        update_status_progress(app, "Simulación completada.", total, total);
    } else {
        set_run_state(app, RUN_STATE_STEP);
        update_status_progress(app, "Paso manual:", current, total);
    }
}

static void on_reset_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppContext *app = user_data;
    if (!app) {
        return;
    }

    stop_simulation_timer(app);
    app->manager.running = 0;
    sim_manager_free(&app->manager);
    refresh_stats(app);
    set_run_state(app, RUN_STATE_IDLE);
    update_status(app, "Simulación reiniciada.");
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppContext *app = user_data;
    if (app) {
        stop_simulation_timer(app);
        app->manager.running = 0;
    }
    gtk_main_quit();
}

// Construye la ventana principal con controles y ganchos básicos.
void ui_view_build_main_window(AppContext *app) {
    if (!app) {
        return;
    }

    app->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app->main_window), 1024, 768);
    gtk_window_set_icon_name(GTK_WINDOW(app->main_window), "applications-system");

    GtkWidget *header = create_header_bar();
    gtk_window_set_titlebar(GTK_WINDOW(app->main_window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_container_add(GTK_CONTAINER(app->main_window), root);
    app->root_box = root;

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);

    GtkWidget *algo_label = gtk_label_new("Algoritmo:");
    gtk_widget_set_halign(algo_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(controls), algo_label, FALSE, FALSE, 0);

    app->algorithm_selector = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "1", "FIFO");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "2", "Second Chance");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "3", "MRU");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "4", "Random");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->algorithm_selector), 0);
    gtk_box_pack_start(GTK_BOX(controls), app->algorithm_selector, FALSE, FALSE, 0);

    app->generate_button = gtk_button_new_with_label("Generar carga");
    gtk_box_pack_start(GTK_BOX(controls), app->generate_button, FALSE, FALSE, 0);

    app->start_button = gtk_button_new_with_label("Iniciar");
    gtk_box_pack_start(GTK_BOX(controls), app->start_button, FALSE, FALSE, 0);

    app->pause_button = gtk_button_new_with_label("Pausar");
    gtk_box_pack_start(GTK_BOX(controls), app->pause_button, FALSE, FALSE, 0);

    app->step_button = gtk_button_new_with_label("Step");
    gtk_box_pack_start(GTK_BOX(controls), app->step_button, FALSE, FALSE, 0);

    app->reset_button = gtk_button_new_with_label("Reset");
    gtk_box_pack_start(GTK_BOX(controls), app->reset_button, FALSE, FALSE, 0);

    app->status_label = gtk_label_new("Idle");
    gtk_widget_set_halign(app->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), app->status_label, FALSE, FALSE, 0);

    ui_view_build_simulation_window(app);

    g_signal_connect(app->main_window, "destroy", G_CALLBACK(on_main_window_destroy), app);
    g_signal_connect(app->generate_button, "clicked", G_CALLBACK(on_generate_clicked), app);
    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_clicked), app);
    g_signal_connect(app->pause_button, "clicked", G_CALLBACK(on_pause_clicked), app);
    g_signal_connect(app->step_button, "clicked", G_CALLBACK(on_step_clicked), app);
    g_signal_connect(app->reset_button, "clicked", G_CALLBACK(on_reset_clicked), app);

    set_run_state(app, RUN_STATE_IDLE);
    refresh_stats(app);
}

// Prepara los paneles de métricas de cada simulador.
void ui_view_build_simulation_window(AppContext *app) {
    if (!app || !app->root_box) {
        return;
    }

    GtkWidget *panels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(app->root_box), panels, TRUE, TRUE, 0);

    GtkWidget *opt_frame = create_stats_frame("OPT (Base)", &app->opt_stats_box);
    GtkWidget *user_frame = create_stats_frame("Algoritmo Usuario", &app->user_stats_box);

    gtk_box_pack_start(GTK_BOX(panels), opt_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(panels), user_frame, TRUE, TRUE, 0);
}

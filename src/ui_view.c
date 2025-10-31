#include "ui_view.h"
#include "ui_init.h"
#include "config.h"
#include "instr_parser.h"
#include "visualization_draw.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TICK_MS 40

typedef struct
{
    const char *label;
    const char *key;
} StatRowDesc;

static const StatRowDesc kStatRows[] = {
    {"Processes", "stat::processes"},
    {"Sim Time", "stat::clock"},
    {"RAM Usage", "stat::ram"},
    {"V-RAM Usage", "stat::vram"},
    {"Loaded Pages", "stat::loaded"},
    {"Unloaded Pages", "stat::unloaded"},
    {"Thrashing", "stat::thrashing"},
    {"Fragmentación Interna", "stat::fragment"}};

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

void on_load_instructions_clicked(GtkButton *button, gpointer user_data);
void on_save_instructions_clicked(GtkButton *button, gpointer user_data);
void on_generate_instructions_clicked(GtkButton *button, gpointer user_data);
void on_start_simulation_clicked(GtkButton *button, gpointer user_data);

static void disconnect_bars(AppContext *app)
{
    if (!app || !app->root_box)
        return;

    GtkWidget *opt_bar = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
    GtkWidget *user_bar = g_object_get_data(G_OBJECT(app->root_box), "user_bar");

    if (opt_bar)
        g_signal_handlers_disconnect_by_func(opt_bar, G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
    if (user_bar)
        g_signal_handlers_disconnect_by_func(user_bar, G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
}

// Construye la barra superior con título y subtítulo.
static GtkWidget *create_header_bar(void)
{
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Paging Simulator");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Comparación OPT vs algoritmo elegido");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    return header;
}

// Genera una grilla de métricas y registra los labels de valores en los data keys.
static GtkWidget *create_stats_grid(void)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    for (guint i = 0; i < G_N_ELEMENTS(kStatRows); ++i)
    {
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
static GtkWidget *create_stats_frame(const char *title, GtkWidget **grid_out)
{
    GtkWidget *frame = gtk_frame_new(title);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 8);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);

    GtkWidget *grid = create_stats_grid();
    gtk_container_add(GTK_CONTAINER(frame), grid);

    if (grid_out)
    {
        *grid_out = grid;
    }
    return frame;
}

static void update_status(AppContext *app, const char *fmt, ...)
{
    if (!app || !GTK_IS_LABEL(app->status_label))
    {
        return;
    }

    if (!fmt)
    {
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

static void stop_simulation_timer(AppContext *app)
{
    if (!app)
    {
        return;
    }
    if (app->tick_source)
    {
        g_source_remove(app->tick_source);
        app->tick_source = 0;
    }
}

static void refresh_stats(AppContext *app)
{
    if (!app)
    {
        return;
    }
    g_idle_add(refresh_stats_idle, app);
}

static gboolean refresh_stats_idle(gpointer user_data)
{
    AppContext *app = user_data;
    if (!app)
    {
        return G_SOURCE_REMOVE;
    }
    if (app->opt_stats_box)
    {
        update_stats_labels(app->opt_stats_box, app->manager.sim_opt);
    }
    if (app->user_stats_box)
    {
        update_stats_labels(app->user_stats_box, app->manager.sim_user);
    }
    if (app->opt_stats_box)
    {
        update_stats_labels(app->opt_stats_box, app->manager.sim_opt);
        update_visual_stats(app->opt_stats_box, app->manager.sim_opt);
    }
    if (app->user_stats_box)
    {
        update_stats_labels(app->user_stats_box, app->manager.sim_user);
        update_visual_stats(app->user_stats_box, app->manager.sim_user);
    }

    GtkWidget *tables_box = g_object_get_data(G_OBJECT(app->root_box), "tables_box");
    if (tables_box)
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(tables_box));
        for (GList *l = children; l != NULL; l = l->next)
            gtk_widget_destroy(GTK_WIDGET(l->data));
        g_list_free(children);

        GtkWidget *opt_table = create_page_table(app->manager.sim_opt);
        GtkWidget *user_table = create_page_table(app->manager.sim_user);
        gtk_box_pack_start(GTK_BOX(tables_box), opt_table, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(tables_box), user_table, TRUE, TRUE, 0);
        gtk_widget_show_all(tables_box);
    }

    gtk_widget_queue_draw(app->root_box);
    return G_SOURCE_REMOVE;
}

static void update_controls(AppContext *app)
{
    if (!app)
    {
        return;
    }

    gboolean has_workload = app->instruction_count > 0;
    gboolean finished = (app->manager.instr_count > 0 &&
                         app->manager.current_index >= app->manager.instr_count);

    if (app->start_button)
    {
        gboolean allow_start = (app->run_state != RUN_STATE_RUNNING) && has_workload;
        gtk_widget_set_sensitive(app->start_button, allow_start);
    }

    if (app->pause_button)
    {
        gboolean can_pause = (app->run_state == RUN_STATE_RUNNING);
        gboolean can_resume = (app->run_state == RUN_STATE_PAUSED || app->run_state == RUN_STATE_STEP) && !finished;
        gtk_widget_set_sensitive(app->pause_button, can_pause || can_resume);
        if (GTK_IS_BUTTON(app->pause_button))
        {
            const char *label = "Pausar";
            if (app->run_state == RUN_STATE_PAUSED || app->run_state == RUN_STATE_STEP)
            {
                label = "Continuar";
            }
            gtk_button_set_label(GTK_BUTTON(app->pause_button), label);
        }
    }

    if (app->step_button)
    {
        gboolean can_step = has_workload && !finished && app->run_state != RUN_STATE_RUNNING;
        gtk_widget_set_sensitive(app->step_button, can_step);
    }

    if (app->generate_button)
    {
        gtk_widget_set_sensitive(app->generate_button, app->run_state != RUN_STATE_RUNNING);
    }

    if (app->algorithm_selector)
    {
        gtk_widget_set_sensitive(app->algorithm_selector, app->run_state == RUN_STATE_IDLE);
    }

    if (app->reset_button)
    {
        gtk_widget_set_sensitive(app->reset_button, has_workload || app->manager.sim_opt != NULL);
    }
}

static void set_run_state(AppContext *app, RunState state)
{
    if (!app)
    {
        return;
    }
    app->run_state = state;
    update_controls(app);
}

static void on_pause_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppContext *app = user_data;
    if (!app)
    {
        return;
    }

    size_t current = app->manager.current_index;
    size_t total = app->manager.instr_count;

    switch (app->run_state)
    {
    case RUN_STATE_RUNNING:
        stop_simulation_timer(app);
        app->manager.running = 0;
        set_run_state(app, RUN_STATE_PAUSED);
        refresh_stats(app);
        update_status_progress(app, "Pausada.", current, total ? total : 0);
        break;
    case RUN_STATE_PAUSED:
    case RUN_STATE_STEP:
        if (total == 0 || current >= total)
        {
            update_status_progress(app, "Simulación completada.", total, total);
            set_run_state(app, RUN_STATE_IDLE);
            return;
        }
        app->manager.running = 1;
        set_run_state(app, RUN_STATE_RUNNING);
        update_status_progress(app, "En ejecución...", current, total);
        app->tick_source = g_timeout_add(DEFAULT_TICK_MS, tick_simulation, app);
        if (!app->tick_source)
        {
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
    case ALG_LRU:
        return "LRU";
    default:
        return "Unknown";
    }
}

static AlgorithmType get_selected_algorithm(const AppContext *app)
{
    if (!app || !GTK_IS_COMBO_BOX(app->algorithm_selector))
    {
        return ALG_FIFO;
    }
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->algorithm_selector));
    if (!id)
    {
        return ALG_FIFO;
    }
    char *end = NULL;
    long value = strtol(id, &end, 10);
    AlgorithmType alg = (AlgorithmType)value;
    switch (alg)
    {
    case ALG_FIFO:
    case ALG_SC:
    case ALG_LRU:
    case ALG_MRU:
    case ALG_RND:
        return alg;
    default:
        return ALG_FIFO;
    }
}

static void update_status_progress(AppContext *app, const char *prefix, size_t current, size_t total)
{
    if (!app)
    {
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
static gboolean ensure_manager_config(AppContext *app, AlgorithmType alg, gboolean reset_position)
{
    if (!app)
        return FALSE;

    if (!app->instructions || app->instruction_count == 0)
    {
        update_status(app, "Primero genera una carga de trabajo.");
        return FALSE;
    }

    gboolean needs_reset = reset_position;
    if (!app->manager.sim_opt || !app->manager.sim_user)
    {
        needs_reset = TRUE;
    }
    else if (app->manager.instructions != app->instructions ||
             app->manager.instr_count != app->instruction_count ||
             app->manager.user_algorithm != alg)
    {
        needs_reset = TRUE;
    }

    if (needs_reset)
    {
        disconnect_bars(app);
        sim_manager_free(&app->manager);
        app->manager.sim_opt = NULL;
        app->manager.sim_user = NULL;

        sim_manager_init(&app->manager, app->instructions, app->instruction_count, alg);
        app->manager.running = 0;

        // Reconectar barras a los nuevos simuladores
        GtkWidget *opt_bar = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
        GtkWidget *user_bar = g_object_get_data(G_OBJECT(app->root_box), "user_bar");
        if (opt_bar && user_bar)
        {
            g_signal_connect(opt_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
            g_signal_connect(user_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
        }

        refresh_stats(app);
        set_run_state(app, RUN_STATE_IDLE);
    }

    return TRUE;
}

static gboolean tick_simulation(gpointer user_data)
{
    AppContext *app = user_data;
    if (!app)
    {
        return G_SOURCE_REMOVE;
    }

    if (app->run_state != RUN_STATE_RUNNING || !app->manager.running)
    {
        app->tick_source = 0;
        return G_SOURCE_REMOVE;
    }

    if (app->manager.current_index >= app->manager.instr_count)
    {
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

    if (current >= total)
    {
        app->manager.running = 0;
        app->tick_source = 0;
        set_run_state(app, RUN_STATE_IDLE);
        update_status_progress(app, "Simulación completada.", total, total);
        return G_SOURCE_REMOVE;
    }

    update_status_progress(app, "En ejecución...", current, total);
    return G_SOURCE_CONTINUE;
}

static void on_generate_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppContext *app = user_data;
    if (!app)
        return;

    stop_simulation_timer(app);
    disconnect_bars(app);
    app->manager.running = 0;
    sim_manager_free(&app->manager);
    app->manager.sim_opt = NULL;
    app->manager.sim_user = NULL;
    set_run_state(app, RUN_STATE_IDLE);

    free(app->instructions);
    app->instructions = NULL;
    app->instruction_count = 0;

    size_t count = 0;
    Instruction *list = generate_instructions(app->process_count, app->operation_count, app->seed, &count);
    if (!list || count == 0)
    {
        update_status(app, "No se pudo generar la carga de trabajo.");
        refresh_stats(app);
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }

    app->instructions = list;
    app->instruction_count = count;

    // Reasignar simuladores y señales de barra
    sim_manager_init(&app->manager, app->instructions, app->instruction_count, app->manager.user_algorithm);
    GtkWidget *opt_bar = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
    GtkWidget *user_bar = g_object_get_data(G_OBJECT(app->root_box), "user_bar");
    if (opt_bar && user_bar)
    {
        g_signal_connect(opt_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
        g_signal_connect(user_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
    }

    update_status(app, "Carga generada: %zu instrucciones (seed %u).", count, app->seed);
    refresh_stats(app);
    set_run_state(app, RUN_STATE_IDLE);
}

static void on_start_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppContext *app = user_data;
    if (!app)
    {
        return;
    }

    stop_simulation_timer(app);

    AlgorithmType alg = get_selected_algorithm(app);
    if (!ensure_manager_config(app, alg, TRUE))
    {
        return;
    }
    if (app->manager.instr_count == 0)
    {
        update_status(app, "No hay instrucciones para simular.");
        return;
    }

    app->manager.running = 1;
    refresh_stats(app);
    set_run_state(app, RUN_STATE_RUNNING);

    update_status_progress(app, "En ejecución...", 0, app->manager.instr_count);
    app->tick_source = g_timeout_add(DEFAULT_TICK_MS, tick_simulation, app);
    if (!app->tick_source)
    {
        app->manager.running = 0;
        set_run_state(app, RUN_STATE_IDLE);
        update_status(app, "Error al iniciar el temporizador GTK.");
    }
}

static void on_step_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppContext *app = user_data;
    if (!app)
    {
        return;
    }
    if (app->manager.running)
    {
        return;
    }

    stop_simulation_timer(app);
    app->manager.running = 0;

    AlgorithmType alg = get_selected_algorithm(app);
    if (!ensure_manager_config(app, alg, FALSE))
    {
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }
    if (app->manager.instr_count == 0)
    {
        update_status(app, "No hay instrucciones para simular.");
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }
    if (app->manager.current_index >= app->manager.instr_count)
    {
        update_status_progress(app, "Simulación completada.", app->manager.instr_count, app->manager.instr_count);
        set_run_state(app, RUN_STATE_IDLE);
        return;
    }

    sim_manager_step(&app->manager);
    refresh_stats(app);

    size_t current = app->manager.current_index;
    size_t total = app->manager.instr_count;
    if (current >= total)
    {
        set_run_state(app, RUN_STATE_IDLE);
        update_status_progress(app, "Simulación completada.", total, total);
    }
    else
    {
        set_run_state(app, RUN_STATE_STEP);
        update_status_progress(app, "Paso manual:", current, total);
    }
}

static void on_reset_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppContext *app = user_data;
    if (!app)
    {
        return;
    }

    stop_simulation_timer(app);
    app->manager.running = 0;
    sim_manager_free(&app->manager);
    refresh_stats(app);
    set_run_state(app, RUN_STATE_IDLE);
    update_status(app, "Simulación reiniciada.");
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    AppContext *app = user_data;
    if (app)
    {
        stop_simulation_timer(app);
        app->manager.running = 0;
        app->main_window = NULL;
        app->root_box = NULL;
    }
    gtk_main_quit();
}

// Construye la ventana principal con controles y ganchos básicos.
void ui_view_build_main_window(AppContext *app)
{
    if (!app)
    {
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
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "3", "LRU");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "4", "MRU");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->algorithm_selector), "5", "Random");
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
void ui_view_build_simulation_window(AppContext *app)
{
    if (!app || !app->root_box)
        return;

    GtkWidget *sim_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(app->root_box), sim_box, TRUE, TRUE, 0);

    GtkWidget *label_opt = gtk_label_new("Memoria RAM - OPT (Algoritmo Base)");
    gtk_widget_set_halign(label_opt, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(sim_box), label_opt, FALSE, FALSE, 0);
    // Barra de RAM OPT
    GtkWidget *opt_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(opt_bar, -1, 32);
    gtk_box_pack_start(GTK_BOX(sim_box), opt_bar, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(app->root_box), "opt_bar", opt_bar);

    GtkWidget *label_user = gtk_label_new("Memoria RAM - Algoritmo seleccionado");
    gtk_widget_set_halign(label_user, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(sim_box), label_user, FALSE, FALSE, 0);
    // Barra de RAM USER
    GtkWidget *user_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(user_bar, -1, 32);
    gtk_box_pack_start(GTK_BOX(sim_box), user_bar, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(app->root_box), "user_bar", user_bar);

    // Tablas de páginas
    GtkWidget *tables_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(sim_box), tables_box, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(app->root_box), "tables_box", tables_box);

    GtkWidget *opt_table = create_page_table(app->manager.sim_opt);
    GtkWidget *user_table = create_page_table(app->manager.sim_user);
    gtk_box_pack_start(GTK_BOX(tables_box), opt_table, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tables_box), user_table, TRUE, TRUE, 0);

    GtkWidget *info_label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(sim_box), info_label_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_label_box), gtk_label_new("Estadísticas OPT"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(info_label_box), gtk_label_new("Estadísticas Algoritmo seleccionado"), TRUE, TRUE, 0);
    // Paneles de estadísticas
    GtkWidget *panels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(sim_box), panels, TRUE, TRUE, 0);

    GtkWidget *opt_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *opt_frame = create_stats_frame("OPT (Base)", &app->opt_stats_box);
    gtk_box_pack_start(GTK_BOX(opt_vbox), opt_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(panels), opt_vbox, TRUE, TRUE, 0);

    GtkWidget *user_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *user_frame = create_stats_frame("Algoritmo Usuario", &app->user_stats_box);
    gtk_box_pack_start(GTK_BOX(user_vbox), user_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(panels), user_vbox, TRUE, TRUE, 0);

    // Conectar señales de dibujo
    g_signal_connect(opt_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
    g_signal_connect(user_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
}

void ui_view_build_setup_window(AppContext *app)
{
    if (!app)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Configuración de simulación",
        NULL,
        GTK_DIALOG_MODAL,
        "_Cerrar", GTK_RESPONSE_CLOSE,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(content), grid);

    // Semilla
    GtkWidget *label_seed = gtk_label_new("Semilla (seed):");
    gtk_widget_set_halign(label_seed, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_seed, 0, 0, 1, 1);

    GtkWidget *entry_seed = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_seed), "1234 (por defecto)");
    gtk_grid_attach(GTK_GRID(grid), entry_seed, 1, 0, 1, 1);
    g_object_set_data(G_OBJECT(dialog), "entry_seed", entry_seed);

    // Spin para procesos (P)
    GtkWidget *label_p = gtk_label_new("Procesos (P):");
    gtk_widget_set_halign(label_p, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_p, 0, 1, 1, 1);

    GtkWidget *spin_p = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_p), 10);
    gtk_grid_attach(GTK_GRID(grid), spin_p, 1, 1, 1, 1);
    g_object_set_data(G_OBJECT(dialog), "spin_p", spin_p);

    // Spin para operaciones (N)
    GtkWidget *label_n = gtk_label_new("Operaciones (N):");
    gtk_widget_set_halign(label_n, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_n, 0, 2, 1, 1);

    GtkWidget *spin_n = gtk_spin_button_new_with_range(10, 10000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_n), 500);
    gtk_grid_attach(GTK_GRID(grid), spin_n, 1, 2, 1, 1);
    g_object_set_data(G_OBJECT(dialog), "spin_n", spin_n);

    // Botones
    GtkWidget *btn_generate = gtk_button_new_with_label("Generar instrucciones");
    gtk_grid_attach(GTK_GRID(grid), btn_generate, 0, 3, 2, 1);
    g_signal_connect(btn_generate, "clicked", G_CALLBACK(on_generate_instructions_clicked), dialog);

    GtkWidget *btn_load = gtk_button_new_with_label("Cargar archivo");
    gtk_grid_attach(GTK_GRID(grid), btn_load, 0, 4, 1, 1);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_load_instructions_clicked), dialog);

    GtkWidget *btn_save = gtk_button_new_with_label("Guardar archivo");
    gtk_grid_attach(GTK_GRID(grid), btn_save, 1, 4, 1, 1);
    gtk_widget_set_sensitive(btn_save, FALSE);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_instructions_clicked), dialog);

    GtkWidget *btn_start = gtk_button_new_with_label("Iniciar simulación");
    gtk_grid_attach(GTK_GRID(grid), btn_start, 0, 5, 2, 1);
    g_signal_connect(btn_start, "clicked", G_CALLBACK(on_start_simulation_clicked), dialog);

    g_object_set_data(G_OBJECT(dialog), "btn_generate", btn_generate);
    g_object_set_data(G_OBJECT(dialog), "btn_load", btn_load);
    g_object_set_data(G_OBJECT(dialog), "btn_save", btn_save);

    g_object_set_data(G_OBJECT(dialog), "app", app);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void on_generate_instructions_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWidget *dialog = GTK_WIDGET(user_data);
    AppContext *app = g_object_get_data(G_OBJECT(dialog), "app");

    GtkWidget *entry_seed = g_object_get_data(G_OBJECT(dialog), "entry_seed");
    GtkWidget *spin_p = g_object_get_data(G_OBJECT(dialog), "spin_p");
    GtkWidget *spin_n = g_object_get_data(G_OBJECT(dialog), "spin_n");

    unsigned int seed = 1234;
    const char *seed_text = gtk_entry_get_text(GTK_ENTRY(entry_seed));
    if (seed_text && *seed_text)
        seed = (unsigned int)atoi(seed_text);

    int P = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_p));
    int N = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_n));

    size_t count = 0;
    Instruction *list = generate_instructions(P, N, seed, &count);
    if (!list || count == 0)
    {
        GtkWidget *err = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK, "Error al generar las instrucciones.");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }
    app->seed = seed;
    app->process_count = P;
    app->operation_count = N;

    free(app->instructions);
    app->instructions = list;
    app->instruction_count = count;

    GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_OK,
                                            "Se generaron %zu instrucciones con semilla %u.", count, seed);
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
    GtkWidget *btn_save = g_object_get_data(G_OBJECT(dialog), "btn_save");
    if (btn_save)
        gtk_widget_set_sensitive(btn_save, TRUE);
}

void on_save_instructions_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;

    GtkWidget *dialog = GTK_WIDGET(user_data);
    AppContext *app = g_object_get_data(G_OBJECT(dialog), "app");

    GtkWidget *chooser = gtk_file_chooser_dialog_new("Guardar instrucciones",
                                                     NULL,
                                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                                     "_Guardar", GTK_RESPONSE_ACCEPT,
                                                     "_Cancelar", GTK_RESPONSE_CANCEL,
                                                     NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "instrucciones.txt");

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        save_instructions_to_file(filename, app->instructions, app->instruction_count);
        g_free(filename);
    }

    gtk_widget_destroy(chooser);
}

static int calculate_process_count(const Instruction *list, size_t count)
{
    if (!list || count == 0)
        return 0;

    sim_pid_t max_pid = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (list[i].pid > max_pid)
            max_pid = list[i].pid;
    }
    return (int)(max_pid > 0 ? max_pid : 1);
}

void on_load_instructions_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;

    GtkWidget *dialog = GTK_WIDGET(user_data);
    AppContext *app = g_object_get_data(G_OBJECT(dialog), "app");

    GtkWidget *chooser = gtk_file_chooser_dialog_new("Cargar instrucciones",
                                                     NULL,
                                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     "_Abrir", GTK_RESPONSE_ACCEPT,
                                                     "_Cancelar", GTK_RESPONSE_CANCEL,
                                                     NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_set_name(filter, "Archivos de texto (*.txt)");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    Instruction *list = NULL;
    size_t count = 0;

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        list = parse_instructions_from_file(filename, &count);
        g_free(filename);
    }

    gtk_widget_destroy(chooser);

    if (!list || count == 0)
    {
        GtkWidget *err = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK, "Error al leer el archivo o está vacío.");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    free(app->instructions);
    app->instructions = list;
    app->instruction_count = count;
    app->process_count = calculate_process_count(list, count);
    app->operation_count = (int)((count > (size_t)app->process_count) ? (count - (size_t)app->process_count) : count);
    // Obtener seed del entry o valor por defecto
    GtkWidget *entry_seed = g_object_get_data(G_OBJECT(dialog), "entry_seed");
    unsigned int seed = 1234;
    if (entry_seed)
    {
        const char *seed_text = gtk_entry_get_text(GTK_ENTRY(entry_seed));
        if (seed_text && *seed_text)
            seed = (unsigned int)atoi(seed_text);
    }
    app->seed = seed;

    // Reconstruir simuladores de forma segura
    disconnect_bars(app);
    sim_manager_free(&app->manager);
    app->manager.sim_opt = NULL;
    app->manager.sim_user = NULL;
    sim_manager_init(&app->manager, app->instructions, app->instruction_count, app->manager.user_algorithm);
    app->manager.running = 0;

    // Reconectar barras
    GtkWidget *opt_bar = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
    GtkWidget *user_bar = g_object_get_data(G_OBJECT(app->root_box), "user_bar");
    if (opt_bar && user_bar)
    {
        g_signal_connect(opt_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
        g_signal_connect(user_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
    }

    refresh_stats(app);
    set_run_state(app, RUN_STATE_IDLE);

    GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_OK,
                                            "Se cargaron %zu instrucciones (seed %u).", count, seed);
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
}

void on_start_simulation_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;

    GtkWidget *dialog = GTK_WIDGET(user_data);
    if (!GTK_IS_WIDGET(dialog))
        return;

    AppContext *app = g_object_get_data(G_OBJECT(dialog), "app");
    if (!app)
        return;

    // Verifica que haya instrucciones cargadas o generadas
    if (!app->instructions || app->instruction_count == 0)
    {
        GtkWidget *err = gtk_message_dialog_new(
            GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Primero debes generar o cargar instrucciones antes de iniciar la simulación.");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    if (!app->manager.sim_opt || !app->manager.sim_user)
    {
        AlgorithmType alg = ALG_FIFO;
        sim_manager_free(&app->manager);
        sim_manager_init(&app->manager, app->instructions, app->instruction_count, alg);
        app->manager.running = 0;
    }

    // Construye o reconstruye la ventana principal
    if (!app->main_window)
    {
        ui_view_build_main_window(app);
    }
    else if (!app->root_box)
    {
        sim_manager_free(&app->manager);
        ui_view_build_main_window(app);
    }

    // Asegurar que las barras existen
    GtkWidget *existing_opt = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
    GtkWidget *existing_user = g_object_get_data(G_OBJECT(app->root_box), "user_bar");
    if (!existing_opt || !existing_user)
    {
        ui_view_build_simulation_window(app);
    }

    GtkWidget *opt_bar = g_object_get_data(G_OBJECT(app->root_box), "opt_bar");
    GtkWidget *user_bar = g_object_get_data(G_OBJECT(app->root_box), "user_bar");
    if (opt_bar && user_bar)
    {
        g_signal_connect(opt_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_opt);
        g_signal_connect(user_bar, "draw", G_CALLBACK(draw_ram_bar_cb), app->manager.sim_user);
    }

    // Mostrar la ventana principal
    if (GTK_IS_WIDGET(app->main_window))
    {
        gtk_widget_show_all(app->main_window);
    }

    // Destruir el diálogo de setup
    if (GTK_IS_WIDGET(dialog))
    {
        gtk_widget_destroy(dialog);
    }
}

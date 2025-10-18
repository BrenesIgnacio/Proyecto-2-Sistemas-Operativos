#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    unsigned int seed;
    int process_count;
    int op_count;
    int algorithm;
} Config;

// Carga valores predeterminados para ejecutar la simulación sin parámetros externos.
void config_load_defaults(Config *cfg);
// Muestra los valores de configuración activos en la salida estándar.
void config_print(const Config *cfg);

#endif

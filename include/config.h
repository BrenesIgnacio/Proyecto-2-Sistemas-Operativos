#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    unsigned int seed;
    int process_count;
    int op_count;
    int algorithm;
} Config;

void config_load_defaults(Config *cfg);
void config_print(const Config *cfg);

#endif

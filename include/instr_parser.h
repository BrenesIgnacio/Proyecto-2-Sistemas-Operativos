#ifndef INSTR_PARSER_H
#define INSTR_PARSER_H

#include "common.h"

typedef enum {
    INS_NEW,
    INS_USE,
    INS_DELETE,
    INS_KILL
} InstrType;

typedef struct {
    InstrType type;
    sim_pid_t pid;
    size_t size;
    sim_ptr_t ptr_id;
} Instruction;

Instruction *parse_instructions_from_file(const char *path, size_t *count);
Instruction *generate_instructions(int P, int N, unsigned int seed, size_t *count);
void save_instructions_to_file(const char *path, Instruction *list, size_t n);

#endif

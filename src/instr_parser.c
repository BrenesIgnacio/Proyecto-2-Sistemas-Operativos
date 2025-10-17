#include "instr_parser.h"

Instruction *parse_instructions_from_file(const char *path, size_t *count) {
    (void)path; (void)count; return NULL;
}

Instruction *generate_instructions(int P, int N, unsigned int seed, size_t *count) {
    (void)P; (void)N; (void)seed; (void)count; return NULL;
}

void save_instructions_to_file(const char *path, Instruction *list, size_t n) {
    (void)path; (void)list; (void)n;
}

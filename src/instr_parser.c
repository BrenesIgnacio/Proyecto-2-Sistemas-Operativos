#include "instr_parser.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    sim_pid_t owner_pid;
    int alive;
} PtrInfo;

typedef struct {
    int seen;
    int killed;
} ProcessInfo;

typedef struct {
    Instruction *data;
    size_t count;
    size_t capacity;
} InstructionBuffer;

typedef struct {
    sim_pid_t pid;
    sim_ptr_t *ptrs;
    size_t count;
    size_t capacity;
} GenProcess;

// Variante segura de realloc que aborta si la reserva falla.
static void *xrealloc(void *ptr, size_t size) {
    void *tmp = realloc(ptr, size);
    if (!tmp && size != 0) {
        fprintf(stderr, "Out of memory (realloc)\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

// Asegura que el búfer pueda almacenar al menos instrucciones "necesitadas".
static void ensure_instruction_capacity(InstructionBuffer *buf, size_t needed) {
    if (needed <= buf->capacity) {
        return;
    }
    size_t new_capacity = buf->capacity ? buf->capacity * 2 : 64;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    buf->data = xrealloc(buf->data, new_capacity * sizeof(Instruction));
    buf->capacity = new_capacity;
}

// Registra un nuevo identificador de puntero dentro del proceso generado.
static void process_add_ptr(GenProcess *proc, sim_ptr_t ptr_id) {
    if (proc->count + 1 > proc->capacity) {
        size_t new_capacity = proc->capacity ? proc->capacity * 2 : 4;
        proc->ptrs = xrealloc(proc->ptrs, new_capacity * sizeof(sim_ptr_t));
        proc->capacity = new_capacity;
    }
    proc->ptrs[proc->count++] = ptr_id;
}

// Elimina un puntero del arreglo del proceso moviendo el último elemento.
static void process_remove_ptr(GenProcess *proc, size_t index) {
    if (proc->count == 0 || index >= proc->count) {
        return;
    }
    proc->ptrs[index] = proc->ptrs[proc->count - 1];
    --proc->count;
}

// Amplía el vector de punteros rastreados si el id requerido no cabe.
static void ensure_ptr_capacity(PtrInfo **ptrs, size_t *capacity, size_t needed) {
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = (*capacity ? *capacity * 2 : 32);
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *ptrs = xrealloc(*ptrs, new_capacity * sizeof(PtrInfo));
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*ptrs)[i].owner_pid = 0;
        (*ptrs)[i].alive = 0;
    }
    *capacity = new_capacity;
}

// Garantiza espacio para registrar datos por proceso según el pid dado.
static void ensure_process_capacity(ProcessInfo **procs, size_t *capacity, sim_pid_t pid) {
    size_t needed = (size_t)pid + 1;
    if (needed <= *capacity) {
        return;
    }
    size_t new_capacity = (*capacity ? *capacity * 2 : 16);
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    *procs = xrealloc(*procs, new_capacity * sizeof(ProcessInfo));
    for (size_t i = *capacity; i < new_capacity; ++i) {
        (*procs)[i].seen = 0;
        (*procs)[i].killed = 0;
    }
    *capacity = new_capacity;
}

// Quita espacios iniciales y finales en la línea analizada.
static void trim_whitespace(char *str) {
    if (!str) {
        return;
    }
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Detecta si la línea está vacía o contiene solo un comentario.
static int is_blank_or_comment(const char *line) {
    for (const char *p = line; *p; ++p) {
        if (isspace((unsigned char)*p)) {
            continue;
        }
        return (*p == '#');
    }
    return 1;
}

// Verifica que no existan caracteres basura después de una instrucción válida.
static int ensure_no_trailing(const char *extra) {
    while (*extra) {
        if (!isspace((unsigned char)*extra)) {
            return 0;
        }
        ++extra;
    }
    return 1;
}

// Comprueba que el identificador de puntero sea válido para la operación solicitada.
static int validate_ptr(PtrInfo *ptrs, size_t ptr_capacity, sim_ptr_t ptr_id, InstrType type, FILE *stream, size_t line_no) {
    if (ptr_id == 0 || ptr_id >= ptr_capacity || !ptrs[ptr_id].alive) {
        const char *label = (type == INS_USE) ? "use" : "delete";
        fprintf(stream, "Instruction parser error on line %zu: invalid pointer id %u for %s()\n",
                line_no, ptr_id, label);
        return 0;
    }
    return 1;
}

// Carga instrucciones desde un archivo de texto con formato amigable.
Instruction *parse_instructions_from_file(const char *path, size_t *count) {
    if (count) {
        *count = 0;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    InstructionBuffer buffer = {0};
    PtrInfo *ptrs = NULL;
    size_t ptr_capacity = 0;
    ProcessInfo *processes = NULL;
    size_t proc_capacity = 0;
    sim_ptr_t next_ptr_id = 0;

    char line[256];
    size_t line_no = 0;
    int ok = 1;

    while (fgets(line, sizeof(line), fp)) {
        ++line_no;
        trim_whitespace(line);
        if (*line == '\0' || is_blank_or_comment(line)) {
            continue;
        }

        Instruction instr = {0};
        int consumed = 0;

        unsigned int pid = 0;
        size_t size = 0;
        unsigned int ptr_raw = 0;

        if (sscanf(line, "new(%u,%zu)%n", &pid, &size, &consumed) == 2) {
            if (!ensure_no_trailing(line + consumed)) {
                fprintf(stderr, "Instruction parser error on line %zu: trailing characters after new()\n", line_no);
                ok = 0;
                break;
            }
            ensure_process_capacity(&processes, &proc_capacity, (sim_pid_t)pid);
            ProcessInfo *proc = &processes[pid];
            if (proc->killed) {
                fprintf(stderr, "Instruction parser error on line %zu: process %u already killed\n", line_no, pid);
                ok = 0;
                break;
            }
            proc->seen = 1;

            ++next_ptr_id;
            ensure_ptr_capacity(&ptrs, &ptr_capacity, (size_t)next_ptr_id + 1);
            ptrs[next_ptr_id].owner_pid = (sim_pid_t)pid;
            ptrs[next_ptr_id].alive = 1;

            instr.type = INS_NEW;
            instr.pid = (sim_pid_t)pid;
            instr.size = size;
            instr.ptr_id = next_ptr_id;
        } else if (sscanf(line, "use(%u)%n", &ptr_raw, &consumed) == 1) {
            if (!ensure_no_trailing(line + consumed)) {
                fprintf(stderr, "Instruction parser error on line %zu: trailing characters after use()\n", line_no);
                ok = 0;
                break;
            }
            sim_ptr_t ptr_id = (sim_ptr_t)ptr_raw;
            if (!validate_ptr(ptrs, ptr_capacity, ptr_id, INS_USE, stderr, line_no)) {
                ok = 0;
                break;
            }
            instr.type = INS_USE;
            instr.ptr_id = ptr_id;
            instr.pid = ptrs[ptr_id].owner_pid;
        } else if (sscanf(line, "delete(%u)%n", &ptr_raw, &consumed) == 1) {
            if (!ensure_no_trailing(line + consumed)) {
                fprintf(stderr, "Instruction parser error on line %zu: trailing characters after delete()\n", line_no);
                ok = 0;
                break;
            }
            sim_ptr_t ptr_id = (sim_ptr_t)ptr_raw;
            if (!validate_ptr(ptrs, ptr_capacity, ptr_id, INS_DELETE, stderr, line_no)) {
                ok = 0;
                break;
            }
            instr.type = INS_DELETE;
            instr.ptr_id = ptr_id;
            instr.pid = ptrs[ptr_id].owner_pid;
            ptrs[ptr_id].alive = 0;
        } else if (sscanf(line, "kill(%u)%n", &pid, &consumed) == 1) {
            if (!ensure_no_trailing(line + consumed)) {
                fprintf(stderr, "Instruction parser error on line %zu: trailing characters after kill()\n", line_no);
                ok = 0;
                break;
            }
            ensure_process_capacity(&processes, &proc_capacity, (sim_pid_t)pid);
            ProcessInfo *proc = &processes[pid];
            if (!proc->seen) {
                fprintf(stderr, "Instruction parser error on line %zu: kill() on unknown process %u\n", line_no, pid);
                ok = 0;
                break;
            }
            if (proc->killed) {
                fprintf(stderr, "Instruction parser error on line %zu: duplicate kill() for process %u\n", line_no, pid);
                ok = 0;
                break;
            }
            proc->killed = 1;

            instr.type = INS_KILL;
            instr.pid = (sim_pid_t)pid;
        } else {
            fprintf(stderr, "Instruction parser error on line %zu: unrecognised instruction '%s'\n", line_no, line);
            ok = 0;
            break;
        }

        ensure_instruction_capacity(&buffer, buffer.count + 1);
        buffer.data[buffer.count++] = instr;
    }

    fclose(fp);

    free(ptrs);
    free(processes);

    if (!ok) {
        free(buffer.data);
        if (count) {
            *count = 0;
        }
        return NULL;
    }

    if (buffer.count == 0) {
        free(buffer.data);
        return NULL;
    }

    if (count) {
        *count = buffer.count;
    }
    return buffer.data;
}

// Genera instrucciones pseudoaleatorias conforme a los parámetros recibidos.
Instruction *generate_instructions(int P, int N, unsigned int seed, size_t *count) {
    if (count) {
        *count = 0;
    }

    if (P <= 0) {
        return NULL;
    }

    srand(seed);

    InstructionBuffer buffer = {0};
    GenProcess *procs = xmalloc((size_t)(P + 1) * sizeof(GenProcess));
    for (int i = 0; i <= P; ++i) {
        procs[i].pid = (sim_pid_t)i;
        procs[i].ptrs = NULL;
        procs[i].count = 0;
        procs[i].capacity = 0;
    }

    sim_ptr_t next_ptr_id = 0;
    int operations_remaining = (N > 0) ? N : 0;

    /* Ensure each process gets an initial allocation when possible */
    for (int pid = 1; pid <= P && operations_remaining > 0; ++pid) {
        Instruction instr = {0};
        instr.type = INS_NEW;
        instr.pid = (sim_pid_t)pid;
        instr.size = (size_t)random_int(1, 20000);
        instr.ptr_id = ++next_ptr_id;

        ensure_instruction_capacity(&buffer, buffer.count + 1);
        buffer.data[buffer.count++] = instr;
        process_add_ptr(&procs[pid], instr.ptr_id);
        --operations_remaining;
    }

    while (operations_remaining > 0) {
        int pid = random_int(1, P);
        GenProcess *proc = &procs[pid];

        int action = 0; /* 0=new, 1=use, 2=delete */
        if (proc->count == 0) {
            action = 0;
        } else {
            int roll = random_int(0, 99);
            if (proc->count == 1) {
                action = (roll < 45) ? 0 : (roll < 80 ? 1 : 2);
            } else {
                action = (roll < 35) ? 0 : (roll < 75 ? 1 : 2);
            }
        }

        Instruction instr = {0};

        if (action == 0) {
            instr.type = INS_NEW;
            instr.pid = (sim_pid_t)pid;
            instr.size = (size_t)random_int(1, 20000);
            instr.ptr_id = ++next_ptr_id;
            process_add_ptr(proc, instr.ptr_id);
        } else if (action == 1 && proc->count > 0) {
            size_t slot = (size_t)random_int(0, (int)proc->count - 1);
            instr.type = INS_USE;
            instr.pid = (sim_pid_t)pid;
            instr.ptr_id = proc->ptrs[slot];
        } else if (action == 2 && proc->count > 0) {
            size_t slot = (size_t)random_int(0, (int)proc->count - 1);
            sim_ptr_t ptr_id = proc->ptrs[slot];
            instr.type = INS_DELETE;
            instr.pid = (sim_pid_t)pid;
            instr.ptr_id = ptr_id;
            process_remove_ptr(proc, slot);
        } else {
            /* Volvemos a asignar memoria si ninguna acción previa fue válida */
            instr.type = INS_NEW;
            instr.pid = (sim_pid_t)pid;
            instr.size = (size_t)random_int(1, 20000);
            instr.ptr_id = ++next_ptr_id;
            process_add_ptr(proc, instr.ptr_id);
        }

        ensure_instruction_capacity(&buffer, buffer.count + 1);
        buffer.data[buffer.count++] = instr;
        --operations_remaining;
    }

    for (int pid = 1; pid <= P; ++pid) {
        Instruction instr = {0};
        instr.type = INS_KILL;
        instr.pid = (sim_pid_t)pid;
        ensure_instruction_capacity(&buffer, buffer.count + 1);
        buffer.data[buffer.count++] = instr;
    }

    for (int pid = 0; pid <= P; ++pid) {
        free(procs[pid].ptrs);
    }
    free(procs);

    if (count) {
        *count = buffer.count;
    }

    return buffer.count ? buffer.data : NULL;
}

// Escribe una lista de instrucciones en un archivo de texto legible.
void save_instructions_to_file(const char *path, Instruction *list, size_t n) {
    if (!path || (!list && n > 0)) {
        return;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        const Instruction *ins = &list[i];
        switch (ins->type) {
            case INS_NEW:
                fprintf(fp, "new(%u,%zu)\n", ins->pid, ins->size);
                break;
            case INS_USE:
                fprintf(fp, "use(%u)\n", ins->ptr_id);
                break;
            case INS_DELETE:
                fprintf(fp, "delete(%u)\n", ins->ptr_id);
                break;
            case INS_KILL:
                fprintf(fp, "kill(%u)\n", ins->pid);
                break;
            default:
                break;
        }
    }

    fclose(fp);
}

#pragma once
#include <stdbool.h>
#include <stddef.h>

#define COMMAND_READ_BUDGET 128
typedef struct {
    char line[96];
    size_t used;
    bool overflow;
} command_line_t;
typedef bool (*command_read_fn)(char *ch, void *context);
bool command_readline(command_line_t *state, char *out, size_t cap,
                      command_read_fn read_byte, void *context);

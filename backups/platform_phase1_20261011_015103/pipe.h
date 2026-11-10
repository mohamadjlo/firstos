#ifndef _FIRSTOS_PIPE_H
#define _FIRSTOS_PIPE_H

#include "include/firstos/uapi/syscall.h"

#define FIRSTOS_PIPE_BUFFER_SIZE 4096
#define FIRSTOS_MAX_PIPES        16

typedef struct {
    int used;
    int readers;
    int writers;
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    char buffer[FIRSTOS_PIPE_BUFFER_SIZE];
} pipe_t;

extern pipe_t pipe_table[FIRSTOS_MAX_PIPES];

void pipe_subsystem_init(void);
int  pipe_create(int *out_pipe_idx);
int  pipe_read(int pipe_idx, void *buf, unsigned int count);
int  pipe_write(int pipe_idx, const void *buf, unsigned int count);
void pipe_close_endpoint(int pipe_idx, int is_write);
void pipe_add_ref(int pipe_idx, int is_write);

#endif /* _FIRSTOS_PIPE_H */

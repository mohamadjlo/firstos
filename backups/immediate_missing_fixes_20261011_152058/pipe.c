#include "pipe.h"
#include "io.h"
#include "serial.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

pipe_t pipe_table[FIRSTOS_MAX_PIPES];

void pipe_subsystem_init(void) {
    for (int i = 0; i < FIRSTOS_MAX_PIPES; i++) {
        pipe_table[i].used = 0;
        pipe_table[i].readers = 0;
        pipe_table[i].writers = 0;
        pipe_table[i].read_pos = 0;
        pipe_table[i].write_pos = 0;
        pipe_table[i].count = 0;
        for (int b = 0; b < FIRSTOS_PIPE_BUFFER_SIZE; b++) {
            pipe_table[i].buffer[b] = 0;
        }
    }
    serial_write("[pipe] Pipe subsystem initialized (16 pipes, 4KB buffers)\n");
}

int pipe_create(int *out_pipe_idx) {
    if (!out_pipe_idx) return -FIRSTOS_EFAULT;

    for (int i = 0; i < FIRSTOS_MAX_PIPES; i++) {
        if (!pipe_table[i].used) {
            pipe_table[i].used = 1;
            pipe_table[i].readers = 1;
            pipe_table[i].writers = 1;
            pipe_table[i].read_pos = 0;
            pipe_table[i].write_pos = 0;
            pipe_table[i].count = 0;
            *out_pipe_idx = i;
            serial_write("[pipe] created idx=");
            serial_write_hex((u32)i);
            serial_write("\n");
            return 0;
        }
    }
    return -FIRSTOS_EMFILE;
}

int pipe_read(int pipe_idx, void *buf, unsigned int count) {
    if (pipe_idx < 0 || pipe_idx >= FIRSTOS_MAX_PIPES) return -FIRSTOS_EBADF;
    pipe_t *p = &pipe_table[pipe_idx];
    if (!p->used) return -FIRSTOS_EBADF;
    if (count == 0) return 0;
    if (!buf) return -FIRSTOS_EFAULT;

    char *dst = (char *)buf;
    unsigned int bytes_read = 0;

    /* If empty, handle blocking/yielding or EOF */
    while (p->count == 0) {
        if (p->writers == 0) {
            /* All write ends closed and buffer empty -> EOF */
            return 0;
        }
        /* Writers exist but no data currently available: yield */
#ifndef FIRSTOS_HOST_TEST
        __asm__ volatile ("int $0x81");
#else
        break;
#endif
    }

    while (bytes_read < count && p->count > 0) {
        dst[bytes_read++] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % FIRSTOS_PIPE_BUFFER_SIZE;
        p->count--;
    }

    return (int)bytes_read;
}

int pipe_write(int pipe_idx, const void *buf, unsigned int count) {
    if (pipe_idx < 0 || pipe_idx >= FIRSTOS_MAX_PIPES) return -FIRSTOS_EBADF;
    pipe_t *p = &pipe_table[pipe_idx];
    if (!p->used) return -FIRSTOS_EBADF;

    /* Writing to a pipe with no readers generates EPIPE */
    if (p->readers == 0) {
        return -FIRSTOS_EPIPE;
    }
    if (count == 0) return 0;
    if (!buf) return -FIRSTOS_EFAULT;

    const char *src = (const char *)buf;
    unsigned int bytes_written = 0;

    while (bytes_written < count) {
        /* If pipe is full, yield if readers exist, or return written bytes */
        if (p->count >= FIRSTOS_PIPE_BUFFER_SIZE) {
            if (p->readers == 0) return -FIRSTOS_EPIPE;
#ifndef FIRSTOS_HOST_TEST
            __asm__ volatile ("int $0x81");
#else
            break;
#endif
        }

        while (bytes_written < count && p->count < FIRSTOS_PIPE_BUFFER_SIZE) {
            p->buffer[p->write_pos] = src[bytes_written++];
            p->write_pos = (p->write_pos + 1) % FIRSTOS_PIPE_BUFFER_SIZE;
            p->count++;
        }
    }

    return (int)bytes_written;
}

void pipe_add_ref(int pipe_idx, int is_write) {
    if (pipe_idx < 0 || pipe_idx >= FIRSTOS_MAX_PIPES) return;
    pipe_t *p = &pipe_table[pipe_idx];
    if (!p->used) return;

    if (is_write) {
        p->writers++;
    } else {
        p->readers++;
    }
}

void pipe_close_endpoint(int pipe_idx, int is_write) {
    if (pipe_idx < 0 || pipe_idx >= FIRSTOS_MAX_PIPES) return;
    pipe_t *p = &pipe_table[pipe_idx];
    if (!p->used) return;

    if (is_write) {
        if (p->writers > 0) p->writers--;
    } else {
        if (p->readers > 0) p->readers--;
    }

    serial_write("[pipe] close endpoint idx=");
    serial_write_hex((u32)pipe_idx);
    serial_write(" is_write=");
    serial_write_hex((u32)is_write);
    serial_write("\n");

    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
        p->count = 0;
        p->read_pos = 0;
        p->write_pos = 0;
        serial_write("[pipe] freed idx=");
        serial_write_hex((u32)pipe_idx);
        serial_write("\n");
    }
}

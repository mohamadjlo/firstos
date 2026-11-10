#ifndef USERSPACE_LOADER_H
#define USERSPACE_LOADER_H

#include "io.h"
#include "include/firstos/uapi/memory.h"

typedef struct {
    u64 entry;
    u64 image_base;
    u64 image_end;
    u64 heap_base;
    u64 stack_top;
    u64 argc;
    u64 argv_user;
} userspace_image_t;

/* ELF validation and loader error codes */
#define ELF_OK                           0
#define ELF_ERR_NULL_BUFFER             -1
#define ELF_ERR_TOO_SMALL               -2
#define ELF_ERR_BAD_MAGIC               -3
#define ELF_ERR_UNSUPPORTED_CLASS       -4
#define ELF_ERR_UNSUPPORTED_ENDIAN      -5
#define ELF_ERR_UNSUPPORTED_VERSION     -6
#define ELF_ERR_UNSUPPORTED_MACHINE     -7
#define ELF_ERR_UNSUPPORTED_TYPE        -8
#define ELF_ERR_INVALID_PHDR_RANGE      -9
#define ELF_ERR_PHDR_COUNT_EXCEEDED     -10
#define ELF_ERR_SEGMENT_FILE_RANGE      -11
#define ELF_ERR_SEGMENT_MEM_RANGE       -12
#define ELF_ERR_SEGMENT_OUT_OF_USER     -13
#define ELF_ERR_SEGMENT_OVERLAPS_HEAP   -14
#define ELF_ERR_ENTRY_OUT_OF_SEGMENT    -15
#define ELF_ERR_DYNAMIC_NOT_SUPPORTED   -16
#define ELF_ERR_ALLOC_FAILED            -17

int userspace_validate_elf(const char *buf, unsigned int size, userspace_image_t *img);
int userspace_spawn_elf_buffer(const char *elf_buf, unsigned int elf_size, const char *task_name, int argc, const char **argv);
int userspace_spawn_from_fs(const char *path, int argc, const char **argv);
int userspace_hello_spawn(void);

#endif /* USERSPACE_LOADER_H */

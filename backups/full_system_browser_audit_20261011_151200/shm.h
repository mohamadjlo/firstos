#ifndef _FIRSTOS_SHM_H
#define _FIRSTOS_SHM_H

#include "include/firstos/uapi/syscall.h"
#include "include/firstos/uapi/memory.h"

#define FIRSTOS_MAX_SHM 8
#define FIRSTOS_SHM_NAME_MAX 32

typedef struct {
    int used;
    char name[FIRSTOS_SHM_NAME_MAX];
    char *phys_mem;        /* 4MB aligned physical buffer */
    int attach_count;      /* Number of processes currently mapped */
    int marked_for_unlink; /* Destroy when attach_count == 0 */
} shm_region_t;

extern shm_region_t shm_table[FIRSTOS_MAX_SHM];

void shm_subsystem_init(void);
int  shm_open(const char *name, int flags, int mode);
int  shm_map(int pid, int shmid, void **out_vaddr);
int  shm_unmap(int pid, int shmid);
void shm_process_cleanup(int pid);

#endif /* _FIRSTOS_SHM_H */

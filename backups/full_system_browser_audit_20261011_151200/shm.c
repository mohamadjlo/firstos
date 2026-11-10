#include "shm.h"
#include "heap.h"
#include "serial.h"
#include "process.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

shm_region_t shm_table[FIRSTOS_MAX_SHM];
extern u64 kernel_pml4[512];
extern void load_page_directory(u64*);
extern int current_task;

static int shm_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    int i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
        i++;
    }
    return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
}

static void shm_strcpy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void shm_subsystem_init(void) {
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        shm_table[i].used = 0;
        shm_table[i].name[0] = '\0';
        shm_table[i].phys_mem = NULL;
        shm_table[i].attach_count = 0;
        shm_table[i].marked_for_unlink = 0;
    }
    serial_write("[shm] Shared memory subsystem initialized (8 regions, 4MB each)\n");
}

int shm_open(const char *name, int flags, int mode) {
    (void)flags;
    (void)mode;
    if (!name || !name[0]) return -FIRSTOS_EINVAL;

    /* Check if already exists */
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        if (shm_table[i].used && shm_strcmp(shm_table[i].name, name) == 0) {
            return i;
        }
    }

    /* Allocate new region */
    int slot = -1;
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        if (!shm_table[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -FIRSTOS_EMFILE;

    char *mem = (char *)kmalloc_aligned(FIRSTOS_USER_SHM_SIZE, 0x400000);
    if (!mem) return -FIRSTOS_ENOMEM;

    /* Zero memory block */
    unsigned int *pz = (unsigned int *)mem;
    for (unsigned int k = 0; k < (FIRSTOS_USER_SHM_SIZE / 4); k++) {
        pz[k] = 0;
    }

    shm_table[slot].used = 1;
    shm_strcpy(shm_table[slot].name, name, FIRSTOS_SHM_NAME_MAX);
    shm_table[slot].phys_mem = mem;
    shm_table[slot].attach_count = 0;
    shm_table[slot].marked_for_unlink = 0;

    serial_write("[shm] created region '");
    serial_write(shm_table[slot].name);
    serial_write("' shmid=");
    serial_write_hex((unsigned int)slot);
    serial_write("\n");

    return slot;
}

int shm_map(int pid, int shmid, void **out_vaddr) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (shmid < 0 || shmid >= FIRSTOS_MAX_SHM) return -FIRSTOS_EINVAL;
    if (!shm_table[shmid].used || !shm_table[shmid].phys_mem) return -FIRSTOS_ENOENT;

    u64 *pd = process_table[pid].proc_pd;
    if (!pd) return -FIRSTOS_EINVAL;

    /* Map entries 34 and 35 to 4MB shared physical block with User RW (0x87) */
    pd[FIRSTOS_USER_SHM_PDE_INDEX] = ((u64)(uintptr_t)shm_table[shmid].phys_mem) | 0x87;
    pd[FIRSTOS_USER_SHM_PDE_INDEX + 1] = ((u64)(uintptr_t)(shm_table[shmid].phys_mem + 0x200000ULL)) | 0x87;

    if (pid == current_task && process_table[pid].page_dir) {
        load_page_directory(process_table[pid].page_dir);
        __asm__ volatile("invlpg (%0)" :: "r"(FIRSTOS_USER_SHM_BASE) : "memory");
        __asm__ volatile("invlpg (%0)" :: "r"(FIRSTOS_USER_SHM_BASE + 0x200000ULL) : "memory");
    }

    shm_table[shmid].attach_count++;
    process_table[pid].attached_shm_id = shmid;

    if (out_vaddr) {
        *out_vaddr = (void *)FIRSTOS_USER_SHM_BASE;
    }

    serial_write("[shm] mapped shmid=");
    serial_write_hex((u32)shmid);
    serial_write(" into pid=");
    serial_write_hex((u32)pid);
    serial_write(" at 0x04400000\n");

    return 0;
}

int shm_unmap(int pid, int shmid) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (shmid < 0 || shmid >= FIRSTOS_MAX_SHM) return -FIRSTOS_EINVAL;
    if (!shm_table[shmid].used) return -FIRSTOS_EINVAL;

    extern u64 kernel_pd[4][512];
    u64 *pd = process_table[pid].proc_pd;
    if (pd) {
        /* Restore kernel identity map */
        pd[FIRSTOS_USER_SHM_PDE_INDEX] = kernel_pd[0][FIRSTOS_USER_SHM_PDE_INDEX];
        pd[FIRSTOS_USER_SHM_PDE_INDEX + 1] = kernel_pd[0][FIRSTOS_USER_SHM_PDE_INDEX + 1];
        if (pid == current_task && process_table[pid].page_dir) {
            load_page_directory(process_table[pid].page_dir);
            __asm__ volatile("invlpg (%0)" :: "r"(FIRSTOS_USER_SHM_BASE) : "memory");
            __asm__ volatile("invlpg (%0)" :: "r"(FIRSTOS_USER_SHM_BASE + 0x200000ULL) : "memory");
        }
    }

    if (shm_table[shmid].attach_count > 0) {
        shm_table[shmid].attach_count--;
    }
    process_table[pid].attached_shm_id = -1;

    serial_write("[shm] unmapped shmid=");
    serial_write_hex((unsigned int)shmid);
    serial_write(" from pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");

    if (shm_table[shmid].attach_count == 0 && shm_table[shmid].marked_for_unlink) {
        if (shm_table[shmid].phys_mem) {
            kfree(shm_table[shmid].phys_mem);
            shm_table[shmid].phys_mem = NULL;
        }
        shm_table[shmid].used = 0;
        shm_table[shmid].name[0] = '\0';
    }

    return 0;
}

void shm_process_cleanup(int pid) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return;
    int shmid = process_table[pid].attached_shm_id;
    if (shmid >= 0 && shmid < FIRSTOS_MAX_SHM) {
        shm_unmap(pid, shmid);
    }
    for (int s = 0; s < FIRSTOS_MAX_PROC_SHM; s++) {
        int sid = process_table[pid].attached_shm_ids[s];
        if (sid >= 0 && sid < FIRSTOS_MAX_SHM) {
            shm_unmap(pid, sid);
            process_table[pid].attached_shm_ids[s] = -1;
        }
    }
    serial_write("[shm] cleanup done pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");
    serial_write("[mem] shm cleanup pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");
}

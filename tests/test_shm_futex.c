#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);
void *memset(void *s, int c, unsigned long n);
void *malloc(unsigned long size);
void free(void *ptr);

#define FIRSTOS_HOST_TEST 1
#include "include/firstos/uapi/syscall.h"
#include "include/firstos/uapi/memory.h"
#include "shm.h"
#include "futex.h"

#include "process.h"

/* Serial dummy */
void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int v) { (void)v; }

process_info_t process_table[FIRSTOS_MAX_PROCESSES];
static unsigned int mock_page_dirs[FIRSTOS_MAX_PROCESSES][1024];
unsigned int kernel_page_dir[1024];
int current_task = 0;

void load_page_directory(unsigned int *dir) { (void)dir; }

/* Mock kmalloc_aligned for host test */
void *kmalloc_aligned(unsigned int size, unsigned int align) {
    (void)align;
    void *ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    free(ptr);
}

void kfree_aligned(void *ptr) {
    free(ptr);
}

/* Fast userspace mutex primitives */
static int mock_mutex_lock(int *mutex) {
    if (__sync_bool_compare_and_swap(mutex, 0, 1)) {
        return 0; /* uncontended fast-path */
    }
    while (__sync_lock_test_and_set(mutex, 2) != 0) {
        futex_wait(current_task, mutex, 2);
    }
    return 0;
}

static void mock_mutex_unlock(int *mutex) {
    if (__sync_lock_test_and_set(mutex, 0) == 2) {
        futex_wake(mutex, 1);
    }
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 3C: Shared Memory & Futex Mutex Tests\n");
    printf("============================================================\n");

    /* TEST 1: Subsystem Initialization */
    printf("[TEST 1] Testing shm & futex subsystem initialization...\n");
    shm_subsystem_init();
    futex_subsystem_init();
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        assert(shm_table[i].used == 0);
        assert(shm_table[i].attach_count == 0);
    }
    for (int p = 0; p < FIRSTOS_MAX_PROCESSES; p++) {
        process_table[p].pid = p;
        process_table[p].attached_shm_id = -1;
        process_table[p].page_dir = mock_page_dirs[p];
        for (int k = 0; k < 1024; k++) mock_page_dirs[p][k] = 0x83;
    }
    printf("  -> SHM (8 regions) & Futex subsystems initialized cleanly.\n");

    /* TEST 2: Create Shared Memory Region */
    printf("[TEST 2] Testing shm_open creation...\n");
    int shm0 = shm_open("framebuffer_shm", 0, 0);
    assert(shm0 == 0);
    assert(shm_table[shm0].used == 1);
    assert(shm_table[shm0].attach_count == 0);
    assert(shm_table[shm0].phys_mem != (void*)0);
    printf("  -> Region 'framebuffer_shm' created at shmid=%d.\n", shm0);

    /* TEST 3: Open Existing Region by Name */
    printf("[TEST 3] Testing opening existing region by name...\n");
    int shm0_reopen = shm_open("framebuffer_shm", 0, 0);
    assert(shm0_reopen == shm0);
    printf("  -> Re-opening 'framebuffer_shm' returned identical shmid=%d.\n", shm0_reopen);

    /* TEST 4: Mapping Shared Memory into Processes */
    printf("[TEST 4] Testing zero-copy shared memory mapping into multiple processes...\n");
    void *vaddr1 = (void*)0;
    void *vaddr2 = (void*)0;
    assert(shm_map(1, shm0, &vaddr1) == 0);
    assert(vaddr1 == (void*)FIRSTOS_USER_SHM_BASE);
    assert(shm_map(2, shm0, &vaddr2) == 0);
    assert(vaddr2 == (void*)FIRSTOS_USER_SHM_BASE);

    assert(shm_table[shm0].attach_count == 2);
    /* Verify both page directories point to identical physical block */
    assert(process_table[1].page_dir[FIRSTOS_USER_SHM_PDE_INDEX] == process_table[2].page_dir[FIRSTOS_USER_SHM_PDE_INDEX]);
    assert((process_table[1].page_dir[FIRSTOS_USER_SHM_PDE_INDEX] & 0x87) == 0x87);

    /* Write data from Process 1 perspective */
    char *shared_view_p1 = shm_table[shm0].phys_mem;
    const char *payload = "ZERO_COPY_SHARED_FRAMEBUFFER_PAYLOAD";
    for (int i = 0; payload[i]; i++) shared_view_p1[i] = payload[i];

    /* Read data from Process 2 perspective */
    char *shared_view_p2 = shm_table[shm0].phys_mem;
    for (int i = 0; payload[i]; i++) {
        assert(shared_view_p2[i] == payload[i]);
    }
    printf("  -> Zero-copy shared memory verified between Process 1 and 2.\n");

    /* TEST 5: Detachment & Reference Counting */
    printf("[TEST 5] Testing detachment & reference counting...\n");
    assert(shm_unmap(1, shm0) == 0);
    assert(shm_table[shm0].attach_count == 1);
    assert(shm_unmap(2, shm0) == 0);
    assert(shm_table[shm0].attach_count == 0);
    printf("  -> Both processes detached, attach_count reached 0.\n");

    /* TEST 6: Region Capacity Exhaustion */
    printf("[TEST 6] Testing shm capacity exhaustion...\n");
    int ids[FIRSTOS_MAX_SHM];
    ids[0] = shm0;
    for (int i = 1; i < FIRSTOS_MAX_SHM; i++) {
        char name[16] = "reg_";
        name[4] = (char)('0' + i);
        name[5] = '\0';
        ids[i] = shm_open(name, 0, 0);
        assert(ids[i] == i);
    }
    int overflow = shm_open("overflow_reg", 0, 0);
    assert(overflow == -FIRSTOS_EMFILE);
    printf("  -> 8-region capacity limit enforced with -EMFILE.\n");

    /* Clean up all regions */
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        if (shm_table[i].phys_mem) {
            free(shm_table[i].phys_mem);
            shm_table[i].phys_mem = (void*)0;
        }
        shm_table[i].used = 0;
    }

    /* TEST 7: Fast Mutex Uncontended Path */
    printf("[TEST 7] Testing fast userspace mutex uncontended path...\n");
    int mutex = 0;
    assert(mock_mutex_lock(&mutex) == 0);
    assert(mutex == 1); /* Locked atomically without syscall */
    mock_mutex_unlock(&mutex);
    assert(mutex == 0); /* Unlocked atomically */
    printf("  -> Uncontended lock and unlock succeeded with zero syscall overhead.\n");

    /* TEST 8: Futex Wait Value Mismatch */
    printf("[TEST 8] Testing futex_wait atomic value validation...\n");
    int sync_var = 42;
    /* Expecting 10, but value is 42 -> must return -EAGAIN */
    int f_rc = futex_wait(1, &sync_var, 10);
    assert(f_rc == -FIRSTOS_EAGAIN);
    printf("  -> futex_wait returned -EAGAIN on value mismatch (expected 10, got 42).\n");

    /* TEST 9: Futex Wake on Matching Address */
    printf("[TEST 9] Testing futex_wake semantics...\n");
    int woken = futex_wake(&sync_var, 1);
    assert(woken == 0); /* No waiters registered */
    printf("  -> futex_wake returned 0 when no waiters active.\n");

    /* TEST 10: Automatic Process Cleanup */
    printf("[TEST 10] Testing automatic cleanup on process exit...\n");
    shm_subsystem_init();
    int clean_shm = shm_open("cleanup_test", 0, 0);
    assert(clean_shm == 0);
    void *vaddr3 = (void*)0;
    assert(shm_map(3, clean_shm, &vaddr3) == 0);
    assert(shm_table[clean_shm].attach_count == 1);
    assert(process_table[3].attached_shm_id == clean_shm);

    /* Test shm_process_cleanup */
    shm_process_cleanup(3);
    assert(process_table[3].attached_shm_id == -1);
    assert(shm_table[clean_shm].attach_count == 0);
    if (shm_table[clean_shm].phys_mem) {
        free(shm_table[clean_shm].phys_mem);
        shm_table[clean_shm].phys_mem = (void*)0;
    }
    shm_table[clean_shm].used = 0;
    printf("  -> Process termination automatically released shared memory attachment.\n");

    printf("============================================================\n");
    printf("All 10 Shared Memory & Futex Host Tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}

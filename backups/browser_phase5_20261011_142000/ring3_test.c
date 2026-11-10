#include "ring3_test.h"
#include "task.h"
#include "paging.h"
#include "heap.h"
#include "serial.h"

extern task_t tasks[MAX_TASKS];

/* Helper to copy null-terminated string */
static void copy_string(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* Machine code for Ring 3 Hello & Protection test:
 * 0:  b8 00 00 00 00       mov eax, 0          ; sys_print
 * 5:  bb 00 01 00 04       mov ebx, 0x04000100 ; valid user space string
 * a:  cd 80                int 0x80            ; invoke system call
 * c:  b8 00 00 00 00       mov eax, 0          ; sys_print
 * 11: bb 00 00 10 00       mov ebx, 0x00100000 ; forbidden kernel address
 * 16: cd 80                int 0x80            ; expect -EFAULT (rejected)
 * 18: b8 ff 7f 00 00       mov eax, 0x00007FFF ; unknown syscall
 * 1d: cd 80                int 0x80            ; expect -ENOSYS
 * 1f: b8 00 00 00 00       mov eax, 0          ; sys_print
 * 24: bb 80 01 00 04       mov ebx, 0x04000180 ; confirmation string
 * 29: cd 80                int 0x80            ; invoke system call
 * 2b: b8 01 00 00 00       mov eax, 1          ; sys_exit
 * 30: bb 00 00 00 00       mov ebx, 0          ; exit code 0
 * 35: cd 80                int 0x80            ; exit cleanly
 * 37: cd 81                int 0x81            ; yield to scheduler
 * 39: eb fc                jmp -4              ; fallback yield loop
 */
static const unsigned char hello_user_code[] = {
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xBB, 0x00, 0x01, 0x00, 0x04,
    0xCD, 0x80,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xBB, 0x00, 0x00, 0x10, 0x00,
    0xCD, 0x80,
    0xB8, 0xFF, 0x7F, 0x00, 0x00,
    0xCD, 0x80,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xBB, 0x80, 0x01, 0x00, 0x04,
    0xCD, 0x80,
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xBB, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xCD, 0x81,
    0xEB, 0xFC
};

int ring3_hello_task_spawn(void) {
    serial_write("[ring3_test] setting up Ring 3 Hello Task...\n");

    /* Allocate independent page directory */
    u64 *dir = create_page_directory();
    if (!dir) {
        serial_write("[ring3_test] failed to allocate page directory\n");
        return -1;
    }

    /* Allocate 4MB physical block for user space */
    char *user_mem = (char *)kmalloc_aligned(0x400000, 0x400000);
    if (!user_mem) {
        serial_write("[ring3_test] failed to allocate 4MB user memory\n");
        return -1;
    }

    /* Zero the allocated user memory */
    unsigned int *p = (unsigned int *)user_mem;
    for (int k = 0; k < 0x100000; k++) p[k] = 0;

    /* Map virtual 0x04000000 (entry 16) with User | RW | Present (0x87) */
    dir[16] = ((u64)(uintptr_t)user_mem) | 0x87;

    /* Copy user payload code to virtual 0x04000000 */
    for (unsigned int c = 0; c < sizeof(hello_user_code); c++) {
        user_mem[c] = (char)hello_user_code[c];
    }

    /* Place valid string at virtual 0x04000100 */
    copy_string(user_mem + 0x100, "Hello from FirstOS Ring 3 Userspace!\n");

    /* Place confirmation string at virtual 0x04000180 */
    copy_string(user_mem + 0x180, "[ring3] all userspace tests executed successfully\n");

    /* User stack top at the end of the 4MB block */
    u64 user_stack_top = 0x04400000 - 4;
    u64 entry_point = 0x04000000;

    int tid = task_create_user((u64)(uintptr_t)dir, entry_point, user_stack_top, (u64)(uintptr_t)user_mem + 0x400000 - 4);
    if (tid >= 0) {
        copy_string(tasks[tid].name, "r3-hello");
        serial_write("[ring3_test] Ring 3 hello task spawned successfully (tid=");
        serial_write_hex((unsigned int)tid);
        serial_write(")\n");
        return tid;
    }

    serial_write("[ring3_test] task_create_user failed\n");
    return -1;
}

/* Machine code for Ring 3 Controlled Page Fault test:
 * 0:  b8 00 00 00 00       mov eax, 0          ; sys_print
 * 5:  bb 00 01 00 04       mov ebx, 0x04000100 ; notification string
 * a:  cd 80                int 0x80
 * c:  a1 00 00 10 00       mov eax, [0x00100000] ; read supervisor page (triggers #PF)
 * 11: cd 81                int 0x81            ; fallback yield
 * 13: eb fc                jmp -4
 */
static const unsigned char fault_user_code[] = {
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xBB, 0x00, 0x01, 0x00, 0x04,
    0xCD, 0x80,
    0xA1, 0x00, 0x00, 0x10, 0x00,
    0xCD, 0x81,
    0xEB, 0xFC
};

int ring3_fault_task_spawn(void) {
    serial_write("[ring3_test] setting up Ring 3 Controlled Page-Fault Test Task...\n");

    /* Allocate independent page directory */
    u64 *dir = create_page_directory();
    if (!dir) {
        serial_write("[ring3_test] failed to allocate page directory\n");
        return -1;
    }

    /* Allocate 4MB physical block for user space */
    char *user_mem = (char *)kmalloc_aligned(0x400000, 0x400000);
    if (!user_mem) {
        serial_write("[ring3_test] failed to allocate 4MB user memory\n");
        return -1;
    }

    /* Zero the allocated user memory */
    unsigned int *p = (unsigned int *)user_mem;
    for (int k = 0; k < 0x100000; k++) p[k] = 0;

    /* Map virtual 0x04000000 (entry 16) with User | RW | Present (0x87) */
    dir[16] = ((u64)(uintptr_t)user_mem) | 0x87;

    /* Copy user payload code to virtual 0x04000000 */
    for (unsigned int c = 0; c < sizeof(fault_user_code); c++) {
        user_mem[c] = (char)fault_user_code[c];
    }

    /* Place notice string at virtual 0x04000100 */
    copy_string(user_mem + 0x100, "[ring3_fault] attempting read from supervisor page 0x00100000...\n");

    /* User stack top at the end of the 4MB block */
    u64 user_stack_top = 0x04400000 - 4;
    u64 entry_point = 0x04000000;

    int tid = task_create_user((u64)(uintptr_t)dir, entry_point, user_stack_top, (u64)(uintptr_t)user_mem + 0x400000 - 4);
    if (tid >= 0) {
        copy_string(tasks[tid].name, "r3-fault");
        serial_write("[ring3_test] Ring 3 fault test task spawned successfully (tid=");
        serial_write_hex((unsigned int)tid);
        serial_write(")\n");
        return tid;
    }

    serial_write("[ring3_test] task_create_user failed for fault test\n");
    return -1;
}

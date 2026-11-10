#include <pthread.h>

#include "io.h"

extern int task_create(u64 cr3, u64 entry_point, u64 stack_top, u64 stack_phys);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg) {
    (void)attr;
    (void)arg;
    extern u64 kernel_pml4[512];
    u64 stack_top = 0; 
    
    int task_id = task_create((u64)(uintptr_t)kernel_pml4, (u64)(uintptr_t)start_routine, stack_top, 0);
    
    if (task_id < 0) return -1;
    
    *thread = (pthread_t)task_id;
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    while(1) {
        __asm__ volatile("sti; hlt"); 
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    while (__sync_lock_test_and_set((int*)mutex, 1)) {
        __asm__ volatile("pause");
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    __sync_lock_release((int*)mutex);
    return 0;
}

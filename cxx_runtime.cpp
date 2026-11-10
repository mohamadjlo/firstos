#include "cxx_runtime.h"

extern "C" {
    void* kmalloc(unsigned int size);
    void kfree(void* ptr);
    void serial_write(const char*);
}

void* operator new(size_t size) {
    return kmalloc(static_cast<unsigned int>(size));
}

void* operator new[](size_t size) {
    return kmalloc(static_cast<unsigned int>(size));
}

void operator delete(void* p) noexcept {
    if (p) kfree(p);
}

void operator delete[](void* p) noexcept {
    if (p) kfree(p);
}

void operator delete(void* p, size_t) noexcept {
    if (p) kfree(p);
}

void operator delete[](void* p, size_t) noexcept {
    if (p) kfree(p);
}

extern "C" {

void __cxa_pure_virtual(void) {
    serial_write("[CXX_RUNTIME] Pure virtual method called! Halting CPU.\n");
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

int __cxa_atexit(void (*)(void *), void *, void *) {
    // In a monolithic kernel environment, global destructors are not invoked
    return 0;
}

void* __dso_handle = nullptr;

}

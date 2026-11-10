#pragma once

#ifdef __cplusplus
typedef __SIZE_TYPE__ size_t;

// Freestanding C++ dynamic memory operators mapped to FirstOS kernel heap
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* p) noexcept;
void operator delete[](void* p) noexcept;
void operator delete(void* p, size_t size) noexcept;
void operator delete[](void* p, size_t size) noexcept;

// Placement new
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void* operator new[](size_t, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

extern "C" {
#endif

// C++ ABI stubs for freestanding kernel environment
void __cxa_pure_virtual(void);
int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso_handle);

#ifdef __cplusplus
}
#endif

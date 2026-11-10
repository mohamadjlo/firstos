#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#undef errno
extern int errno;

extern void serial_write(const char*);
extern void* kmalloc(unsigned int size);

static char *heap_end = 0;

void *_sbrk(int incr) {
    extern char _end; // Defined by linker.ld
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;

    heap_end += incr;
    return (void *)prev_heap_end;
}

int _fork(void) {
    errno = EAGAIN;
    return -1;
}

int _execve(char *name, char **argv, char **env) {
    errno = ENOMEM;
    return -1;
}

void _exit(int status) {
    serial_write("[POSIX] Process called exit()\n");
    while (1) { __asm__ volatile("cli; hlt"); }
}

int _wait(int *status) {
    errno = ECHILD;
    return -1;
}

int _getpid(void) {
    return 1;
}

int _kill(int pid, int sig) {
    errno = EINVAL;
    return -1;
}

int _open(const char *name, int flags, int mode) {
    return -1;
}

int _read(int file, char *ptr, int len) {
    return 0;
}

int _write(int file, char *ptr, int len) {
    if (file == 1 || file == 2) { // stdout or stderr
        // Quick and dirty serial write
        extern void serial_write(const char*);
        // serial_write expects null terminated string, but we get a buffer.
        // For safe printing, we can write character by character to port 0x3F8, 
        // or just let the caller handle it.
        // FirstOS has outb for serial output:
        extern void outb(unsigned short port, unsigned char data);
        for(int i = 0; i < len; i++) {
            outb(0x3F8, ptr[i]);
        }
        return len;
    }
    return -1;
}

int _close(int file) {
    return -1;
}

int _fstat(int file, struct stat *st) {
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file) {
    return (file <= 2) ? 1 : 0;
}

int _lseek(int file, int ptr, int dir) {
    return 0;
}

void _fini(void) {}
void _init(void) {}

// Provide non-underscored aliases for Newlib
void *sbrk(int incr) { return _sbrk(incr); }
int close(int file) { return _close(file); }
int lseek(int file, int ptr, int dir) { return _lseek(file, ptr, dir); }
int read(int file, char *ptr, int len) { return _read(file, ptr, len); }
int write(int file, char *ptr, int len) { return _write(file, ptr, len); }
int fork(void) { return _fork(); }
int execve(char *name, char **argv, char **env) { return _execve(name, argv, env); }
int wait(int *status) { return _wait(status); }
int kill(int pid, int sig) { return _kill(pid, sig); }
int getpid(void) { return _getpid(); }
int isatty(int file) { return _isatty(file); }
int fstat(int file, struct stat *st) { return _fstat(file, st); }

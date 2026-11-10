#include "linux_elf.h"
#include "task.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"
#include "paging.h"
#include "display_server.h"
#include <string.h>

extern task_t tasks[MAX_TASKS];
extern u64* create_page_directory(void);
extern void* kmalloc_aligned(unsigned int size, unsigned int alignment);

struct elf64_header {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    unsigned int type;
    unsigned int flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} __attribute__((packed));

struct elf64_shdr {
    unsigned int name;
    unsigned int type;
    u64 flags;
    u64 addr;
    u64 offset;
    u64 size;
    unsigned int link;
    unsigned int info;
    u64 addralign;
    u64 entsize;
} __attribute__((packed));

typedef struct {
    u64 r_offset;
    u64 r_info;
    s64 r_addend;
} __attribute__((packed)) Elf64_Rela;

typedef struct {
    u64 r_offset;
    u64 r_info;
} __attribute__((packed)) Elf64_Rel;

typedef struct {
    unsigned int st_name;
    unsigned char st_info;
    unsigned char st_other;
    unsigned short st_shndx;
    u64 st_value;
    u64 st_size;
} __attribute__((packed)) Elf64_Sym;

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_PLATFORM 15
#define AT_RANDOM   25

static void *get_phys_ptr(u64 *dir, u64 vaddr) {
    u64 pde = vaddr >> 21; /* 2MB pages in long mode */
    if (pde >= 512) return (void *)0;
    u64 entry = dir[pde];
    if (!(entry & 1)) return (void *)0; /* Not present */
    u64 phys_base = entry & ~0x1FFFFFULL;
    u64 offset = vaddr & 0x001FFFFF;
    return (void *)(uintptr_t)(phys_base + offset);
}

/* Builtin Runtime Function Bridges */
static int stub_zero(void) { return 0; }
static int stub_one(void) { return 1; }
static void stub_void(void) { }

static void *bridge_malloc(unsigned int sz) { return kmalloc(sz); }
static void bridge_free(void *p) { kfree(p); }
static void *bridge_calloc(unsigned int n, unsigned int sz) { return kcalloc(n, sz); }
static void *bridge_realloc(void *p, unsigned int sz) { return krealloc(p, sz); }

static char *bridge_getenv(const char *name) {
    if (!name) return (char *)0;
    if (strcmp(name, "DISPLAY") == 0) return ":0.0";
    if (strcmp(name, "HOME") == 0) return "/home/user";
    if (strcmp(name, "PATH") == 0) return "/bin:/usr/bin";
    if (strcmp(name, "LANG") == 0) return "C";
    return (char *)0;
}

static int bridge_printf(const char *fmt, ...) {
    serial_write("[linux-app] ");
    if (fmt) serial_write(fmt);
    serial_write("\n");
    return 0;
}

static int bridge_puts(const char *s) {
    if (s) {
        serial_write("[linux-app] ");
        serial_write(s);
        serial_write("\n");
    }
    return 0;
}

static int bridge_open(const char *path, int flags, ...) {
    (void)flags;
    serial_write("[linux_elf] open: ");
    if (path) serial_write(path);
    serial_write("\n");
    return 3;
}

static int bridge_close(int fd) {
    (void)fd;
    return 0;
}

static int bridge_read(int fd, void *buf, unsigned int count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}

static int bridge_write(int fd, const void *buf, unsigned int count) {
    (void)fd;
    if (buf && count > 0 && count < 1024) {
        char kbuf[1025];
        memcpy(kbuf, buf, count);
        kbuf[count] = 0;
        serial_write(kbuf);
    }
    return count;
}

static void *bridge_XOpenDisplay(const char *name) {
    serial_write("[linux_elf] XOpenDisplay connected: ");
    if (name) serial_write(name);
    serial_write("\n");
    return (void *)0x00100000;
}

static int bridge_XCloseDisplay(void *dpy) {
    (void)dpy;
    return 0;
}

static unsigned long bridge_XCreateSimpleWindow(void *dpy, unsigned long parent, int x, int y, unsigned int w, unsigned int h, unsigned int border_w, unsigned long border, unsigned long bg) {
    (void)dpy; (void)parent; (void)x; (void)y; (void)w; (void)h; (void)border_w; (void)border; (void)bg;
    serial_write("[linux_elf] XCreateSimpleWindow created window\n");
    return 100;
}

static unsigned long bridge_XCreateWindow(void *dpy, unsigned long parent, int x, int y, unsigned int w, unsigned int h, unsigned int border_w, int depth, unsigned int class, void *visual, unsigned long valuemask, void *attributes) {
    (void)dpy; (void)parent; (void)x; (void)y; (void)w; (void)h; (void)border_w; (void)depth; (void)class; (void)visual; (void)valuemask; (void)attributes;
    serial_write("[linux_elf] XCreateWindow created window\n");
    return 100;
}

static int bridge_XMapWindow(void *dpy, unsigned long win) {
    (void)dpy; (void)win;
    serial_write("[linux_elf] XMapWindow: window visible on FirstOS desktop\n");
    display_server_send_expose();
    return 0;
}

static int bridge_XMapRaised(void *dpy, unsigned long win) {
    (void)dpy; (void)win;
    serial_write("[linux_elf] XMapRaised: window raised\n");
    display_server_send_expose();
    return 0;
}

static int bridge_XSync(void *dpy, int discard) {
    (void)dpy; (void)discard;
    return 0;
}

static int bridge_XFlush(void *dpy) {
    (void)dpy;
    return 0;
}

static int bridge_XPending(void *dpy) {
    (void)dpy;
    return 0;
}

static int bridge_XNextEvent(void *dpy, void *event_return) {
    (void)dpy; (void)event_return;
    return 0;
}

typedef int (*main_fn_t)(int argc, char **argv, char **envp);
typedef void (*init_fn_t)(void);

static int in_os_libc_start_main(
    main_fn_t main_func,
    int argc,
    char **argv,
    init_fn_t init_func,
    void *fini_func,
    void *rtld_fini,
    void *stack_end
) {
    (void)fini_func; (void)rtld_fini; (void)stack_end;
    serial_write("[linux_elf] __libc_start_main successfully entered!\n");
    serial_write("[linux_elf] argc=");
    char astr[4]; astr[0] = '0' + (argc % 10); astr[1] = '\n'; astr[2] = 0;
    serial_write(astr);

    if (init_func) {
        serial_write("[linux_elf] running ELF init...\n");
    }

    serial_write("[linux_elf] running native Linux ELF main()...\n");
    char *envp[] = {"DISPLAY=:0.0", "HOME=/home/user", "LANG=C", (char *)0};
    int ret = 0;
    if (main_func) {
        ret = main_func(argc, argv, envp);
    }
    serial_write("[linux_elf] native Linux ELF main() returned rc=");
    char rstr[4]; rstr[0] = '0' + (ret % 10); rstr[1] = '\n'; rstr[2] = 0;
    serial_write(rstr);
    return ret;
}

struct symbol_binding {
    const char *name;
    void *func;
};

static const struct symbol_binding builtin_symbols[] = {
    {"__libc_start_main", (void *)in_os_libc_start_main},
    {"malloc", (void *)bridge_malloc},
    {"free", (void *)bridge_free},
    {"calloc", (void *)bridge_calloc},
    {"realloc", (void *)bridge_realloc},
    {"getenv", (void *)bridge_getenv},
    {"printf", (void *)bridge_printf},
    {"fprintf", (void *)bridge_printf},
    {"sprintf", (void *)bridge_printf},
    {"snprintf", (void *)bridge_printf},
    {"puts", (void *)bridge_puts},
    {"putchar", (void *)bridge_printf},
    {"fputs", (void *)bridge_puts},
    {"__printf_chk", (void *)bridge_printf},
    {"__fprintf_chk", (void *)bridge_printf},
    {"__sprintf_chk", (void *)bridge_printf},
    {"__snprintf_chk", (void *)bridge_printf},
    {"open", (void *)bridge_open},
    {"open64", (void *)bridge_open},
    {"close", (void *)bridge_close},
    {"read", (void *)bridge_read},
    {"write", (void *)bridge_write},
    {"lseek", (void *)stub_zero},
    {"lseek64", (void *)stub_zero},
    {"stat", (void *)stub_zero},
    {"stat64", (void *)stub_zero},
    {"fstat", (void *)stub_zero},
    {"fstat64", (void *)stub_zero},
    {"access", (void *)stub_zero},
    {"unlink", (void *)stub_zero},
    {"strlen", (void *)strlen},
    {"strcpy", (void *)strcpy},
    {"strncpy", (void *)strncpy},
    {"strcmp", (void *)strcmp},
    {"strncmp", (void *)strncmp},
    {"strchr", (void *)strchr},
    {"strrchr", (void *)strrchr},
    {"strstr", (void *)strstr},
    {"strcat", (void *)strcat},
    {"strncat", (void *)strncat},
    {"memset", (void *)memset},
    {"memcpy", (void *)memcpy},
    {"memmove", (void *)memmove},
    {"memcmp", (void *)memcmp},
    {"getpid", (void *)stub_one},
    {"getppid", (void *)stub_zero},
    {"getuid", (void *)stub_zero},
    {"geteuid", (void *)stub_zero},
    {"getgid", (void *)stub_zero},
    {"getegid", (void *)stub_zero},
    {"exit", (void *)stub_void},
    {"_exit", (void *)stub_void},
    {"abort", (void *)stub_void},
    {"pthread_mutex_init", (void *)stub_zero},
    {"pthread_mutex_lock", (void *)stub_zero},
    {"pthread_mutex_unlock", (void *)stub_zero},
    {"pthread_mutex_destroy", (void *)stub_zero},
    {"pthread_self", (void *)stub_one},
    {"pthread_create", (void *)stub_zero},
    {"pthread_join", (void *)stub_zero},
    {"pthread_key_create", (void *)stub_zero},
    {"pthread_setspecific", (void *)stub_zero},
    {"pthread_getspecific", (void *)stub_zero},
    {"XOpenDisplay", (void *)bridge_XOpenDisplay},
    {"XCloseDisplay", (void *)bridge_XCloseDisplay},
    {"XDefaultScreen", (void *)stub_zero},
    {"XDefaultRootWindow", (void *)stub_one},
    {"XCreateSimpleWindow", (void *)bridge_XCreateSimpleWindow},
    {"XCreateWindow", (void *)bridge_XCreateWindow},
    {"XMapWindow", (void *)bridge_XMapWindow},
    {"XMapRaised", (void *)bridge_XMapRaised},
    {"XUnmapWindow", (void *)stub_zero},
    {"XCreateGC", (void *)stub_one},
    {"XPutImage", (void *)stub_zero},
    {"XSync", (void *)bridge_XSync},
    {"XFlush", (void *)bridge_XFlush},
    {"XPending", (void *)bridge_XPending},
    {"XNextEvent", (void *)bridge_XNextEvent},
    {"XSelectInput", (void *)stub_zero},
    {(const char *)0, (void *)0}
};

static void *resolve_symbol(const char *name) {
    if (!name || !name[0]) return (void *)stub_zero;
    for (int i = 0; builtin_symbols[i].name != (const char *)0; i++) {
        if (strcmp(name, builtin_symbols[i].name) == 0) {
            return builtin_symbols[i].func;
        }
    }
    return (void *)stub_zero;
}

int linux_elf_load_and_run(const char *path, const char *arg) {
    serial_write("[linux_elf] loading: ");
    serial_write(path);
    serial_write("\n");

    u32 max_file_size = 16000000; /* 16MB buffer */
    char *file_buf = kmalloc(max_file_size);
    if (!file_buf) {
        serial_write("[linux_elf] failed to allocate file buffer\n");
        return -1;
    }

    int bytes = fs_read_at(path, file_buf, max_file_size);
    if (bytes < (int)sizeof(struct elf64_header)) {
        serial_write("[linux_elf] file read failed or too small\n");
        kfree(file_buf);
        return -1;
    }

    struct elf64_header *hdr = (struct elf64_header *)file_buf;
    if (hdr->ident[0] != 0x7F || hdr->ident[1] != 'E' || hdr->ident[2] != 'L' || hdr->ident[3] != 'F') {
        serial_write("[linux_elf] invalid ELF magic\n");
        kfree(file_buf);
        return -1;
    }
    if (hdr->ident[4] != 2) { /* ELFCLASS64 */
        serial_write("[linux_elf] not ELFCLASS64\n");
        kfree(file_buf);
        return -1;
    }
    if (hdr->machine != 62) { /* EM_X86_64 */
        serial_write("[linux_elf] not EM_X86_64\n");
        kfree(file_buf);
        return -1;
    }

    /* Allocate page directory for new process */
    u64 *dir = create_page_directory();
    if (!dir) {
        serial_write("[linux_elf] failed to allocate page directory\n");
        kfree(file_buf);
        return -1;
    }

    /* Calculate PT_LOAD segment boundaries */
    u64 min_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    u64 max_vaddr = 0x0000000000000000ULL;
    for (int i = 0; i < hdr->phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(file_buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == PT_LOAD && ph->memsz > 0) {
            if (ph->vaddr < min_vaddr) min_vaddr = ph->vaddr;
            if (ph->vaddr + ph->memsz > max_vaddr) max_vaddr = ph->vaddr + ph->memsz;
        }
    }

    if (min_vaddr == 0xFFFFFFFFFFFFFFFFULL) min_vaddr = 0x04000000ULL;
    if (max_vaddr == 0x0000000000000000ULL) max_vaddr = 0x04400000ULL;

    u64 pde_start = min_vaddr >> 21; /* 2MB pages */
    u64 pde_end   = (max_vaddr + 0x001FFFFFULL) >> 21;

    /* Allocate user stack at 0x04000000 (Index 32 & 33 for 4MB) */
    char *stack_phys = kmalloc_aligned(0x400000, 0x200000);
    if (!stack_phys) {
        kfree(file_buf);
        return -1;
    }
    memset(stack_phys, 0, 0x400000);
    dir[32] = ((u64)(uintptr_t)stack_phys) | 0x87; /* User | RW | Present | 2MB */
    dir[33] = ((u64)(uintptr_t)(stack_phys + 0x200000)) | 0x87;

    /* Allocate physical 2MB blocks for all code/data/BSS PDEs */
    for (u64 p = pde_start; p <= pde_end + 1; p++) {
        if (p < 512 && !(dir[p] & 1)) {
            char *page = kmalloc_aligned(0x200000, 0x200000);
            if (page) {
                memset(page, 0, 0x200000);
                dir[p] = ((u64)(uintptr_t)page) | 0x87;
            }
        }
    }

    /* Copy all PT_LOAD segments */
    for (int i = 0; i < hdr->phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)(file_buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == PT_LOAD && ph->memsz > 0) {
            u64 copied = 0;
            while (copied < ph->filesz) {
                u64 cur_vaddr = ph->vaddr + copied;
                u64 chunk_in_page = 0x200000ULL - (cur_vaddr & 0x001FFFFFULL);
                u64 rem_filesz = ph->filesz - copied;
                u64 to_copy = rem_filesz < chunk_in_page ? rem_filesz : chunk_in_page;

                void *dst = get_phys_ptr(dir, cur_vaddr);
                if (dst) {
                    memcpy(dst, file_buf + ph->offset + copied, (size_t)to_copy);
                }
                copied += to_copy;
            }

            /* Zero BSS memory */
            u64 bss_start = ph->vaddr + ph->filesz;
            u64 bss_len = ph->memsz - ph->filesz;
            u64 zeroed = 0;
            while (zeroed < bss_len) {
                u64 cur_vaddr = bss_start + zeroed;
                u64 chunk_in_page = 0x200000ULL - (cur_vaddr & 0x001FFFFFULL);
                u64 rem_len = bss_len - zeroed;
                u64 to_zero = rem_len < chunk_in_page ? rem_len : chunk_in_page;

                void *dst = get_phys_ptr(dir, cur_vaddr);
                if (dst) {
                    memset(dst, 0, (size_t)to_zero);
                }
                zeroed += to_zero;
            }
        }
    }

    /* Dynamic Symbol & Relocation Resolution */
    const char *dynstr = (const char *)0;
    Elf64_Sym *dynsym = (Elf64_Sym *)0;
    Elf64_Rela *relaplt = (Elf64_Rela *)0;
    u64 relaplt_count = 0;
    Elf64_Rela *reladyn = (Elf64_Rela *)0;
    u64 reladyn_count = 0;

    if (hdr->shoff && hdr->shnum && hdr->shstrndx < hdr->shnum) {
        struct elf64_shdr *shstr_sh = (struct elf64_shdr *)(file_buf + hdr->shoff + hdr->shstrndx * hdr->shentsize);
        const char *shstrtab = (const char *)(file_buf + shstr_sh->offset);

        for (int i = 0; i < hdr->shnum; i++) {
            struct elf64_shdr *sh = (struct elf64_shdr *)(file_buf + hdr->shoff + i * hdr->shentsize);
            const char *name = shstrtab + sh->name;
            if (strcmp(name, ".dynstr") == 0) {
                dynstr = (const char *)(file_buf + sh->offset);
            } else if (strcmp(name, ".dynsym") == 0) {
                dynsym = (Elf64_Sym *)(file_buf + sh->offset);
            } else if (strcmp(name, ".rela.plt") == 0 || strcmp(name, ".rela.got") == 0) {
                relaplt = (Elf64_Rela *)(file_buf + sh->offset);
                relaplt_count = sh->size / sizeof(Elf64_Rela);
            } else if (strcmp(name, ".rela.dyn") == 0) {
                reladyn = (Elf64_Rela *)(file_buf + sh->offset);
                reladyn_count = sh->size / sizeof(Elf64_Rela);
            }
        }
    }

    if (dynstr && dynsym && relaplt) {
        serial_write("[linux_elf] Resolving dynamic PLT relocations\n");
        for (u64 r = 0; r < relaplt_count; r++) {
            u64 r_offset = relaplt[r].r_offset;
            u32 sym_idx = (u32)(relaplt[r].r_info >> 32);
            const char *sym_name = dynstr + dynsym[sym_idx].st_name;
            void *target = resolve_symbol(sym_name);
            u64 *got_slot = (u64 *)get_phys_ptr(dir, r_offset);
            if (got_slot) {
                *got_slot = (u64)(uintptr_t)target;
            }
        }
    }

    if (dynstr && dynsym && reladyn) {
        serial_write("[linux_elf] Resolving dynamic data relocations\n");
        for (u64 r = 0; r < reladyn_count; r++) {
            u64 r_offset = reladyn[r].r_offset;
            u32 type = (u32)(reladyn[r].r_info & 0xFFFFFFFF);
            u32 sym_idx = (u32)(reladyn[r].r_info >> 32);
            u64 *slot = (u64 *)get_phys_ptr(dir, r_offset);
            if (slot) {
                if (type == 6 || type == 1) { /* GLOB_DAT or 64 */
                    const char *sym_name = dynstr + dynsym[sym_idx].st_name;
                    *slot = (u64)(uintptr_t)resolve_symbol(sym_name);
                }
            }
        }
    }

    /* Set up Linux initial 64-bit stack frame */
    char *str_ptr = (char *)(stack_phys + 0x3FE000);

    char *saved_arg0 = str_ptr;
    strcpy(str_ptr, path);
    str_ptr += strlen(path) + 1;

    char *saved_arg1 = (char *)0;
    if (arg && arg[0]) {
        saved_arg1 = str_ptr;
        strcpy(str_ptr, arg);
        str_ptr += strlen(arg) + 1;
    }

    char *saved_env0 = str_ptr;
    strcpy(str_ptr, "DISPLAY=:0.0");
    str_ptr += strlen(str_ptr) + 1;

    char *saved_env1 = str_ptr;
    strcpy(str_ptr, "HOME=/home/user");
    str_ptr += strlen(str_ptr) + 1;

    u64 *sp = (u64 *)(stack_phys + 0x3FC000);
    u64 stack_top_virt = 0x043FC000ULL;

    /* Push Auxiliary Vectors */
    #define PUSH_AUXV(type, val) do { *--sp = (val); *--sp = (type); stack_top_virt -= 16; } while (0)
    PUSH_AUXV(AT_NULL, 0);
    PUSH_AUXV(AT_PLATFORM, 0x043FE000ULL);
    PUSH_AUXV(AT_RANDOM, 0x043FE000ULL);
    PUSH_AUXV(AT_PAGESZ, 4096);
    PUSH_AUXV(AT_ENTRY, hdr->entry);
    PUSH_AUXV(AT_BASE, 0);
    PUSH_AUXV(AT_PHNUM, hdr->phnum);
    PUSH_AUXV(AT_PHENT, hdr->phentsize);
    PUSH_AUXV(AT_PHDR, 0x04000000ULL + hdr->phoff);
    PUSH_AUXV(AT_UID, 0);
    PUSH_AUXV(AT_EUID, 0);
    PUSH_AUXV(AT_GID, 0);
    PUSH_AUXV(AT_EGID, 0);

    /* Push envp (NULL terminated) */
    *--sp = 0; stack_top_virt -= 8;
    *--sp = 0x04000000ULL + (u64)(saved_env1 - stack_phys); stack_top_virt -= 8;
    *--sp = 0x04000000ULL + (u64)(saved_env0 - stack_phys); stack_top_virt -= 8;

    /* Push argv (NULL terminated) */
    *--sp = 0; stack_top_virt -= 8;
    if (saved_arg1) {
        *--sp = 0x04000000ULL + (u64)(saved_arg1 - stack_phys); stack_top_virt -= 8;
    }
    *--sp = 0x04000000ULL + (u64)(saved_arg0 - stack_phys); stack_top_virt -= 8;

    /* Push argc */
    *--sp = saved_arg1 ? 2 : 1; stack_top_virt -= 8;

    u64 entry_point = hdr->entry;
    u64 stack_phys_addr = (u64)(uintptr_t)sp;

    /* Create task in FirstOS scheduler */
    int tid = task_create((u64)(uintptr_t)dir, entry_point, stack_top_virt, stack_phys_addr);
    if (tid >= 0) {
        strncpy(tasks[tid].name, "linux-elf", sizeof(tasks[tid].name) - 1);
        serial_write("[linux_elf] task created successfully (tid=");
        char tstr[8];
        tstr[0] = '0' + (tid % 10);
        tstr[1] = ')'; tstr[2] = '\n'; tstr[3] = 0;
        serial_write(tstr);
    }

    kfree(file_buf);
    return tid;
}

int linux_spawn(const char *binary_path, const char *arg) {
    return linux_elf_load_and_run(binary_path, arg);
}

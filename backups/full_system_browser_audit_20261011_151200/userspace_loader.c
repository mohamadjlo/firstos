#include "userspace_loader.h"
#ifndef HOST_TEST
#include "task.h"
#include "paging.h"
#include "heap.h"
#include "fs.h"
#include "serial.h"

#ifndef FIRSTOS_EMBEDDED_USER_FALLBACK
#define FIRSTOS_EMBEDDED_USER_FALLBACK 1
#endif

#if FIRSTOS_EMBEDDED_USER_FALLBACK
extern const char userspace_hello_elf_start[];
extern const char userspace_hello_elf_end[];
#endif

extern task_t tasks[MAX_TASKS];
extern u64* create_page_directory(void);
extern void* kmalloc_aligned(unsigned int size, unsigned int alignment);
extern void kfree_aligned(void *ptr);
#endif

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

int userspace_validate_elf(const char *buf, unsigned int size, userspace_image_t *img) {
    if (!buf) return ELF_ERR_NULL_BUFFER;
    if (size < sizeof(struct elf64_header)) return ELF_ERR_TOO_SMALL;
    if (size > FIRSTOS_ELF_MAX_SIZE) return ELF_ERR_SEGMENT_FILE_RANGE;

    const struct elf64_header *hdr = (const struct elf64_header *)buf;

    /* Magic check: 0x7F 'E' 'L' 'F' */
    if (hdr->ident[0] != 0x7F || hdr->ident[1] != 'E' || hdr->ident[2] != 'L' || hdr->ident[3] != 'F') {
        return ELF_ERR_BAD_MAGIC;
    }

    /* ELFCLASS64 (2) check */
    if (hdr->ident[4] != 2) {
        return ELF_ERR_UNSUPPORTED_CLASS;
    }

    /* ELFDATA2LSB (1 = little-endian) check */
    if (hdr->ident[5] != 1) {
        return ELF_ERR_UNSUPPORTED_ENDIAN;
    }

    /* Version check */
    if (hdr->ident[6] != 1 || hdr->version != 1) {
        return ELF_ERR_UNSUPPORTED_VERSION;
    }

    /* Type check (ET_EXEC = 2) */
    if (hdr->type != 2) {
        return ELF_ERR_UNSUPPORTED_TYPE;
    }

    /* Machine check (EM_X86_64 = 62 = 0x3E) */
    if (hdr->machine != 62) {
        return ELF_ERR_UNSUPPORTED_MACHINE;
    }

    /* Program header table boundary check */
    if (hdr->phnum == 0 || hdr->phnum > FIRSTOS_ELF_MAX_PHDRS) {
        return ELF_ERR_PHDR_COUNT_EXCEEDED;
    }
    if (hdr->phentsize < sizeof(struct elf64_phdr)) {
        return ELF_ERR_INVALID_PHDR_RANGE;
    }

    u64 ph_table_bytes = (u64)hdr->phnum * (u64)hdr->phentsize;
    if (hdr->phoff > size || ph_table_bytes > (u64)size - hdr->phoff) {
        return ELF_ERR_INVALID_PHDR_RANGE;
    }

    u64 min_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    u64 max_vaddr = 0;
    int load_segs_count = 0;
    int entry_in_exec_segment = 0;

    for (unsigned int i = 0; i < hdr->phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(buf + hdr->phoff + i * hdr->phentsize);

        /* Disallow dynamic loading / interpreter */
        if (ph->type == 2 /* PT_DYNAMIC */ || ph->type == 3 /* PT_INTERP */) {
            return ELF_ERR_DYNAMIC_NOT_SUPPORTED;
        }

        if (ph->type == 1 /* PT_LOAD */) {
            load_segs_count++;
            if (load_segs_count > FIRSTOS_ELF_MAX_LOAD_SEGS) {
                return ELF_ERR_PHDR_COUNT_EXCEEDED;
            }

            if (ph->filesz > ph->memsz) {
                return ELF_ERR_SEGMENT_MEM_RANGE;
            }

            /* Overflow checks */
            if (ph->offset > size || ph->filesz > (u64)size - ph->offset) {
                return ELF_ERR_SEGMENT_FILE_RANGE;
            }
            if (ph->vaddr + ph->memsz < ph->vaddr) {
                return ELF_ERR_SEGMENT_MEM_RANGE;
            }

            /* Boundary checks: Must stay within user address space */
            if (ph->vaddr < FIRSTOS_USER_BASE || (ph->vaddr + ph->memsz) > FIRSTOS_USER_END) {
                return ELF_ERR_SEGMENT_OUT_OF_USER;
            }

            /* Code/Data segments must not overlap heap */
            if ((ph->vaddr + ph->memsz) > FIRSTOS_USER_HEAP_BASE) {
                return ELF_ERR_SEGMENT_OVERLAPS_HEAP;
            }

            if (ph->vaddr < min_vaddr) min_vaddr = ph->vaddr;
            if (ph->vaddr + ph->memsz > max_vaddr) max_vaddr = ph->vaddr + ph->memsz;

            /* Check if entry point falls into an executable PT_LOAD segment */
            if ((ph->flags & 1 /* PF_X */) && hdr->entry >= ph->vaddr && hdr->entry < (ph->vaddr + ph->memsz)) {
                entry_in_exec_segment = 1;
            }
        }
    }

    if (load_segs_count == 0) {
        return ELF_ERR_SEGMENT_MEM_RANGE;
    }

    if (!entry_in_exec_segment) {
        return ELF_ERR_ENTRY_OUT_OF_SEGMENT;
    }

    if (img) {
        img->entry = hdr->entry;
        img->image_base = min_vaddr;
        img->image_end = max_vaddr;
        img->heap_base = FIRSTOS_USER_HEAP_BASE;
        img->stack_top = FIRSTOS_USER_STACK_TOP;
        img->argc = 0;
        img->argv_user = 0;
    }

    return ELF_OK;
}

#ifndef HOST_TEST
static void u_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static unsigned int u_strlen_k(const char *s) {
    unsigned int len = 0;
    while (s[len]) len++;
    return len;
}

int userspace_spawn_elf_buffer(const char *elf_buf, unsigned int elf_size, const char *task_name, int argc, const char **argv) {
    userspace_image_t img;
    int val_rc = userspace_validate_elf(elf_buf, elf_size, &img);
    if (val_rc != ELF_OK) {
        serial_write("[userspace] ERROR: ELF validation failed (code=");
        serial_write_hex((unsigned int)-val_rc);
        serial_write(")\n");
        return val_rc;
    }

    const struct elf64_header *hdr = (const struct elf64_header *)elf_buf;

    serial_write("[userspace] ELF_VALIDATE ok class=64 machine=x86_64 type=EXEC\n");
    serial_write("[userspace] ELF_SEGMENTS count=");
    serial_write_hex(hdr->phnum);
    serial_write("\n");

    /* Allocate independent page tables */
    u64 *pml4 = create_page_directory();
    if (!pml4) {
        serial_write("[userspace] ERROR: failed to allocate page directory\n");
        return ELF_ERR_ALLOC_FAILED;
    }
    u64 *pdpt = (u64 *)kmalloc_aligned(4096, 4096);
    u64 *pd   = (u64 *)kmalloc_aligned(4096, 4096);
    if (!pdpt || !pd) {
        serial_write("[userspace] ERROR: failed to allocate page tables\n");
        if (pdpt) kfree_aligned(pdpt);
        if (pd) kfree_aligned(pd);
        kfree_aligned(pml4);
        return ELF_ERR_ALLOC_FAILED;
    }
    for (int i = 0; i < 512; i++) pdpt[i] = kernel_pdpt[i];
    for (int i = 0; i < 512; i++) pd[i] = kernel_pd[0][i];
    pml4[0] = ((u64)(uintptr_t)pdpt) | 0x07;
    pdpt[0] = ((u64)(uintptr_t)pd) | 0x07;

    /* Allocate 4MB physical block for user space */
    char *user_mem = (char *)kmalloc_aligned(FIRSTOS_USER_SIZE, 0x400000);
    if (!user_mem) {
        serial_write("[userspace] ERROR: failed to allocate 4MB user memory\n");
        kfree_aligned(pdpt);
        kfree_aligned(pd);
        kfree_aligned(pml4);
        return ELF_ERR_ALLOC_FAILED;
    }

    /* Zero the allocated user memory */
    u64 *p = (u64 *)user_mem;
    for (int k = 0; k < (int)(FIRSTOS_USER_SIZE / sizeof(u64)); k++) p[k] = 0;

    /* In x86_64 Long Mode, each PDE maps 2MB. 4MB covers 2 PDE entries:
     * FIRSTOS_USER_PDE_INDEX (32) covers 64MB..66MB
     * FIRSTOS_USER_PDE_INDEX + 1 (33) covers 66MB..68MB */
    pd[FIRSTOS_USER_PDE_INDEX] = ((u64)(uintptr_t)user_mem) | 0x87;
    pd[FIRSTOS_USER_PDE_INDEX + 1] = ((u64)(uintptr_t)(user_mem + 0x200000ULL)) | 0x87;

    /* Copy PT_LOAD segments & zero BSS */
    for (int i = 0; i < hdr->phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(elf_buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == 1 /* PT_LOAD */ && ph->memsz > 0) {
            serial_write("[userspace] ELF_LOAD segment=");
            serial_write_hex((unsigned int)i);
            serial_write(" vaddr=");
            serial_write_hex((u32)ph->vaddr);
            serial_write(" filesz=");
            serial_write_hex((u32)ph->filesz);
            serial_write(" memsz=");
            serial_write_hex((u32)ph->memsz);
            serial_write("\n");

            u64 offset = ph->vaddr - FIRSTOS_USER_BASE;
            char *dest = user_mem + offset;
            const char *src = elf_buf + ph->offset;

            for (u64 j = 0; j < ph->filesz; j++) {
                dest[j] = src[j];
            }
            /* Zero remaining memory (BSS) */
            for (u64 j = ph->filesz; j < ph->memsz; j++) {
                dest[j] = 0;
            }
        }
    }

    /* Set up argc and argv on User Stack */
    if (argc < 0) argc = 0;
    if (argc > FIRSTOS_ELF_MAX_ARGC) argc = FIRSTOS_ELF_MAX_ARGC;

    u64 str_offset = FIRSTOS_USER_ARG_BASE - FIRSTOS_USER_BASE;
    u64 total_arg_bytes = 0;
    u64 v_args[FIRSTOS_ELF_MAX_ARGC];

    for (int a = 0; a < argc; a++) {
        const char *arg_str = argv ? argv[a] : "";
        if (!arg_str) arg_str = "";
        unsigned int len = u_strlen_k(arg_str);
        if (len >= FIRSTOS_ELF_MAX_ARG_LEN) len = FIRSTOS_ELF_MAX_ARG_LEN - 1;

        if (total_arg_bytes + len + 1 > FIRSTOS_ELF_MAX_TOTAL_ARG_LEN) {
            len = FIRSTOS_ELF_MAX_TOTAL_ARG_LEN - total_arg_bytes - 1;
        }

        char *dest_str = user_mem + str_offset;
        for (unsigned int c = 0; c < len; c++) {
            dest_str[c] = arg_str[c];
        }
        dest_str[len] = '\0';

        v_args[a] = FIRSTOS_USER_BASE + str_offset;
        str_offset += len + 1;
        total_arg_bytes += len + 1;
    }

    /* 16-byte aligned User stack pointer for System V AMD64 ABI */
    u64 user_rsp = FIRSTOS_USER_STACK_TOP;
    u64 stack_offset = user_rsp - FIRSTOS_USER_BASE;
    u64 *stack_slots = (u64 *)(user_mem + stack_offset);

    /* System V AMD64 initial stack layout:
     * [rsp + 0] = argc (64-bit)
     * [rsp + 8] = argv[0] (64-bit)
     * ...
     * [rsp + 8*(argc+1)] = NULL
     * [rsp + 8*(argc+2)] = NULL (envp NULL)
     */
    stack_slots[0] = (u64)argc;
    for (int a = 0; a < argc; a++) {
        stack_slots[1 + a] = v_args[a];
    }
    stack_slots[1 + argc] = 0; /* NULL terminator for argv */
    stack_slots[2 + argc] = 0; /* NULL terminator for envp */

    serial_write("[userspace] STACK_BUILD argc=");
    serial_write_hex((unsigned int)argc);
    serial_write("\n");
    serial_write("[userspace] LOAD name=");
    serial_write(task_name ? task_name : "user_task");
    serial_write("\n");
    serial_write("[userspace] ENTRY rip=");
    serial_write_hex((u32)hdr->entry);
    serial_write(" rsp=");
    serial_write_hex((u32)user_rsp);
    serial_write("\n");
    serial_write("[userspace] RING3_ENTER cpl=3\n");

    int tid = task_create_user((u64)(uintptr_t)pml4, hdr->entry, user_rsp, (u64)(uintptr_t)user_mem + FIRSTOS_USER_SIZE - 8);
    if (tid >= 0) {
        if (task_name) {
            u_strcpy(tasks[tid].name, task_name);
        } else {
            u_strcpy(tasks[tid].name, "u-task");
        }
        return tid;
    }

    serial_write("[userspace] ERROR: task_create_user failed\n");
    kfree_aligned(user_mem);
    kfree_aligned(pdpt);
    kfree_aligned(pd);
    kfree_aligned(pml4);
    return -1;
}

int userspace_spawn_from_fs(const char *path, int argc, const char **argv) {
    if (!path || !path[0]) {
        serial_write("[userspace] ERROR: null or empty path\n");
        return -1;
    }

    if (!fs_ready()) {
        serial_write("[userspace] ERROR: filesystem not ready\n");
        return -1;
    }

    serial_write("[userspace] FS_OPEN path=");
    serial_write(path);
    serial_write("\n");

    int file_size = fs_read_at(path, 0, 0);
    if (file_size <= 0) {
        serial_write("[userspace] ERROR: file not found or empty: ");
        serial_write(path);
        serial_write("\n");
        return -1;
    }

    if ((unsigned int)file_size > FIRSTOS_ELF_MAX_SIZE) {
        serial_write("[userspace] ERROR: file exceeds max size limit\n");
        return -1;
    }

    serial_write("[userspace] FS_SIZE bytes=");
    serial_write_hex((unsigned int)file_size);
    serial_write("\n");

    char *file_buf = (char *)kmalloc((unsigned int)file_size);
    if (!file_buf) {
        serial_write("[userspace] ERROR: kmalloc failed for file buffer\n");
        return -1;
    }

    int read_bytes = fs_read_raw_at(path, file_buf, (unsigned int)file_size);
    if (read_bytes != file_size) {
        serial_write("[userspace] ERROR: fs_read_raw_at incomplete\n");
        kfree(file_buf);
        return -1;
    }

    int tid = userspace_spawn_elf_buffer(file_buf, (unsigned int)file_size, path, argc, argv);
    kfree(file_buf);
    return tid;
}

int userspace_hello_spawn(void) {
    const char *default_args[] = {
        "userspace_hello",
        "test_arg_alpha",
        "test_arg_beta"
    };

    /* Attempt default filesystem execution path */
    int tid = userspace_spawn_from_fs("/userspace_hello.elf", 3, default_args);
    if (tid >= 0) {
        return tid;
    }

#if FIRSTOS_EMBEDDED_USER_FALLBACK
    serial_write("[userspace] NOTICE: Falling back to embedded ELF binary\n");
    return userspace_spawn_elf_buffer(
        userspace_hello_elf_start,
        (unsigned int)(userspace_hello_elf_end - userspace_hello_elf_start),
        "u-hello",
        3,
        default_args
    );
#else
    serial_write("[userspace] ERROR: Failed to load /userspace_hello.elf and fallback is disabled\n");
    return -1;
#endif
}
#endif /* !HOST_TEST */

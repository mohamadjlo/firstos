#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);

#include "include/firstos/uapi/memory.h"
#include "userspace_loader.h"

/* ELF Structures for mock testing */
struct elf_header {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned int entry;
    unsigned int phoff;
    unsigned int shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} __attribute__((packed));

struct elf_phdr {
    unsigned int type;
    unsigned int offset;
    unsigned int vaddr;
    unsigned int paddr;
    unsigned int filesz;
    unsigned int memsz;
    unsigned int flags;
    unsigned int align;
} __attribute__((packed));

static void build_valid_elf(char *buf, unsigned int buf_size) {
    for (unsigned int k = 0; k < buf_size; k++) buf[k] = 0;
    struct elf_header *hdr = (struct elf_header *)buf;

    /* e_ident */
    hdr->ident[0] = 0x7F;
    hdr->ident[1] = 'E';
    hdr->ident[2] = 'L';
    hdr->ident[3] = 'F';
    hdr->ident[4] = 1; /* ELFCLASS32 */
    hdr->ident[5] = 1; /* ELFDATA2LSB */
    hdr->ident[6] = 1; /* EV_CURRENT */

    hdr->type = 2;     /* ET_EXEC */
    hdr->machine = 3;  /* EM_386 */
    hdr->version = 1;
    hdr->entry = 0x04000000;
    hdr->phoff = sizeof(struct elf_header);
    hdr->ehsize = sizeof(struct elf_header);
    hdr->phentsize = sizeof(struct elf_phdr);
    hdr->phnum = 2;

    /* Segment 0: Code R+E */
    struct elf_phdr *ph0 = (struct elf_phdr *)(buf + hdr->phoff);
    ph0->type = 1; /* PT_LOAD */
    ph0->offset = 0x1000;
    ph0->vaddr = 0x04000000;
    ph0->paddr = 0x04000000;
    ph0->filesz = 0x500;
    ph0->memsz = 0x500;
    ph0->flags = 5; /* PF_R | PF_X */
    ph0->align = 0x1000;

    /* Segment 1: Data RW */
    struct elf_phdr *ph1 = (struct elf_phdr *)(buf + hdr->phoff + sizeof(struct elf_phdr));
    ph1->type = 1; /* PT_LOAD */
    ph1->offset = 0x1000;
    ph1->vaddr = 0x04001000;
    ph1->paddr = 0x04001000;
    ph1->filesz = 0x100;
    ph1->memsz = 0x200; /* BSS */
    ph1->flags = 6; /* PF_R | PF_W */
    ph1->align = 0x1000;
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Static ELF Loader & Validation Unit Tests\n");
    printf("============================================================\n");

    char elf_buf[8192];
    userspace_image_t img;

    /* TEST 1: Valid ELF */
    printf("[TEST 1] Testing valid 32-bit x86 ET_EXEC ELF...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    int rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_OK);
    assert(img.entry == 0x04000000);
    assert(img.image_base == 0x04000000);
    assert(img.image_end == 0x04001200);
    printf("  -> Valid ELF accepted: entry=0x%08X base=0x%08X end=0x%08X\n", img.entry, img.image_base, img.image_end);

    /* TEST 2: Bad Magic */
    printf("[TEST 2] Testing rejection of corrupted ELF magic...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    elf_buf[0] = 0x7E;
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_BAD_MAGIC);
    printf("  -> Bad magic rejected (ELF_ERR_BAD_MAGIC)\n");

    /* TEST 3: Unsupported Class (ELFCLASS64 = 2) */
    printf("[TEST 3] Testing rejection of 64-bit ELF...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    struct elf_header *hdr = (struct elf_header *)elf_buf;
    hdr->ident[4] = 2; /* 64-bit */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_UNSUPPORTED_CLASS);
    printf("  -> 64-bit class rejected (ELF_ERR_UNSUPPORTED_CLASS)\n");

    /* TEST 4: Unsupported Endianness (Big Endian = 2) */
    printf("[TEST 4] Testing rejection of big-endian ELF...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    hdr->ident[5] = 2;
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_UNSUPPORTED_ENDIAN);
    printf("  -> Big-endian rejected (ELF_ERR_UNSUPPORTED_ENDIAN)\n");

    /* TEST 5: Unsupported Machine (e.g. ARM = 40) */
    printf("[TEST 5] Testing rejection of non-x86 architecture...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    hdr->machine = 40; /* ARM */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_UNSUPPORTED_MACHINE);
    printf("  -> Non-x86 machine rejected (ELF_ERR_UNSUPPORTED_MACHINE)\n");

    /* TEST 6: Unsupported Type (ET_DYN = 3, shared lib / PIE) */
    printf("[TEST 6] Testing rejection of dynamic / PIE ELF...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    hdr->type = 3; /* ET_DYN */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_UNSUPPORTED_TYPE);
    printf("  -> ET_DYN rejected (ELF_ERR_UNSUPPORTED_TYPE)\n");

    /* TEST 7: Program header table offset beyond file size */
    printf("[TEST 7] Testing rejection of out-of-bounds program header table...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    hdr->phoff = sizeof(elf_buf) + 100;
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_INVALID_PHDR_RANGE);
    printf("  -> Out-of-bounds phdr table rejected (ELF_ERR_INVALID_PHDR_RANGE)\n");

    /* TEST 8: filesz > memsz */
    printf("[TEST 8] Testing rejection of segment where filesz > memsz...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    struct elf_phdr *ph0 = (struct elf_phdr *)(elf_buf + hdr->phoff);
    ph0->filesz = 0x600;
    ph0->memsz = 0x500;
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_SEGMENT_MEM_RANGE);
    printf("  -> filesz > memsz rejected (ELF_ERR_SEGMENT_MEM_RANGE)\n");

    /* TEST 9: Segment outside userspace (< FIRSTOS_USER_BASE) */
    printf("[TEST 9] Testing rejection of kernel-mapped segment (vaddr < 0x04000000)...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    ph0 = (struct elf_phdr *)(elf_buf + hdr->phoff);
    ph0->vaddr = 0x00100000; /* Supervisor area! */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_SEGMENT_OUT_OF_USER);
    printf("  -> Kernel-space segment rejected (ELF_ERR_SEGMENT_OUT_OF_USER)\n");

    /* TEST 10: Segment overlapping heap boundary (> FIRSTOS_USER_HEAP_BASE) */
    printf("[TEST 10] Testing rejection of segment colliding with heap...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    ph0 = (struct elf_phdr *)(elf_buf + hdr->phoff);
    ph0->vaddr = FIRSTOS_USER_HEAP_BASE + 0x1000;
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_SEGMENT_OVERLAPS_HEAP);
    printf("  -> Segment overlapping heap rejected (ELF_ERR_SEGMENT_OVERLAPS_HEAP)\n");

    /* TEST 11: Dynamic linker / PT_INTERP rejection */
    printf("[TEST 11] Testing rejection of PT_INTERP...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    ph0 = (struct elf_phdr *)(elf_buf + hdr->phoff);
    ph0->type = 3; /* PT_INTERP */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_DYNAMIC_NOT_SUPPORTED);
    printf("  -> Dynamic interpreter rejected (ELF_ERR_DYNAMIC_NOT_SUPPORTED)\n");

    /* TEST 12: Entry point outside executable segment */
    printf("[TEST 12] Testing rejection of non-executable entry point...\n");
    build_valid_elf(elf_buf, sizeof(elf_buf));
    hdr = (struct elf_header *)elf_buf;
    hdr->entry = 0x04001000; /* Points to Segment 1, which is RW, NOT executable! */
    rc = userspace_validate_elf(elf_buf, sizeof(elf_buf), &img);
    assert(rc == ELF_ERR_ENTRY_OUT_OF_SEGMENT);
    printf("  -> Non-executable entry point rejected (ELF_ERR_ENTRY_OUT_OF_SEGMENT)\n");

    /* TEST 13: Null buffer and truncated file */
    printf("[TEST 13] Testing null buffer and file too small...\n");
    rc = userspace_validate_elf(NULL, 100, &img);
    assert(rc == ELF_ERR_NULL_BUFFER);
    rc = userspace_validate_elf(elf_buf, 20, &img);
    assert(rc == ELF_ERR_TOO_SMALL);
    printf("  -> Null and truncated buffers safely handled\n");

    printf("============================================================\n");
    printf("All 13 Static ELF Validation Tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}

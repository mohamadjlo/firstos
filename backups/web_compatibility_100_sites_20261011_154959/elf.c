#include "task.h"
#include "elf.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"

extern task_t tasks[MAX_TASKS];

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

int elf_load_and_run(const char *path) {
    serial_write("[elf] before kmalloc\n"); char *buf = kmalloc(4000000); serial_write("[elf] after kmalloc\n");
    if (!buf) return -1;
    
    serial_write("[elf] before fs_read\n"); int bytes = fs_read_at(path, buf, 4000000); serial_write("[elf] after fs_read\n");
    if (bytes < (int)sizeof(struct elf_header)) { serial_write("[elf] read failed\n");
        kfree(buf);
        return -1;
    }
    
    struct elf_header *hdr = (struct elf_header *)buf;
    if (hdr->ident[0] != 0x7F || hdr->ident[1] != 'E' || hdr->ident[2] != 'L' || hdr->ident[3] != 'F') {
        kfree(buf);
        return -1;
    }
    
    // Allocate new page directory
    extern u64* create_page_directory(void);
    serial_write("[elf] before create_page_dir\n"); u64 *dir = create_page_directory(); serial_write("[elf] after create_page_dir\n");
    if (!dir) { serial_write("[elf] dir alloc failed\n"); kfree(buf); return -1; }
    
    // Allocate 4MB physical block for the ELF
    extern void* kmalloc_aligned(unsigned int size, unsigned int alignment);
    serial_write("[elf] before phys_mem\n"); char *phys_mem = kmalloc_aligned(0x400000, 0x400000); serial_write("[elf] after phys_mem\n");
    if (!phys_mem) { serial_write("[elf] phys_mem alloc failed\n"); kfree(buf); return -1; } // Leaks dir, fine for now
    
    // Zero the physical memory
    unsigned int *p = (unsigned int *)phys_mem;
    int k; for(k=0; k<0x100000; k++) p[k] = 0;
    
    // Map 0x04000000 to phys_mem in the new directory
    // 0x04000000 is 64MB. Index = 64 / 4 = 16.
    dir[16] = ((u64)(uintptr_t)phys_mem) | 0x83;
    
    // Copy ELF segments
    int i;
    for (i = 0; i < hdr->phnum; i++) {
        struct elf_phdr *ph = (struct elf_phdr *)(buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == 1) { // PT_LOAD
            // ph->vaddr is expected to be >= 0x04000000 and < 0x04400000
            if (ph->vaddr >= 0x04000000 && ph->vaddr < 0x04400000) {
                unsigned int offset = ph->vaddr - 0x04000000;
                char *dest = phys_mem + offset;
                char *src = buf + ph->offset;
                unsigned int j;
                for (j = 0; j < ph->filesz; j++) dest[j] = src[j];
            }
        }
    }
    
    // Create task
    
    // Stack top is at the end of the 4MB block
    u64 stack_top = 0x04400000 - 4;
    u64 stack_phys = (u64)(uintptr_t)phys_mem + 0x400000 - 4;
    serial_write("[elf] entry="); 
    char hex[16];
    int h; for(h=0; h<8; h++) { int v = (hdr->entry >> ((7-h)*4)) & 0xF; hex[h] = v < 10 ? '0'+v : 'A'+v-10; }
    hex[8] = '\n';
hex[9] = 0;
    serial_write(hex);
    /* Set task name from ELF path */
    {
        int tid = task_create((u64)(uintptr_t)dir, hdr->entry, stack_top, stack_phys);
        if (tid >= 0) {
            /* Copy basename of path into task name */
            int pi = 0, ni = 0, last_slash = -1;
            while (path[pi]) { if (path[pi]=='/') last_slash=pi; pi++; }
            pi = (last_slash >= 0) ? last_slash + 1 : 0;
            while (path[pi] && ni < 31) { tasks[tid].name[ni++] = path[pi++]; }
            tasks[tid].name[ni] = 0;
        }
    }
    
    serial_write("[elf] loaded into new task\n");
    
    kfree(buf);
    return 0;
}

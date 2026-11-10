#ifndef _FIRSTOS_UAPI_MEMORY_H
#define _FIRSTOS_UAPI_MEMORY_H

/* Userspace Virtual Address Range */
#define FIRSTOS_USER_BASE        0x04000000ULL
#define FIRSTOS_USER_END         0x04400000ULL
#define FIRSTOS_USER_SIZE        0x00400000ULL /* 4 MB */
#define FIRSTOS_USER_PDE_INDEX   32            /* 64MB / 2MB = 32 in x86_64 Long Mode */

/* Shared Memory Virtual Address Range */
#define FIRSTOS_USER_SHM_BASE     0x04400000ULL
#define FIRSTOS_USER_SHM_END      0x04800000ULL
#define FIRSTOS_USER_SHM_SIZE     0x00400000ULL /* 4 MB */
#define FIRSTOS_USER_SHM_PDE_INDEX 34           /* 68MB / 2MB = 34 in x86_64 Long Mode */
#define FIRSTOS_USER_TOTAL_END    FIRSTOS_USER_SHM_END

/* Memory Sub-regions within the 4MB User Block */
#define FIRSTOS_USER_CODE_BASE   0x04000000u  /* Code & data mapped here */
#define FIRSTOS_USER_HEAP_BASE   0x04020000u  /* Default heap break start (128KB code/data headroom) */
#define FIRSTOS_USER_HEAP_MAX    0x043E0000u  /* Max heap limit */

#define FIRSTOS_USER_STACK_TOP   0x043FFE00u  /* 16-byte aligned initial ESP */
#define FIRSTOS_USER_ARG_BASE    0x043FFF00u  /* String table base for argv */
#define FIRSTOS_USER_STACK_SIZE  0x00080000u  /* ~512 KB dedicated stack range */

/* ELF Loader Limits and Constraints */
#define FIRSTOS_ELF_MAX_SIZE          (4u * 1024u * 1024u) /* 4 MB max file size */
#define FIRSTOS_ELF_MAX_PHDRS         16
#define FIRSTOS_ELF_MAX_LOAD_SEGS     8
#define FIRSTOS_ELF_MAX_ARGC          16
#define FIRSTOS_ELF_MAX_ARG_LEN       128
#define FIRSTOS_ELF_MAX_TOTAL_ARG_LEN 512

#endif /* _FIRSTOS_UAPI_MEMORY_H */

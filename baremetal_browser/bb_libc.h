#ifndef BB_LIBC_H
#define BB_LIBC_H

#include "bb_types.h"

/* Freestanding stdarg support */
typedef __builtin_va_list bb_va_list;
#define bb_va_start(ap, last) __builtin_va_start(ap, last)
#define bb_va_end(ap)         __builtin_va_end(ap)
#define bb_va_arg(ap, type)   __builtin_va_arg(ap, type)

/* Memory Operations */
void *bb_memcpy(void *dest, const void *src, size_t n);
void *bb_memset(void *s, int c, size_t n);
void *bb_memmove(void *dest, const void *src, size_t n);
int   bb_memcmp(const void *s1, const void *s2, size_t n);

/* String Operations */
size_t bb_strlen(const char *s);
char  *bb_strcpy(char *dest, const char *src);
char  *bb_strncpy(char *dest, const char *src, size_t n);
char  *bb_strcat(char *dest, const char *src);
char  *bb_strncat(char *dest, const char *src, size_t n);
int    bb_strcmp(const char *s1, const char *s2);
int    bb_strncmp(const char *s1, const char *s2, size_t n);
int    bb_strcasecmp(const char *s1, const char *s2);
int    bb_strncasecmp(const char *s1, const char *s2, size_t n);
char  *bb_strchr(const char *s, int c);
char  *bb_strrchr(const char *s, int c);
char  *bb_strstr(const char *haystack, const char *needle);
char  *bb_strcasestr(const char *haystack, const char *needle);
char  *bb_strdup(const char *s);
char  *bb_strndup(const char *s, size_t n);

/* Numeric & Formatted Output */
int    bb_atoi(const char *str);
char  *bb_itoa(int val, char *buf, int base);
int    bb_snprintf(char *str, size_t size, const char *format, ...);
int    bb_vsnprintf(char *str, size_t size, const char *format, bb_va_list ap);

/* Dynamic Growable String Buffer */
typedef struct {
    char   *data;
    size_t  length;
    size_t  capacity;
} bb_str_t;

void bb_str_init(bb_str_t *s);
void bb_str_init_with_cap(bb_str_t *s, size_t initial_cap);
void bb_str_append(bb_str_t *s, const char *str);
void bb_str_append_char(bb_str_t *s, char c);
void bb_str_append_mem(bb_str_t *s, const void *data, size_t len);
void bb_str_clear(bb_str_t *s);
void bb_str_free(bb_str_t *s);

/* Dynamic Generic Vector */
typedef struct {
    void  **items;
    size_t  count;
    size_t  capacity;
} bb_vec_t;

void bb_vec_init(bb_vec_t *v);
void bb_vec_push(bb_vec_t *v, void *item);
void *bb_vec_get(const bb_vec_t *v, size_t index);
void *bb_vec_pop(bb_vec_t *v);
void bb_vec_clear(bb_vec_t *v);
void bb_vec_free(bb_vec_t *v);

/* Standalone Memory Allocator */
#define BB_DEFAULT_HEAP_SIZE (16 * 1024 * 1024) /* 16 MB heap */
void  bb_heap_init(void *pool, size_t pool_size);
void *bb_malloc(size_t size);
void *bb_calloc(size_t nmemb, size_t size);
void *bb_realloc(void *ptr, size_t size);
void  bb_free(void *ptr);
size_t bb_heap_used_bytes(void);

#endif /* BB_LIBC_H */

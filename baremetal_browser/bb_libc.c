#include "bb_libc.h"

/* Memory Operations */
void *bb_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *bb_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    uint8_t val = (uint8_t)c;
    while (n--) {
        *p++ = val;
    }
    return s;
}

void *bb_memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *(--d) = *(--s);
        }
    }
    return dest;
}

int bb_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

/* String Operations */
size_t bb_strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *bb_strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ret = dest;
    while ((*dest++ = *src++)) {}
    return ret;
}

char *bb_strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *bb_strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++)) {}
    return ret;
}

char *bb_strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

int bb_strcmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

int bb_strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

static inline char bb_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int bb_strcasecmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (bb_tolower(*s1) == bb_tolower(*s2))) {
        s1++;
        s2++;
    }
    return (int)(uint8_t)bb_tolower(*s1) - (int)(uint8_t)bb_tolower(*s2);
}

int bb_strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (n > 1 && *s1 && (bb_tolower(*s1) == bb_tolower(*s2))) {
        s1++;
        s2++;
        n--;
    }
    return (int)(uint8_t)bb_tolower(*s1) - (int)(uint8_t)bb_tolower(*s2);
}

char *bb_strchr(const char *s, int c) {
    if (!s) return NULL;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return NULL;
}

char *bb_strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *bb_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t needle_len = bb_strlen(needle);
    while (*haystack) {
        if (bb_strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *bb_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t needle_len = bb_strlen(needle);
    while (*haystack) {
        if (bb_strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *bb_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = bb_strlen(s);
    char *copy = (char *)bb_malloc(len + 1);
    if (!copy) return NULL;
    bb_memcpy(copy, s, len + 1);
    return copy;
}

char *bb_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *copy = (char *)bb_malloc(len + 1);
    if (!copy) return NULL;
    bb_memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

/* Numeric & Formatted Output */
int bb_atoi(const char *str) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

char *bb_itoa(int val, char *buf, int base) {
    if (!buf || (base != 10 && base != 16)) return buf;
    char temp[32];
    int i = 0;
    int is_neg = 0;
    unsigned int uval;

    if (base == 10 && val < 0) {
        is_neg = 1;
        uval = (unsigned int)(-val);
    } else {
        uval = (unsigned int)val;
    }

    if (uval == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    while (uval > 0) {
        int rem = uval % base;
        temp[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
        uval /= base;
    }

    if (is_neg) {
        temp[i++] = '-';
    }

    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
    return buf;
}

int bb_vsnprintf(char *str, size_t size, const char *format, bb_va_list ap) {
    if (!format) return 0;
    if (!str || size == 0) return 0;

    size_t out_pos = 0;
    const char *f = format;

    while (*f && out_pos + 1 < size) {
        if (*f != '%') {
            str[out_pos++] = *f++;
            continue;
        }

        f++; /* skip '%' */
        if (*f == '%') {
            str[out_pos++] = '%';
            f++;
            continue;
        }

        /* Check width/padding flags (e.g. %02x, %4d) */
        char pad_char = ' ';
        int width = 0;
        if (*f == '0') {
            pad_char = '0';
            f++;
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        char spec = *f++;
        if (spec == 'c') {
            char c = (char)bb_va_arg(ap, int);
            str[out_pos++] = c;
        } else if (spec == 's') {
            const char *s = bb_va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t slen = bb_strlen(s);
            while (width > (int)slen && out_pos + 1 < size) {
                str[out_pos++] = pad_char;
                width--;
            }
            while (*s && out_pos + 1 < size) {
                str[out_pos++] = *s++;
            }
        } else if (spec == 'd' || spec == 'i') {
            int d = bb_va_arg(ap, int);
            char num_buf[32];
            bb_itoa(d, num_buf, 10);
            size_t nlen = bb_strlen(num_buf);
            while (width > (int)nlen && out_pos + 1 < size) {
                str[out_pos++] = pad_char;
                width--;
            }
            const char *p = num_buf;
            while (*p && out_pos + 1 < size) {
                str[out_pos++] = *p++;
            }
        } else if (spec == 'u') {
            unsigned int u = bb_va_arg(ap, unsigned int);
            char num_buf[32];
            char temp[32];
            int ti = 0;
            if (u == 0) {
                temp[ti++] = '0';
            } else {
                while (u > 0) {
                    temp[ti++] = '0' + (u % 10);
                    u /= 10;
                }
            }
            int ni = 0;
            while (ti > 0) num_buf[ni++] = temp[--ti];
            num_buf[ni] = '\0';
            while (width > ni && out_pos + 1 < size) {
                str[out_pos++] = pad_char;
                width--;
            }
            const char *p = num_buf;
            while (*p && out_pos + 1 < size) {
                str[out_pos++] = *p++;
            }
        } else if (spec == 'x' || spec == 'X' || spec == 'p') {
            unsigned int x;
            if (spec == 'p') {
                x = (unsigned int)(uintptr_t)bb_va_arg(ap, void *);
                if (out_pos + 2 < size) {
                    str[out_pos++] = '0';
                    str[out_pos++] = 'x';
                }
            } else {
                x = bb_va_arg(ap, unsigned int);
            }
            char num_buf[32];
            char temp[32];
            int ti = 0;
            if (x == 0) {
                temp[ti++] = '0';
            } else {
                while (x > 0) {
                    int rem = x % 16;
                    if (spec == 'X') {
                        temp[ti++] = (rem < 10) ? ('0' + rem) : ('A' + rem - 10);
                    } else {
                        temp[ti++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
                    }
                    x /= 16;
                }
            }
            int ni = 0;
            while (ti > 0) num_buf[ni++] = temp[--ti];
            num_buf[ni] = '\0';
            while (width > ni && out_pos + 1 < size) {
                str[out_pos++] = pad_char;
                width--;
            }
            const char *p = num_buf;
            while (*p && out_pos + 1 < size) {
                str[out_pos++] = *p++;
            }
        }
    }

    str[out_pos] = '\0';
    return (int)out_pos;
}

int bb_snprintf(char *str, size_t size, const char *format, ...) {
    bb_va_list ap;
    bb_va_start(ap, format);
    int res = bb_vsnprintf(str, size, format, ap);
    bb_va_end(ap);
    return res;
}

/* Dynamic Growable String Buffer */
void bb_str_init(bb_str_t *s) {
    bb_str_init_with_cap(s, 64);
}

void bb_str_init_with_cap(bb_str_t *s, size_t initial_cap) {
    if (!s) return;
    if (initial_cap < 16) initial_cap = 16;
    s->data = (char *)bb_malloc(initial_cap);
    if (s->data) {
        s->data[0] = '\0';
        s->length = 0;
        s->capacity = initial_cap;
    } else {
        s->data = NULL;
        s->length = 0;
        s->capacity = 0;
    }
}

void bb_str_append(bb_str_t *s, const char *str) {
    if (!s || !str) return;
    size_t slen = bb_strlen(str);
    bb_str_append_mem(s, str, slen);
}

void bb_str_append_char(bb_str_t *s, char c) {
    if (!s) return;
    char buf[2] = {c, '\0'};
    bb_str_append_mem(s, buf, 1);
}

void bb_str_append_mem(bb_str_t *s, const void *data, size_t len) {
    if (!s || !data || len == 0) return;
    if (s->length + len + 1 > s->capacity) {
        size_t new_cap = (s->capacity == 0) ? 64 : s->capacity * 2;
        while (new_cap < s->length + len + 1) {
            new_cap *= 2;
        }
        char *new_data = (char *)bb_realloc(s->data, new_cap);
        if (!new_data) return;
        s->data = new_data;
        s->capacity = new_cap;
    }
    bb_memcpy(s->data + s->length, data, len);
    s->length += len;
    s->data[s->length] = '\0';
}

void bb_str_clear(bb_str_t *s) {
    if (!s) return;
    s->length = 0;
    if (s->data) s->data[0] = '\0';
}

void bb_str_free(bb_str_t *s) {
    if (!s) return;
    if (s->data) {
        bb_free(s->data);
        s->data = NULL;
    }
    s->length = 0;
    s->capacity = 0;
}

/* Dynamic Generic Vector */
void bb_vec_init(bb_vec_t *v) {
    if (!v) return;
    v->capacity = 16;
    v->count = 0;
    v->items = (void **)bb_malloc(v->capacity * sizeof(void *));
    if (!v->items) {
        v->capacity = 0;
    }
}

void bb_vec_push(bb_vec_t *v, void *item) {
    if (!v) return;
    if (v->count >= v->capacity) {
        size_t new_cap = (v->capacity == 0) ? 16 : v->capacity * 2;
        void **new_items = (void **)bb_realloc(v->items, new_cap * sizeof(void *));
        if (!new_items) return;
        v->items = new_items;
        v->capacity = new_cap;
    }
    v->items[v->count++] = item;
}

void *bb_vec_get(const bb_vec_t *v, size_t index) {
    if (!v || index >= v->count) return NULL;
    return v->items[index];
}

void *bb_vec_pop(bb_vec_t *v) {
    if (!v || v->count == 0) return NULL;
    return v->items[--v->count];
}

void bb_vec_clear(bb_vec_t *v) {
    if (!v) return;
    v->count = 0;
}

void bb_vec_free(bb_vec_t *v) {
    if (!v) return;
    if (v->items) {
        bb_free(v->items);
        v->items = NULL;
    }
    v->count = 0;
    v->capacity = 0;
}

/* ========================================================================= */
/* Standalone Boundary-Tag Heap Allocator                                    */
/* ========================================================================= */

#define BB_CHUNK_USED_BIT 0x01
#define BB_CHUNK_ALIGN    8
#define BB_ALIGN(x)       (((x) + (BB_CHUNK_ALIGN - 1)) & ~(BB_CHUNK_ALIGN - 1))

typedef struct bb_chunk {
    size_t size_and_flags; /* Total chunk size including header. Bit 0: 1=used, 0=free */
    struct bb_chunk *next;
    struct bb_chunk *prev;
} bb_chunk_t;

static uint8_t  g_default_heap[BB_DEFAULT_HEAP_SIZE] __attribute__((aligned(8)));
static uint8_t *g_heap_start = NULL;
static size_t   g_heap_size = 0;
static bb_chunk_t *g_free_list = NULL;
static size_t   g_used_bytes = 0;

static inline size_t chunk_size(bb_chunk_t *c) {
    return c->size_and_flags & ~BB_CHUNK_USED_BIT;
}

static inline int chunk_is_used(bb_chunk_t *c) {
    return (int)(c->size_and_flags & BB_CHUNK_USED_BIT);
}

static inline void chunk_set_used(bb_chunk_t *c, int used) {
    if (used) c->size_and_flags |= BB_CHUNK_USED_BIT;
    else      c->size_and_flags &= ~BB_CHUNK_USED_BIT;
}

void bb_heap_init(void *pool, size_t pool_size) {
    if (!pool || pool_size < sizeof(bb_chunk_t) + 64) {
        pool = g_default_heap;
        pool_size = sizeof(g_default_heap);
    }
    g_heap_start = (uint8_t *)pool;
    g_heap_size = pool_size;
    g_used_bytes = 0;

    bb_chunk_t *first = (bb_chunk_t *)g_heap_start;
    first->size_and_flags = g_heap_size & ~BB_CHUNK_USED_BIT;
    first->next = NULL;
    first->prev = NULL;
    g_free_list = first;
}

static void ensure_heap_initialized(void) {
    if (!g_heap_start) {
        bb_heap_init(g_default_heap, sizeof(g_default_heap));
    }
}

size_t bb_heap_used_bytes(void) {
    return g_used_bytes;
}

void *bb_malloc(size_t size) {
    if (size == 0) return NULL;
    ensure_heap_initialized();

    size_t needed = BB_ALIGN(size + sizeof(bb_chunk_t));
    if (needed < sizeof(bb_chunk_t) + 16) {
        needed = sizeof(bb_chunk_t) + 16;
    }

    bb_chunk_t *curr = g_free_list;
    while (curr) {
        size_t c_size = chunk_size(curr);
        if (c_size >= needed) {
            /* Can we split the chunk? */
            size_t rem = c_size - needed;
            if (rem >= sizeof(bb_chunk_t) + 16) {
                bb_chunk_t *remainder = (bb_chunk_t *)((uint8_t *)curr + needed);
                remainder->size_and_flags = rem & ~BB_CHUNK_USED_BIT;
                remainder->next = curr->next;
                remainder->prev = curr->prev;
                if (remainder->next) remainder->next->prev = remainder;
                if (remainder->prev) remainder->prev->next = remainder;
                if (curr == g_free_list) g_free_list = remainder;

                curr->size_and_flags = needed | BB_CHUNK_USED_BIT;
            } else {
                /* Consume entire chunk */
                if (curr->prev) curr->prev->next = curr->next;
                if (curr->next) curr->next->prev = curr->prev;
                if (curr == g_free_list) g_free_list = curr->next;

                curr->size_and_flags |= BB_CHUNK_USED_BIT;
            }

            g_used_bytes += chunk_size(curr);
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    return NULL; /* Out of memory */
}

void *bb_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = bb_malloc(total);
    if (ptr) {
        bb_memset(ptr, 0, total);
    }
    return ptr;
}

void bb_free(void *ptr) {
    if (!ptr) return;
    ensure_heap_initialized();

    bb_chunk_t *chunk = ((bb_chunk_t *)ptr) - 1;
    if (!chunk_is_used(chunk)) return; /* Double free protection */

    chunk_set_used(chunk, 0);
    g_used_bytes -= chunk_size(chunk);

    /* Insert chunk back at head of free list */
    chunk->prev = NULL;
    chunk->next = g_free_list;
    if (g_free_list) g_free_list->prev = chunk;
    g_free_list = chunk;

    /* Merge adjacent free blocks */
    bb_chunk_t *c = g_free_list;
    while (c && c->next) {
        uint8_t *curr_end = (uint8_t *)c + chunk_size(c);
        if (curr_end == (uint8_t *)c->next) {
            c->size_and_flags = (chunk_size(c) + chunk_size(c->next)) & ~BB_CHUNK_USED_BIT;
            c->next = c->next->next;
            if (c->next) c->next->prev = c;
        } else {
            c = c->next;
        }
    }
}

void *bb_realloc(void *ptr, size_t size) {
    if (!ptr) return bb_malloc(size);
    if (size == 0) {
        bb_free(ptr);
        return NULL;
    }

    bb_chunk_t *chunk = ((bb_chunk_t *)ptr) - 1;
    size_t cur_size = chunk_size(chunk) - sizeof(bb_chunk_t);
    if (cur_size >= size) {
        return ptr; /* Fits in current block */
    }

    void *new_ptr = bb_malloc(size);
    if (!new_ptr) return NULL;

    size_t copy_size = (cur_size < size) ? cur_size : size;
    bb_memcpy(new_ptr, ptr, copy_size);
    bb_free(ptr);
    return new_ptr;
}

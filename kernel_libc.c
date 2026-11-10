/*
 * kernel_libc.c - Lightweight 64-bit C library implementations for FirstOS kernel
 */

#include "io.h"
#include "heap.h"
#include <stddef.h>
#include <stdarg.h>

int errno = 0;

/* Character classification array for Newlib / ctype */
const char _ctype_[257] = {
    0,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x08,0x08,0x08,0x08,0x08,0x20,0x20,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x10,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
    0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x40,0x40,0x40,0x40,0x40,0x40,
    0x40,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x40,0x40,0x40,0x40,0x40,
    0x40,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x40,0x40,0x40,0x40,0x20
};

/* Memory allocation primitives */
void *malloc(size_t size) {
    return kmalloc((unsigned int)size);
}

void free(void *ptr) {
    kfree(ptr);
}

void *calloc(size_t nmemb, size_t size) {
    return kcalloc((unsigned int)nmemb, (unsigned int)size);
}

void *realloc(void *ptr, size_t size) {
    return krealloc(ptr, (unsigned int)size);
}

/* Memory operations */
void *memcpy(void *dest, const void *src, size_t n) {
    if (dest == src || n == 0) return dest;
#if defined(__x86_64__)
    void *d = dest;
    const void *s = src;
    size_t qwords = n >> 3;
    size_t tail = n & 7u;
    __asm__ volatile (
        "cld\n\t"
        "rep movsq\n\t"
        "movq %[tail], %%rcx\n\t"
        "rep movsb"
        : "+D"(d), "+S"(s), "+c"(qwords)
        : [tail] "r"(tail)
        : "memory"
    );
    return dest;
#else
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dest;
#endif
}

void *memset(void *s, int c, size_t n) {
    if (n == 0) return s;
#if defined(__x86_64__)
    void *d = s;
    unsigned long val8 = (unsigned char)c;
    unsigned long pattern = val8 * 0x0101010101010101ULL;
    size_t qwords = n >> 3;
    size_t tail = n & 7u;
    __asm__ volatile (
        "cld\n\t"
        "rep stosq\n\t"
        "movq %[tail], %%rcx\n\t"
        "rep stosb"
        : "+D"(d), "+c"(qwords)
        : "a"(pattern), [tail] "r"(tail)
        : "memory"
    );
    return s;
#else
    u8 *p = (u8 *)s;
    u8 val = (u8)c;
    while (n--) *p++ = val;
    return s;
#endif
}

void *memmove(void *dest, const void *src, size_t n) {
    if (dest == src || n == 0) return dest;
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    if (d < s || d >= s + n) {
        return memcpy(dest, src, n);
    }
    d += n;
    s += n;
    while (n >= 8) {
        d -= 8;
        s -= 8;
        *(unsigned long *)d = *(const unsigned long *)s;
        n -= 8;
    }
    while (n--) {
        *(--d) = *(--s);
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const u8 *p1 = (const u8 *)s1;
    const u8 *p2 = (const u8 *)s2;
    while (n--) {
        if (*p1 != *p2) return (int)*p1 - (int)*p2;
        p1++;
        p2++;
    }
    return 0;
}

/* String operations */
size_t strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++)) ;
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!n) return 0;
    while (n-- && *s1 && *s2) {
        if (*s1 != *s2) return (int)*(const unsigned char *)s1 - (int)*(const unsigned char *)s2;
        s1++; s2++;
    }
    if (n == (size_t)-1) return 0;
    return (int)*(const unsigned char *)s1 - (int)*(const unsigned char *)s2;
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++)) ;
    return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) *dest++ = *src++;
    *dest = '\0';
    return ret;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (!n) return 0;
    while (n-- && *s1 && *s2) {
        char c1 = to_lower(*s1);
        char c2 = to_lower(*s2);
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++; s2++;
    }
    if (n == (size_t)-1) return 0;
    return (int)(unsigned char)to_lower(*s1) - (int)(unsigned char)to_lower(*s2);
}

int atoi(const char *str) {
    if (!str) return 0;
    while (*str == ' ' || (*str >= '\t' && *str <= '\r')) str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else base = 8;
        } else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (*s) {
        int c = *s;
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'z') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') val = c - 'A' + 10;
        else break;
        if (val >= base) break;
        acc = acc * base + val;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return acc;
}

/* Formatting helpers */
static void int_to_str(long long val, char *buf, int base, int is_signed) {
    char tmp[64];
    int i = 0;
    unsigned long long uval;
    if (is_signed && val < 0) {
        uval = (unsigned long long)(-val);
    } else {
        uval = (unsigned long long)val;
    }
    if (uval == 0) { tmp[i++] = '0'; }
    else {
        while (uval > 0) {
            int rem = uval % base;
            tmp[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
            uval /= base;
        }
    }
    int j = 0;
    if (is_signed && val < 0) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    size_t written = 0;
    const char *p = format;

    while (*p && written + 1 < size) {
        if (*p != '%') {
            str[written++] = *p++;
            continue;
        }
        p++; // skip '%'
        if (*p == '%') {
            str[written++] = '%';
            p++;
            continue;
        }

        int left_align = 0;
        int zero_pad = 0;
        while (*p == '-' || *p == '0' || *p == '+' || *p == ' ') {
            if (*p == '-') left_align = 1;
            else if (*p == '0') zero_pad = 1;
            p++;
        }
        if (left_align) zero_pad = 0;

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }

        int is_long = 0;
        while (*p == 'l' || *p == 'z' || *p == 'h') {
            if (*p == 'l' || *p == 'z') is_long++;
            p++;
        }

        if (*p == 'c') {
            char c = (char)va_arg(ap, int);
            if (!left_align) {
                for (int pad = 1; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            if (written + 1 < size) str[written++] = c;
            if (left_align) {
                for (int pad = 1; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            p++;
        } else if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = 0;
            while (s[slen] && (precision < 0 || slen < precision)) slen++;
            if (!left_align) {
                for (int pad = slen; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            for (int i = 0; i < slen && written + 1 < size; i++) str[written++] = s[i];
            if (left_align) {
                for (int pad = slen; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            p++;
        } else if (*p == 'd' || *p == 'i') {
            long long d = (is_long >= 2) ? va_arg(ap, long long) : (is_long == 1 ? va_arg(ap, long) : va_arg(ap, int));
            char num_buf[32];
            int_to_str(d, num_buf, 10, 1);
            int nlen = (int)strlen(num_buf);
            char pad_ch = zero_pad ? '0' : ' ';
            if (!left_align) {
                if (zero_pad && num_buf[0] == '-' && written + 1 < size) {
                    str[written++] = '-';
                    for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = '0';
                    for (int i = 1; i < nlen && written + 1 < size; i++) str[written++] = num_buf[i];
                } else {
                    for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = pad_ch;
                    for (int i = 0; i < nlen && written + 1 < size; i++) str[written++] = num_buf[i];
                }
            } else {
                for (int i = 0; i < nlen && written + 1 < size; i++) str[written++] = num_buf[i];
                for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            p++;
        } else if (*p == 'u') {
            unsigned long long u = (is_long >= 2) ? va_arg(ap, unsigned long long) : (is_long == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            char num_buf[32];
            int_to_str((long long)u, num_buf, 10, 0);
            int nlen = (int)strlen(num_buf);
            char pad_ch = zero_pad ? '0' : ' ';
            if (!left_align) {
                for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = pad_ch;
            }
            for (int i = 0; i < nlen && written + 1 < size; i++) str[written++] = num_buf[i];
            if (left_align) {
                for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            p++;
        } else if (*p == 'x' || *p == 'X') {
            unsigned long long x = (is_long >= 2) ? va_arg(ap, unsigned long long) : (is_long == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            char num_buf[32];
            int_to_str((long long)x, num_buf, 16, 0);
            if (*p == 'X') {
                for (int i = 0; num_buf[i]; i++) {
                    if (num_buf[i] >= 'a' && num_buf[i] <= 'f') num_buf[i] -= 32;
                }
            }
            int nlen = (int)strlen(num_buf);
            char pad_ch = zero_pad ? '0' : ' ';
            if (!left_align) {
                for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = pad_ch;
            }
            for (int i = 0; i < nlen && written + 1 < size; i++) str[written++] = num_buf[i];
            if (left_align) {
                for (int pad = nlen; pad < width && written + 1 < size; pad++) str[written++] = ' ';
            }
            p++;
        } else if (*p == 'p') {
            uintptr_t ptr = (uintptr_t)va_arg(ap, void *);
            char num_buf[32];
            int_to_str(ptr, num_buf, 16, 0);
            if (written + 2 < size) { str[written++] = '0'; str[written++] = 'x'; }
            const char *s = num_buf;
            while (*s && written + 1 < size) str[written++] = *s++;
            p++;
        } else {
            if (written + 1 < size) str[written++] = *p;
            p++;
        }
    }
    str[written] = '\0';
    return (int)written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 65536, format, ap);
    va_end(ap);
    return ret;
}

int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int matches = 0;
    const char *s = str;
    const char *f = format;

    while (*f && *s) {
        if (*f == ' ') {
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
            f++;
            continue;
        }
        if (*f != '%') {
            if (*f != *s) break;
            f++; s++;
            continue;
        }
        f++; // skip '%'

        /* Optional field width */
        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }
        if (width <= 0) width = 0x7FFFFFFF;

        if (*f == 'd') {
            while (*s == ' ' || *s == '\t') s++;
            int sign = 1;
            int count = 0;
            if (*s == '-') { sign = -1; s++; count++; }
            else if (*s == '+') { s++; count++; }
            int val = 0;
            if (*s < '0' || *s > '9' || count >= width) break;
            while (*s >= '0' && *s <= '9' && count < width) {
                val = val * 10 + (*s - '0');
                s++;
                count++;
            }
            int *out = va_arg(ap, int *);
            if (out) *out = sign * val;
            matches++;
            f++;
        } else if (*f == 'u') {
            while (*s == ' ' || *s == '\t') s++;
            unsigned int val = 0;
            int count = 0;
            if (*s < '0' || *s > '9') break;
            while (*s >= '0' && *s <= '9' && count < width) {
                val = val * 10 + (*s - '0');
                s++;
                count++;
            }
            unsigned int *out = va_arg(ap, unsigned int *);
            if (out) *out = val;
            matches++;
            f++;
        } else if (*f == 'x' || *f == 'X') {
            while (*s == ' ' || *s == '\t') s++;
            unsigned int val = 0;
            int count = 0;
            int found_digits = 0;
            while (count < width) {
                char c = *s;
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else break;
                val = val * 16 + digit;
                s++;
                count++;
                found_digits++;
            }
            if (!found_digits) break;
            unsigned int *out = va_arg(ap, unsigned int *);
            if (out) *out = val;
            matches++;
            f++;
        } else if (*f == 's') {
            while (*s == ' ' || *s == '\t') s++;
            char *out = va_arg(ap, char *);
            int count = 0;
            while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && count < width) {
                if (out) *out++ = *s;
                s++;
                count++;
            }
            if (!count) break;
            if (out) *out = '\0';
            matches++;
            f++;
        } else if (*f == '[') {
            f++;
            int invert = 0;
            if (*f == '^') {
                invert = 1;
                f++;
            }
            unsigned char set[256];
            memset(set, 0, sizeof(set));
            if (*f == ']') {
                set[(unsigned char)']'] = 1;
                f++;
            }
            while (*f && *f != ']') {
                if (f[1] == '-' && f[2] && f[2] != ']') {
                    unsigned char c1 = (unsigned char)f[0];
                    unsigned char c2 = (unsigned char)f[2];
                    for (unsigned int c = c1; c <= c2; c++) {
                        set[(unsigned char)c] = 1;
                    }
                    f += 3;
                } else {
                    set[(unsigned char)*f] = 1;
                    f++;
                }
            }
            if (*f == ']') f++;

            char *out = va_arg(ap, char *);
            int count = 0;
            while (*s && count < width) {
                unsigned char c = (unsigned char)*s;
                int in_set = set[c];
                if (invert) in_set = !in_set;
                if (!in_set) break;
                if (out) *out++ = *s;
                s++;
                count++;
            }
            if (!count) break;
            if (out) *out = '\0';
            matches++;
        } else {
            break;
        }
    }
    va_end(ap);
    return matches;
}

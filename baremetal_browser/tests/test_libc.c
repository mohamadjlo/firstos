#include "../bb_types.h"
#include "../bb_libc.h"
#include <stdio.h>
#include <assert.h>

void run_libc_tests(void) {
    printf("[TEST] Running Freestanding Libc Tests...\n");

    /* Memory tests */
    char buf[32];
    bb_memset(buf, 'A', 10);
    buf[10] = '\0';
    assert(bb_strlen(buf) == 10);
    assert(buf[0] == 'A' && buf[9] == 'A');

    char dest[32];
    bb_memcpy(dest, buf, 11);
    assert(bb_strcmp(dest, buf) == 0);

    /* Overlapping memmove */
    bb_strcpy(dest, "1234567890");
    bb_memmove(dest + 2, dest, 5); /* "1212345890" */
    assert(bb_strncmp(dest, "1212345", 7) == 0);

    /* String tests */
    assert(bb_strcmp("apple", "apple") == 0);
    assert(bb_strcmp("apple", "banana") < 0);
    assert(bb_strcasecmp("Hello", "hElLo") == 0);
    assert(bb_strstr("firstos operating system", "operating") != NULL);
    assert(bb_strstr("firstos", "linux") == NULL);

    /* Number formatting tests */
    char fmt[64];
    bb_snprintf(fmt, sizeof(fmt), "Val: %d, Hex: 0x%x, Str: %s", 1234, 0xabcd, "FirstOS");
    assert(bb_strcmp(fmt, "Val: 1234, Hex: 0xabcd, Str: FirstOS") == 0);

    /* Allocator tests */
    void *p1 = bb_malloc(256);
    assert(p1 != NULL);
    bb_memset(p1, 0x55, 256);
    void *p2 = bb_malloc(1024);
    assert(p2 != NULL);
    bb_free(p1);
    void *p3 = bb_malloc(128);
    assert(p3 != NULL);
    bb_free(p2);
    bb_free(p3);

    /* Dynamic string tests */
    bb_str_t str;
    bb_str_init(&str);
    bb_str_append(&str, "Hello");
    bb_str_append(&str, " ");
    bb_str_append(&str, "World!");
    assert(bb_strcmp(str.data, "Hello World!") == 0);
    assert(str.length == 12);
    bb_str_free(&str);

    /* Dynamic vector tests */
    bb_vec_t vec;
    bb_vec_init(&vec);
    int a = 1, b = 2, c = 3;
    bb_vec_push(&vec, &a);
    bb_vec_push(&vec, &b);
    bb_vec_push(&vec, &c);
    assert(vec.count == 3);
    assert(*(int*)bb_vec_get(&vec, 1) == 2);
    bb_vec_free(&vec);

    printf("       -> ALL Libc tests PASSED!\n");
}

int main(void) {
    run_libc_tests();
    return 0;
}

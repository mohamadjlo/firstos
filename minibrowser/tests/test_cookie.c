#include "cookie.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("[test_cookie] Running CookieJar tests...\n");

    CookieJar *jar = cookie_jar_create(NULL);
    assert(jar != NULL);

    /* 1. Set cookie with Domain, Path, Max-Age */
    cookie_jar_set(jar, "user_id=usr_4281; Domain=example.com; Path=/app; Max-Age=3600; HttpOnly", "example.com", "/app/dashboard");

    /* 2. Set cookie for root path */
    cookie_jar_set(jar, "theme=dark; Domain=example.com; Path=/", "example.com", "/");

    /* 3. Set another domain cookie */
    cookie_jar_set(jar, "session=sess_999; Domain=api.other.org; Path=/; Secure", "api.other.org", "/v1/auth");

    /* 4. Query cookies for example.com /app/overview */
    char *hdr1 = cookie_jar_get_header(jar, "example.com", "/app/overview");
    assert(hdr1 != NULL);
    assert(strstr(hdr1, "user_id=usr_4281") != NULL);
    assert(strstr(hdr1, "theme=dark") != NULL);
    assert(strstr(hdr1, "session=sess_999") == NULL); /* different domain */
    printf("[test_cookie] Generated Cookie Header for /app: %s\n", hdr1);
    free(hdr1);

    /* 5. Query cookies for example.com /other (should only match theme, not user_id path /app) */
    char *hdr2 = cookie_jar_get_header(jar, "example.com", "/other");
    assert(hdr2 != NULL);
    assert(strstr(hdr2, "theme=dark") != NULL);
    assert(strstr(hdr2, "user_id=usr_4281") == NULL);
    printf("[test_cookie] Generated Cookie Header for /other: %s\n", hdr2);
    free(hdr2);

    /* 6. Test Expiration (Max-Age=0 removes/expires cookie) */
    cookie_jar_set(jar, "theme=deleted; Domain=example.com; Path=/; Max-Age=0", "example.com", "/");

    char *hdr3 = cookie_jar_get_header(jar, "example.com", "/");
    assert(hdr3 == NULL || strstr(hdr3, "theme") == NULL);
    if (hdr3) free(hdr3);

    cookie_jar_free(jar);
    printf("[test_cookie] All CookieJar tests passed successfully!\n");
    return 0;
}

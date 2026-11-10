#ifndef COOKIE_H
#define COOKIE_H

#include <stdbool.h>
#include <time.h>

typedef struct Cookie {
    char *name;
    char *value;
    char *domain;
    char *path;
    time_t expiry_epoch;
    bool http_only;
    bool secure;
    struct Cookie *next;
} Cookie;

typedef struct CookieJar {
    Cookie *head;
    char *storage_path;
} CookieJar;

CookieJar *cookie_jar_create(const char *storage_path);
void cookie_jar_free(CookieJar *jar);
void cookie_jar_set(CookieJar *jar, const char *raw_set_cookie_header, const char *request_domain, const char *request_path);
char *cookie_jar_get_header(CookieJar *jar, const char *target_domain, const char *target_path);
void cookie_jar_clear(CookieJar *jar);

#endif /* COOKIE_H */

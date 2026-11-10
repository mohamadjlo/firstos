#define _POSIX_C_SOURCE 200809L
#include "cookie.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *trim_spaces(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

CookieJar *cookie_jar_create(const char *storage_path) {
    CookieJar *jar = (CookieJar *)calloc(1, sizeof(CookieJar));
    if (!jar) return NULL;
    if (storage_path) jar->storage_path = strdup(storage_path);
    return jar;
}

static void cookie_free(Cookie *c) {
    if (!c) return;
    free(c->name);
    free(c->value);
    free(c->domain);
    free(c->path);
    free(c);
}

void cookie_jar_clear(CookieJar *jar) {
    if (!jar) return;
    Cookie *curr = jar->head;
    while (curr) {
        Cookie *next = curr->next;
        cookie_free(curr);
        curr = next;
    }
    jar->head = NULL;
}

void cookie_jar_free(CookieJar *jar) {
    if (!jar) return;
    cookie_jar_clear(jar);
    free(jar->storage_path);
    free(jar);
}

static bool domain_matches(const char *cookie_domain, const char *target_domain) {
    if (!cookie_domain || !target_domain) return false;
    if (strcasecmp(cookie_domain, target_domain) == 0) return true;

    /* Leading dot in cookie domain */
    if (cookie_domain[0] == '.') {
        size_t c_len = strlen(cookie_domain);
        size_t t_len = strlen(target_domain);
        if (t_len >= c_len - 1) {
            const char *suffix = target_domain + (t_len - (c_len - 1));
            return (strcasecmp(suffix, cookie_domain + 1) == 0);
        }
    } else {
        /* Suffix match e.g. domain=example.com matching sub.example.com */
        size_t c_len = strlen(cookie_domain);
        size_t t_len = strlen(target_domain);
        if (t_len > c_len && target_domain[t_len - c_len - 1] == '.') {
            const char *suffix = target_domain + (t_len - c_len);
            return (strcasecmp(suffix, cookie_domain) == 0);
        }
    }
    return false;
}

static bool path_matches(const char *cookie_path, const char *target_path) {
    if (!cookie_path || !cookie_path[0]) return true;
    if (!target_path || !target_path[0]) target_path = "/";

    if (strcmp(cookie_path, "/") == 0) return true;
    return (strncmp(target_path, cookie_path, strlen(cookie_path)) == 0);
}

void cookie_jar_set(CookieJar *jar, const char *raw_set_cookie_header, const char *request_domain, const char *request_path) {
    if (!jar || !raw_set_cookie_header) return;

    const char *header_content = raw_set_cookie_header;
    if (strncasecmp(header_content, "Set-Cookie:", 11) == 0) {
        header_content += 11;
    }

    char buf[1024];
    strncpy(buf, header_content, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *token = strtok(buf, ";");
    if (!token) return;

    char *eq = strchr(token, '=');
    if (!eq) return;
    *eq = 0;

    char *cookie_name = trim_spaces(token);
    char *cookie_val = trim_spaces(eq + 1);
    if (!*cookie_name) return;

    char *cookie_domain = NULL;
    char *cookie_path = NULL;
    time_t expiry = 0;
    bool http_only = false;
    bool secure = false;

    token = strtok(NULL, ";");
    while (token) {
        char *attr = trim_spaces(token);
        char *attr_eq = strchr(attr, '=');
        if (attr_eq) {
            *attr_eq = 0;
            char *k = trim_spaces(attr);
            char *v = trim_spaces(attr_eq + 1);

            if (strcasecmp(k, "domain") == 0) {
                cookie_domain = strdup(v);
            } else if (strcasecmp(k, "path") == 0) {
                cookie_path = strdup(v);
            } else if (strcasecmp(k, "max-age") == 0) {
                long seconds = atol(v);
                expiry = (seconds <= 0) ? 1 : (time(NULL) + seconds);
            }
        } else {
            if (strcasecmp(attr, "httponly") == 0) http_only = true;
            else if (strcasecmp(attr, "secure") == 0) secure = true;
        }
        token = strtok(NULL, ";");
    }

    if (!cookie_domain && request_domain) {
        cookie_domain = strdup(request_domain);
    }
    if (!cookie_path) {
        if (request_path && request_path[0]) cookie_path = strdup(request_path);
        else cookie_path = strdup("/");
    }

    /* Check if expired or max-age=0 to delete */
    time_t now = time(NULL);
    bool should_delete = (expiry > 0 && expiry <= now);

    Cookie *prev = NULL;
    Cookie *curr = jar->head;
    while (curr) {
        if (strcmp(curr->name, cookie_name) == 0 &&
            strcasecmp(curr->domain, cookie_domain ? cookie_domain : "") == 0 &&
            strcmp(curr->path, cookie_path ? cookie_path : "/") == 0) {

            if (should_delete) {
                if (prev) prev->next = curr->next;
                else jar->head = curr->next;
                cookie_free(curr);
            } else {
                free(curr->value);
                curr->value = strdup(cookie_val);
                curr->expiry_epoch = expiry;
                curr->http_only = http_only;
                curr->secure = secure;
            }
            free(cookie_domain);
            free(cookie_path);
            return;
        }
        prev = curr;
        curr = curr->next;
    }

    if (should_delete) {
        free(cookie_domain);
        free(cookie_path);
        return;
    }

    /* Insert new cookie */
    Cookie *new_c = (Cookie *)calloc(1, sizeof(Cookie));
    new_c->name = strdup(cookie_name);
    new_c->value = strdup(cookie_val);
    new_c->domain = cookie_domain ? cookie_domain : strdup("");
    new_c->path = cookie_path ? cookie_path : strdup("/");
    new_c->expiry_epoch = expiry;
    new_c->http_only = http_only;
    new_c->secure = secure;

    new_c->next = jar->head;
    jar->head = new_c;
}

char *cookie_jar_get_header(CookieJar *jar, const char *target_domain, const char *target_path) {
    if (!jar || !target_domain) return NULL;
    if (!target_path || !target_path[0]) target_path = "/";

    time_t now = time(NULL);

    char buf[4096] = "Cookie: ";
    size_t len = strlen(buf);
    int count = 0;

    Cookie *curr = jar->head;
    while (curr) {
        if (curr->expiry_epoch > 0 && curr->expiry_epoch < now) {
            /* Expired, skip */
            curr = curr->next;
            continue;
        }

        if (domain_matches(curr->domain, target_domain) && path_matches(curr->path, target_path)) {
            if (count > 0) {
                if (len + 2 < sizeof(buf)) {
                    strcat(buf, "; ");
                    len += 2;
                }
            }
            size_t add_len = strlen(curr->name) + 1 + strlen(curr->value);
            if (len + add_len < sizeof(buf)) {
                strcat(buf, curr->name);
                strcat(buf, "=");
                strcat(buf, curr->value);
                len += add_len;
                count++;
            }
        }
        curr = curr->next;
    }

    if (count == 0) return NULL;
    return strdup(buf);
}

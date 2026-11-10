#define _POSIX_C_SOURCE 200809L
#include "dom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

DOMNode *dom_node_create(NodeType type) {
    DOMNode *node = (DOMNode *)calloc(1, sizeof(DOMNode));
    if (!node) return NULL;
    node->type = type;
    return node;
}

static void attribute_list_free(Attribute *attr) {
    while (attr) {
        Attribute *next = attr->next;
        free(attr->key);
        free(attr->value);
        free(attr);
        attr = next;
    }
}

void dom_node_free(DOMNode *node) {
    if (!node) return;

    /* Recursively free children */
    DOMNode *child = node->first_child;
    while (child) {
        DOMNode *next = child->next_sibling;
        dom_node_free(child);
        child = next;
    }

    free(node->tag_name);
    free(node->text_content);
    free(node->inner_text);
    attribute_list_free(node->attributes);
    free(node);
}

void dom_node_append_child(DOMNode *parent, DOMNode *child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
        parent->last_child = child;
    } else {
        parent->first_child = child;
        parent->last_child = child;
    }
}

void dom_node_set_text(DOMNode *node, const char *text) {
    if (!node) return;
    free(node->text_content);
    node->text_content = text ? strdup(text) : NULL;
}

void dom_node_set_attribute(DOMNode *node, const char *key, const char *value) {
    if (!node || !key) return;

    /* Check if attribute already exists */
    for (Attribute *curr = node->attributes; curr; curr = curr->next) {
        if (strcasecmp(curr->key, key) == 0) {
            free(curr->value);
            curr->value = value ? strdup(value) : strdup("");
            return;
        }
    }

    Attribute *attr = (Attribute *)calloc(1, sizeof(Attribute));
    if (!attr) return;
    attr->key = strdup(key);
    attr->value = value ? strdup(value) : strdup("");
    attr->next = node->attributes;
    node->attributes = attr;
}

const char *dom_node_get_attribute(const DOMNode *node, const char *key) {
    if (!node || !key) return NULL;
    for (const Attribute *curr = node->attributes; curr; curr = curr->next) {
        if (strcasecmp(curr->key, key) == 0) {
            return curr->value;
        }
    }
    return NULL;
}

const char *dom_node_get_id(const DOMNode *node) {
    return dom_node_get_attribute(node, "id");
}

bool dom_node_has_class(const DOMNode *node, const char *class_name) {
    if (!node || !class_name) return false;
    const char *classes = dom_node_get_attribute(node, "class");
    if (!classes) return false;

    size_t target_len = strlen(class_name);
    const char *p = classes;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len == target_len && strncmp(start, class_name, len) == 0) {
            return true;
        }
    }
    return false;
}

DOMNode *dom_node_find_by_tag(const DOMNode *node, const char *tag_name) {
    if (!node || !tag_name) return NULL;
    if (node->type == NODE_ELEMENT && node->tag_name && strcasecmp(node->tag_name, tag_name) == 0) {
        return (DOMNode *)node;
    }
    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        DOMNode *found = dom_node_find_by_tag(child, tag_name);
        if (found) return found;
    }
    return NULL;
}

static void collect_inner_text(const DOMNode *node, char **out_buf, size_t *out_len, size_t *out_cap) {
    if (!node) return;
    if (node->type == NODE_TEXT && node->text_content) {
        size_t l = strlen(node->text_content);
        if (*out_len + l + 1 > *out_cap) {
            *out_cap = (*out_len + l + 1) * 2;
            *out_buf = (char *)realloc(*out_buf, *out_cap);
        }
        memcpy(*out_buf + *out_len, node->text_content, l);
        *out_len += l;
        (*out_buf)[*out_len] = '\0';
    }
    for (const DOMNode *child = node->first_child; child; child = child->next_sibling) {
        collect_inner_text(child, out_buf, out_len, out_cap);
    }
}

char *dom_node_get_inner_text(const DOMNode *node) {
    if (!node) return NULL;
    if (node->type == NODE_TEXT && node->text_content) {
        return strdup(node->text_content);
    }
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';
    collect_inner_text(node, &buf, &len, &cap);
    return buf;
}

void dom_node_print_tree(const DOMNode *node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) printf("  ");

    if (node->type == NODE_DOCUMENT) {
        printf("[#document]\n");
    } else if (node->type == NODE_ELEMENT) {
        printf("<%s", node->tag_name ? node->tag_name : "?");
        for (Attribute *a = node->attributes; a; a = a->next) {
            printf(" %s=\"%s\"", a->key, a->value);
        }
        printf(">\n");
    } else if (node->type == NODE_TEXT) {
        printf("\"%s\"\n", node->text_content ? node->text_content : "");
    }

    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        dom_node_print_tree(child, depth + 1);
    }
}

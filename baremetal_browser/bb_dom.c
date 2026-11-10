#include "bb_dom.h"
#include "bb_image.h"

bb_node_t *bb_node_create(bb_node_type_t type) {
    bb_node_t *node = (bb_node_t *)bb_malloc(sizeof(bb_node_t));
    if (!node) return NULL;
    bb_memset(node, 0, sizeof(bb_node_t));
    node->type = type;
    return node;
}

bb_node_t *bb_node_create_element(const char *tag_name) {
    bb_node_t *node = bb_node_create(BB_NODE_ELEMENT);
    if (!node) return NULL;
    if (tag_name) {
        size_t len = bb_strlen(tag_name);
        node->tag_name = (char *)bb_malloc(len + 1);
        if (node->tag_name) {
            size_t i;
            for (i = 0; i < len; i++) {
                char c = tag_name[i];
                if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
                node->tag_name[i] = c;
            }
            node->tag_name[len] = '\0';
        }
    }
    return node;
}

bb_node_t *bb_node_create_text(const char *text) {
    bb_node_t *node = bb_node_create(BB_NODE_TEXT);
    if (!node) return NULL;
    node->text_content = bb_strdup(text ? text : "");
    return node;
}

void bb_node_append_child(bb_node_t *parent, bb_node_t *child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

void bb_node_set_attribute(bb_node_t *node, const char *key, const char *val) {
    if (!node || !key) return;

    /* Check if attribute already exists */
    bb_attr_t *curr = node->attributes;
    while (curr) {
        if (bb_strcasecmp(curr->key, key) == 0) {
            if (curr->val) bb_free(curr->val);
            curr->val = bb_strdup(val ? val : "");
            return;
        }
        curr = curr->next;
    }

    /* Append new attribute */
    bb_attr_t *attr = (bb_attr_t *)bb_malloc(sizeof(bb_attr_t));
    if (!attr) return;
    attr->key = bb_strdup(key);
    attr->val = bb_strdup(val ? val : "");
    attr->next = node->attributes;
    node->attributes = attr;
}

const char *bb_node_get_attribute(const bb_node_t *node, const char *key) {
    if (!node || !key) return NULL;
    bb_attr_t *curr = node->attributes;
    while (curr) {
        if (bb_strcasecmp(curr->key, key) == 0) {
            return curr->val;
        }
        curr = curr->next;
    }
    return NULL;
}

bool bb_node_has_class(const bb_node_t *node, const char *class_name) {
    if (!node || !class_name) return false;
    const char *cls = bb_node_get_attribute(node, "class");
    if (!cls) return false;

    size_t target_len = bb_strlen(class_name);
    const char *p = cls;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *token_start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t token_len = (size_t)(p - token_start);
        if (token_len == target_len && bb_strncmp(token_start, class_name, token_len) == 0) {
            return true;
        }
    }
    return false;
}

void bb_node_free_tree(bb_node_t *root) {
    if (!root) return;

    bb_node_t *child = root->first_child;
    while (child) {
        bb_node_t *next = child->next_sibling;
        bb_node_free_tree(child);
        child = next;
    }

    if (root->tag_name) bb_free(root->tag_name);
    if (root->text_content) bb_free(root->text_content);
    if (root->image) bb_image_free(root->image);

    bb_attr_t *attr = root->attributes;
    while (attr) {
        bb_attr_t *next_attr = attr->next;
        if (attr->key) bb_free(attr->key);
        if (attr->val) bb_free(attr->val);
        bb_free(attr);
        attr = next_attr;
    }

    bb_free(root);
}

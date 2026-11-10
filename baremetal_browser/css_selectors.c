#include "css_selectors.h"
#include "bb_libc.h"

css_selector_step_t *css_selector_step_create(void) {
    css_selector_step_t *s = (css_selector_step_t *)bb_malloc(sizeof(css_selector_step_t));
    if (s) {
        bb_memset(s, 0, sizeof(css_selector_step_t));
    }
    return s;
}

void css_selector_step_free(css_selector_step_t *step) {
    if (!step) return;
    if (step->tag) bb_free(step->tag);
    if (step->id) bb_free(step->id);
    if (step->class_name) bb_free(step->class_name);
    if (step->ancestor) css_selector_step_free(step->ancestor);
    bb_free(step);
}

css_selector_chain_t *css_selector_chain_create(void) {
    css_selector_chain_t *c = (css_selector_chain_t *)bb_malloc(sizeof(css_selector_chain_t));
    if (c) {
        c->key_selector = NULL;
        c->specificity = 0;
    }
    return c;
}

void css_selector_chain_free(css_selector_chain_t *chain) {
    if (!chain) return;
    if (chain->key_selector) {
        css_selector_step_free(chain->key_selector);
    }
    bb_free(chain);
}

uint32_t css_selector_calc_specificity(const css_selector_chain_t *chain) {
    if (!chain || !chain->key_selector) return 0;
    uint32_t spec = 0;
    const css_selector_step_t *curr = chain->key_selector;
    while (curr) {
        if (curr->id) {
            spec += 10000;
        }
        if (curr->class_name) {
            spec += 100;
        }
        if (curr->tag && bb_strcmp(curr->tag, "*") != 0) {
            spec += 1;
        }
        curr = curr->ancestor;
    }
    return spec;
}

static bool step_matches_node(const css_selector_step_t *step, const bb_node_t *node) {
    if (!step || !node) return false;
    if (node->type != BB_NODE_ELEMENT) return false;

    if (step->tag && bb_strcmp(step->tag, "*") != 0) {
        if (!node->tag_name || bb_strcasecmp(node->tag_name, step->tag) != 0) {
            return false;
        }
    }

    if (step->id) {
        const char *node_id = bb_node_get_attribute(node, "id");
        if (!node_id || bb_strcmp(node_id, step->id) != 0) {
            return false;
        }
    }

    if (step->class_name) {
        if (!bb_node_has_class(node, step->class_name)) {
            return false;
        }
    }

    return true;
}

static bool match_step_recursive(const css_selector_step_t *step, const bb_node_t *node) {
    if (!step) return true;
    if (!node) return false;

    if (!step_matches_node(step, node)) {
        return false;
    }

    if (!step->ancestor) {
        return true;
    }

    /* Descendant combinator: find any matching ancestor */
    const bb_node_t *anc = node->parent;
    while (anc) {
        if (anc->type == BB_NODE_ELEMENT && match_step_recursive(step->ancestor, anc)) {
            return true;
        }
        anc = anc->parent;
    }

    return false;
}

bool css_selector_match(const css_selector_chain_t *chain, const bb_node_t *node) {
    if (!chain || !chain->key_selector || !node) return false;
    return match_step_recursive(chain->key_selector, node);
}

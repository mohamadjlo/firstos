#ifndef CSS_SELECTORS_H
#define CSS_SELECTORS_H

#include "bb_types.h"
#include "bb_dom.h"

/* Single step in a selector chain (e.g. "div.intro") */
typedef struct css_selector_step {
    char                     *tag;        /* Tag name or NULL */
    char                     *id;         /* ID attribute or NULL */
    char                     *class_name; /* Class name or NULL */
    struct css_selector_step *ancestor;   /* Previous step linked by descendant combinator */
} css_selector_step_t;

/* A complete selector chain */
typedef struct {
    css_selector_step_t *key_selector;   /* Rightmost selector (matched first against node) */
    uint32_t             specificity;    /* a*10000 + b*100 + c */
} css_selector_chain_t;

css_selector_step_t  *css_selector_step_create(void);
void                  css_selector_step_free(css_selector_step_t *step);

css_selector_chain_t *css_selector_chain_create(void);
void                  css_selector_chain_free(css_selector_chain_t *chain);

uint32_t              css_selector_calc_specificity(const css_selector_chain_t *chain);
bool                  css_selector_match(const css_selector_chain_t *chain, const bb_node_t *node);

#endif /* CSS_SELECTORS_H */

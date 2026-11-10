#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "dom.h"
#include "style.h"

typedef enum {
    SELECTOR_UNIVERSAL,
    SELECTOR_TAG,
    SELECTOR_CLASS,
    SELECTOR_ID
} SelectorType;

typedef struct Selector {
    SelectorType type;
    char *value;
    struct Selector *next;
} Selector;

typedef struct Declaration {
    char *property;
    char *value;
    struct Declaration *next;
} Declaration;

typedef struct CSSRule {
    Selector *selectors;
    Declaration *declarations;
    int specificity;
    struct CSSRule *next;
} CSSRule;

typedef struct Stylesheet {
    CSSRule *rules;
} Stylesheet;

Stylesheet *css_parse(const char *css_source);
void stylesheet_free(Stylesheet *sheet);
void css_apply_stylesheet(DOMNode *root, const Stylesheet *sheet);

#endif /* CSS_PARSER_H */

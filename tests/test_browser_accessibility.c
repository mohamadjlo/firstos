#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

#include "../include/firstos/accessibility/ax_tree.h"
#include "../userspace/browser/include/firstos_renderer.h"

void test_ax_tree_init(void) {
    firstos_ax_tree_t tree;
    firstos_ax_tree_init(&tree, 42, 101);
    assert(tree.tab_id == 42);
    assert(tree.frame_id == 101);
    assert(tree.generation == 1);
    assert(tree.node_count == 0);
    assert(firstos_ax_tree_get_node(&tree, 1) == NULL);
    printf("PASS: test_ax_tree_init\n");
}

void test_ax_tree_build_semantics_and_roles(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Create mock DOM nodes */
    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_HTML;

    html_node_t body;
    memset(&body, 0, sizeof(body));
    body.id_num = 2;
    body.tag = HTML_TAG_BODY;
    body.parent = &root;
    root.first_child = &body;

    html_node_t h1;
    memset(&h1, 0, sizeof(h1));
    h1.id_num = 3;
    h1.tag = HTML_TAG_H1;
    h1.text = "Page Title";
    h1.parent = &body;
    body.first_child = &h1;

    html_node_t btn;
    memset(&btn, 0, sizeof(btn));
    btn.id_num = 4;
    btn.tag = HTML_TAG_BUTTON;
    btn.text = "Click Me";
    btn.parent = &body;
    h1.next_sibling = &btn;

    html_node_t chk;
    memset(&chk, 0, sizeof(chk));
    chk.id_num = 5;
    chk.tag = HTML_TAG_INPUT;
    chk.input_type = "checkbox";
    chk.checked = 1;
    chk.is_focused = 1;
    chk.parent = &body;
    btn.next_sibling = &chk;

    ctx.root_dom = &root;
    ctx.node_count = 5;

    firstos_ax_tree_t tree;
    firstos_ax_tree_init(&tree, 1, 1);
    int rc = firstos_ax_tree_build(&ctx, &tree);
    assert(rc > 0);
    assert(tree.node_count >= 5);

    /* Verify root */
    const firstos_ax_node_t *ax_root = firstos_ax_tree_get_node(&tree, 1);
    assert(ax_root != NULL);
    assert(ax_root->role == AX_ROLE_DOCUMENT);

    /* Verify h1 heading */
    const firstos_ax_node_t *ax_h1 = firstos_ax_tree_get_node(&tree, 3);
    assert(ax_h1 != NULL);
    assert(ax_h1->role == AX_ROLE_HEADING);
    assert(ax_h1->heading_level == 1);
    assert(strcmp(ax_h1->name, "Page Title") == 0);

    /* Verify button */
    const firstos_ax_node_t *ax_btn = firstos_ax_tree_get_node(&tree, 4);
    assert(ax_btn != NULL);
    assert(ax_btn->role == AX_ROLE_BUTTON);
    assert(strcmp(ax_btn->name, "Click Me") == 0);

    /* Verify checkbox state flags */
    const firstos_ax_node_t *ax_chk = firstos_ax_tree_get_node(&tree, 5);
    assert(ax_chk != NULL);
    assert(ax_chk->role == AX_ROLE_CHECKBOX);
    assert((ax_chk->state_flags & AX_STATE_CHECKED) != 0);
    assert((ax_chk->state_flags & AX_STATE_FOCUSED) != 0);

    printf("PASS: test_ax_tree_build_semantics_and_roles\n");
}

void test_ax_password_masking(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_BODY;

    html_node_t pwd_input;
    memset(&pwd_input, 0, sizeof(pwd_input));
    pwd_input.id_num = 2;
    pwd_input.tag = HTML_TAG_INPUT;
    pwd_input.input_type = "password";
    strcpy(pwd_input.value, "SuperSecretPassword123!");
    pwd_input.value_len = strlen(pwd_input.value);
    pwd_input.parent = &root;
    root.first_child = &pwd_input;

    ctx.root_dom = &root;
    ctx.node_count = 2;

    firstos_ax_tree_t tree;
    firstos_ax_tree_init(&tree, 1, 1);
    int rc = firstos_ax_tree_build(&ctx, &tree);
    assert(rc > 0);

    const firstos_ax_node_t *ax_pwd = firstos_ax_tree_get_node(&tree, 2);
    assert(ax_pwd != NULL);
    assert(ax_pwd->role == AX_ROLE_TEXTBOX);
    assert(strcmp(ax_pwd->value, "********") == 0);
    assert(strstr(ax_pwd->value, "SuperSecret") == NULL);

    printf("PASS: test_ax_password_masking\n");
}

void test_ax_tree_dump_output(void) {
    firstos_ax_tree_t tree;
    firstos_ax_tree_init(&tree, 1, 1);
    tree.node_count = 2;

    tree.nodes[0].ax_id = 1;
    tree.nodes[0].role = AX_ROLE_DOCUMENT;
    strcpy(tree.nodes[0].name, "Root");

    tree.nodes[1].ax_id = 2;
    tree.nodes[1].role = AX_ROLE_BUTTON;
    strcpy(tree.nodes[1].name, "Submit");
    tree.nodes[1].state_flags = AX_STATE_FOCUSED;

    char dump_buf[512];
    int len = firstos_ax_tree_dump(&tree, dump_buf, sizeof(dump_buf));
    assert(len > 0);
    assert(strstr(dump_buf, "[AX_TREE] Nodes=2:") != NULL);
    assert(strstr(dump_buf, "role=button") != NULL);
    assert(strstr(dump_buf, "name=\"Submit\"") != NULL);
    assert(strstr(dump_buf, "[focused]") != NULL);

    printf("PASS: test_ax_tree_dump_output\n");
}

int main(void) {
    printf("=== Starting Phase 6A.3 Accessibility Tree Unit Tests ===\n");
    test_ax_tree_init();
    test_ax_tree_build_semantics_and_roles();
    test_ax_password_masking();
    test_ax_tree_dump_output();
    printf("=== All 4 Accessibility Unit Tests PASSED ===\n");
    return 0;
}

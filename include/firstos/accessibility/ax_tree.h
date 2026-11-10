#ifndef FIRSTOS_AX_TREE_H
#define FIRSTOS_AX_TREE_H

#include <stdint.h>

#define FIRSTOS_MAX_AX_NODES_PER_FRAME 512u
#define FIRSTOS_MAX_AX_NAME            128u
#define FIRSTOS_MAX_AX_VALUE           256u
#define FIRSTOS_MAX_AX_DESCRIPTION     256u

typedef enum {
    AX_ROLE_NONE = 0,
    AX_ROLE_DOCUMENT,
    AX_ROLE_MAIN,
    AX_ROLE_SECTION,
    AX_ROLE_HEADING,
    AX_ROLE_PARAGRAPH,
    AX_ROLE_LINK,
    AX_ROLE_BUTTON,
    AX_ROLE_TEXTBOX,
    AX_ROLE_CHECKBOX,
    AX_ROLE_RADIO,
    AX_ROLE_IMAGE,
    AX_ROLE_IFRAME,
    AX_ROLE_GENERIC
} firstos_ax_role_t;

/* State Bitflags */
#define AX_STATE_NONE      0x0000u
#define AX_STATE_FOCUSED   0x0001u
#define AX_STATE_DISABLED  0x0002u
#define AX_STATE_CHECKED   0x0004u
#define AX_STATE_SELECTED  0x0008u
#define AX_STATE_READONLY  0x0010u
#define AX_STATE_REQUIRED  0x0020u
#define AX_STATE_HIDDEN    0x0040u
#define AX_STATE_BUSY      0x0080u

typedef struct {
    int x;
    int y;
    int w;
    int h;
} firstos_ax_rect_t;

typedef struct {
    uint32_t          ax_id;              /* 1-based node ID */
    uint32_t          generation;         /* Tree generation */
    uint32_t          tab_id;
    uint32_t          frame_id;
    uint32_t          dom_node_id;        /* Associated DOM node ID */
    firstos_ax_role_t role;
    uint16_t          state_flags;
    uint8_t           heading_level;      /* 1-6 for headings */
    uint8_t           reserved;

    /* Topology */
    uint32_t          parent_ax_id;       /* 0 for root document */
    uint32_t          child_count;

    /* Computed Geometry */
    firstos_ax_rect_t bounds;

    /* Accessible Text Metadata */
    char              name[FIRSTOS_MAX_AX_NAME];
    char              value[FIRSTOS_MAX_AX_VALUE];
    char              description[FIRSTOS_MAX_AX_DESCRIPTION];
} firstos_ax_node_t;

typedef struct {
    firstos_ax_node_t nodes[FIRSTOS_MAX_AX_NODES_PER_FRAME];
    uint32_t          node_count;
    uint32_t          generation;
    uint32_t          tab_id;
    uint32_t          frame_id;
} firstos_ax_tree_t;

/* API */
void firstos_ax_tree_init(firstos_ax_tree_t *tree, uint32_t tab_id, uint32_t frame_id);
const firstos_ax_node_t *firstos_ax_tree_get_node(const firstos_ax_tree_t *tree, uint32_t ax_id);
int  firstos_ax_tree_dump(const firstos_ax_tree_t *tree, char *out_buf, int max_len);

#endif /* FIRSTOS_AX_TREE_H */

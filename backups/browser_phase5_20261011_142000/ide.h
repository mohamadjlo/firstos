#ifndef IDE_H
#define IDE_H

typedef struct {
    char text[32768];
    int length;
    int cursor;
    int scroll_y;
    int scroll_x;
    int saved;
    int select_anchor;
    int select_active;
    char filename[32];
    char undo_text[32768];
} ide_tab_t;

extern ide_tab_t ide_tabs[4];
extern int ide_active_tab;

#define ide_text (ide_tabs[ide_active_tab].text)
#define ide_length (ide_tabs[ide_active_tab].length)
#define ide_cursor (ide_tabs[ide_active_tab].cursor)
#define ide_scroll_y (ide_tabs[ide_active_tab].scroll_y)
#define ide_scroll_x (ide_tabs[ide_active_tab].scroll_x)
#define ide_saved (ide_tabs[ide_active_tab].saved)
#define ide_select_anchor (ide_tabs[ide_active_tab].select_anchor)
#define ide_select_active (ide_tabs[ide_active_tab].select_active)
#define ide_filename (ide_tabs[ide_active_tab].filename)
#define ide_undo_text (ide_tabs[ide_active_tab].undo_text)

extern void ide_init_tabs(void);
extern void ide_switch_tab(int index);

#endif

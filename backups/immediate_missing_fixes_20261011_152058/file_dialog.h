#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#define FILE_DIALOG_OPEN   1
#define FILE_DIALOG_SAVE   2
#define FILE_DIALOG_IMPORT 3

void file_dialog_init(void);
void file_dialog_show(int caller_kind, int mode, const char *default_name, const char *initial_path);
void file_dialog_close(void);
int  file_dialog_is_open(void);
void file_dialog_draw(void);
int  file_dialog_handle_click(int mx, int my);
int  file_dialog_handle_drag(int mx, int my);
int  file_dialog_handle_scroll(int delta);
int  file_dialog_handle_key(unsigned char key, char ascii);

#endif /* FILE_DIALOG_H */

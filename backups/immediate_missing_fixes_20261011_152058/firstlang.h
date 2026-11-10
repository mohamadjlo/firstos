#ifndef FIRSTLANG_H
#define FIRSTLANG_H

#define FL_OUTPUT_MAX 4096

/*
 * FirstLang v1.0 — FirstOS Domain-Specific Language
 *
 * Syntax:
 *   class init: function UI, function check, function copy.
 *
 *   function UI(buttons, radio box, slider)
 *     buttons positions: (center, top).
 *
 *   function check: directory Desktop any new files with format .jpg
 *
 *   function copy: copy detected from check to Desktop/images.
 *
 * Commands from terminal:
 *   fl run <script_file>   — run a FirstLang script from FS
 *   fl eval <code>         — evaluate inline FirstLang code
 *   fl new <filename>      — create a new script template
 */

/* Run a FirstLang source string. Returns number of functions executed, -1 on error. */
int firstlang_run(const char *source, char *output_buf, int output_sz);

/* Run a FirstLang script from a file path in the FS. */
int firstlang_run_file(const char *path, char *output_buf, int output_sz);

/* Check if the language has rendered a UI window */
int  firstlang_has_ui(void);
void firstlang_close_ui(void);
void firstlang_draw_ui(void);
int firstlang_mouse_click(int mx, int my);

#endif

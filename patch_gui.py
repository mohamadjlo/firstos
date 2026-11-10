import sys
content = open('gui.c').read()

# Replace the Top Bar FirstOS icon string with native image loading
if 'draw_label(14, 10, "Fi", top_text_color);' in content:
    replacement = """    static u8* firstos_ico = 0; static int f_w, f_h;
    if (!firstos_ico) firstos_ico = image_load("/firstOS.ico", &f_w, &f_h);
    if (firstos_ico) image_draw_alpha(firstos_ico, 12, 6, f_w, f_h);
    else draw_label(14, 10, "Fi", top_text_color);"""
    content = content.replace('draw_label(14, 10, "Fi", top_text_color);', replacement)

# Replace the Dock Terminal icon string
# The dock draws icons by: font_icon(x + 10, y + 10, dock_apps[i], color);
if 'font_icon(x + 10, y + 10, dock_apps[i], color);' in content:
    replacement = """if (dock_apps[i] == ICON_TERMINAL) {
                        static u8* t_png = 0; static int tw, th;
                        if (!t_png) t_png = image_load("/icons/terminal.png", &tw, &th);
                        if (t_png) image_draw_alpha(t_png, x+6, y+6, tw, th);
                        else font_icon(x + 10, y + 10, dock_apps[i], color);
                    } else if (dock_apps[i] == ICON_SETTINGS) {
                        static u8* s_png = 0; static int sw, sh;
                        if (!s_png) s_png = image_load("/icons/settings.png", &sw, &sh);
                        if (s_png) image_draw_alpha(s_png, x+6, y+6, sw, sh);
                        else font_icon(x + 10, y + 10, dock_apps[i], color);
                    } else if (dock_apps[i] == ICON_EXPLORER) {
                        static u8* e_png = 0; static int ew, eh;
                        if (!e_png) e_png = image_load("/icons/explorer.png", &ew, &eh);
                        if (e_png) image_draw_alpha(e_png, x+6, y+6, ew, eh);
                        else font_icon(x + 10, y + 10, dock_apps[i], color);
                    } else {
                        font_icon(x + 10, y + 10, dock_apps[i], color);
                    }"""
    content = content.replace('font_icon(x + 10, y + 10, dock_apps[i], color);', replacement)

open('gui.c', 'w').write(content)

import sys

with open('gui.c', 'r') as f:
    code = f.read()

# Add to globals
globals_patch = """
int desktop_drag_mx = 0;
int desktop_drag_my = 0;
"""
code = code.replace("int desktop_dragging = 0;", globals_patch + "int desktop_dragging = 0;")

# Update in update_drag
drag_up = """    if (left && desktop_dragging) {"""
drag_up_new = """    if (left && desktop_dragging) {
        desktop_drag_mx = mx;
        desktop_drag_my = my;"""
code = code.replace(drag_up, drag_up_new)

with open('gui.c', 'w') as f:
    f.write(code)

with open('desktop.c', 'r') as f:
    desk = f.read()

draw_icon = """            extern gui_event_t gui_last_event;
            item_x = gui_last_event.x - 24;
            item_y = gui_last_event.y - 24;"""
draw_icon_new = """            extern int desktop_drag_mx;
            extern int desktop_drag_my;
            item_x = desktop_drag_mx - 24;
            item_y = desktop_drag_my - 24;"""
desk = desk.replace(draw_icon, draw_icon_new)

with open('desktop.c', 'w') as f:
    f.write(desk)

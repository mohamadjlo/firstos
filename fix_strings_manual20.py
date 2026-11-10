import re
with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('serial_write("[gui] before sync_main_window_parts\n");', 'serial_write("[gui] before sync_main_window_parts\\n");')
text = text.replace('serial_write("[gui] after sync_main_window_parts\n");', 'serial_write("[gui] after sync_main_window_parts\\n");')
text = text.replace('serial_write("[gui] before app_windows loop\n");', 'serial_write("[gui] before app_windows loop\\n");')
text = text.replace('serial_write("[gui] app_window iteration\n', 'serial_write("[gui] app_window iteration\\n');

with open('gui.c', 'w') as f:
    f.write(text)

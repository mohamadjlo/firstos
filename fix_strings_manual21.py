import re
with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('serial_write("[gui] after app_windows loop\n");', 'serial_write("[gui] after app_windows loop\\n");')
text = text.replace('serial_write("[gui] exit gui_init\n");', 'serial_write("[gui] exit gui_init\\n");')

with open('gui.c', 'w') as f:
    f.write(text)

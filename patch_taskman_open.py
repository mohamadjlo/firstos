with open('kernel.c', 'r') as f:
    text = f.read()
text = text.replace('gui_init();', 'gui_init();\n    extern void launch_app(int kind);\n    launch_app(9); // APP_TASKMAN')
with open('kernel.c', 'w') as f:
    f.write(text)

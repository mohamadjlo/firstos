with open('gui.c', 'r') as f:
    text = f.read()
import re
print("open_app:", "static void open_app" in text)
print("open_app def:", "static void open_app(int kind) {" in text)

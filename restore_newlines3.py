with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('\\\\n', '\\n')
# Now replace '\\n' with actual newline everywhere
# But wait, wait! The previous `restore_newlines2.py` already replaced `\\n` with `\n`? No, it replaced `\n` string with actual newline.
# I will just take `gui.c` and try to manually split the string at `\n` everywhere EXCEPT where it's a known string!
# Let's just fix it manually.

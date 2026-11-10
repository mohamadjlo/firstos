import re
with open('gui.c', 'r') as f:
    text = f.read()

# Replace any literal newline that is between two strings or whatever
# We can find all instances of literal newline that is right before `");`
text = re.sub(r'\n"\);', r'\\n");', text)
text = re.sub(r'\n",', r'\\n",', text)

with open('gui.c', 'w') as f:
    f.write(text)

import re

with open('gui.c', 'r') as f:
    text = f.read()

# I want to find instances of a string literal that was broken by a literal newline.
# Basically, a line ending with a string that is not closed, followed by a line starting with `"`.
# Actually, the error says:
# gui.c:447:27: error: missing terminating " character
# gui.c:6386:18: error: missing terminating " character

# The corruption looks like:
# serial_write("[gui] enter gui_init
# ");
# OR:
# "Type your
# notes here.
# "
# We can just replace the literal '\n' that is NOT inside a macro but IS inside a string.
# A simpler way: just read line by line. If a line has an odd number of double quotes, join it with the next line, replacing the newline with \n.
# Same for single quotes.

def fix_quotes(lines, quote_char):
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        # Count non-escaped quotes
        count = len(re.findall(r'(?<!\\)' + quote_char, line))
        if count % 2 != 0:
            # It's an open quote!
            # Join with next lines until closed
            merged = line.rstrip('\n') + '\\n'
            i += 1
            while i < len(lines):
                next_line = lines[i]
                next_count = len(re.findall(r'(?<!\\)' + quote_char, next_line))
                if next_count % 2 != 0:
                    merged += next_line # The closing quote is here
                    break
                else:
                    merged += next_line.rstrip('\n') + '\\n'
                    i += 1
            out.append(merged)
        else:
            out.append(line)
        i += 1
    return out

with open('gui.c', 'r') as f:
    lines = f.readlines()

lines = fix_quotes(lines, '"')
lines = fix_quotes(lines, "'")

with open('gui.c', 'w') as f:
    f.writelines(lines)


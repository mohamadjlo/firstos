with open('gui.c', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if line.endswith('\\\n'):
        # Keep it if we are inside WC_FILL macro
        # Actually I can just check if the line starts with #define WC_FILL or if it's the 10 lines after it.
        if i >= 2184 and i <= 2192: continue
        if i >= 2210 and i <= 2218: continue
        lines[i] = line[:-2] + '\n'

with open('gui.c', 'w') as f:
    f.writelines(lines)

import sys

with open('/Users/mohamadjlo/Desktop/FirstOS/html_parser.c', 'r') as f:
    lines = f.read()

# find the replacement block and fix it.
# Actually I know exactly what I injected. I will find the marker "/* ============================================================\n * Recursive Layout Engine" and the end of html_layout

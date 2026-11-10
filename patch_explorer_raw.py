import sys
content = open('explorer.c').read()

patch = """int explorer_is_image_file(const char *name) {
    return explorer_str_ends_ci(name,".jpg") || explorer_str_ends_ci(name,".jpeg") ||
           explorer_str_ends_ci(name,".png") || explorer_str_ends_ci(name,".bmp") ||
           explorer_str_ends_ci(name,".gif") || explorer_str_ends_ci(name,".raw");
}"""

if 'int explorer_is_image_file(const char *name) {' in content:
    idx1 = content.find('int explorer_is_image_file(const char *name) {')
    idx2 = content.find('}', idx1) + 1
    content = content[:idx1] + patch + content[idx2:]
    open('explorer.c', 'w').write(content)

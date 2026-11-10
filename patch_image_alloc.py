import sys
content = open('image.c').read()

patch = """    int file_size = 0;
    int fs_total = fs_count();
    for (int i = 0; i < fs_total; i++) {
        const fs_entry_t *e = fs_entry_at(i);
        if (e && e->path[0]) {
            int p1 = 0, p2 = 0;
            while(e->path[p1] && path[p2] && e->path[p1] == path[p2]) { p1++; p2++; }
            if (e->path[p1] == 0 && path[p2] == 0) {
                file_size = e->size;
                break;
            }
        }
    }
    
    if (file_size <= 0) file_size = 5 * 1024 * 1024; // Fallback to 5MB
    
    char *buf = (char*)kmalloc(file_size + 1024);
    if (!buf) return 0;
    int n = fs_read_at(path, buf, file_size + 1024);
"""

if 'char *buf = (char*)kmalloc(512 * 1024);' in content:
    idx1 = content.find('char *buf = (char*)kmalloc(512 * 1024);')
    idx2 = content.find('    if (n <= 0) { kfree(buf); return 0; }', idx1) + 41
    content = content[:idx1] + patch + content[idx2:]
    
open('image.c', 'w').write(content)

with open('heap.c', 'r') as f:
    text = f.read()

bad_func = '''u32 get_heap_used(void) {
    __asm__ volatile("cli");
    u32 total_used = 0;
    heap_block_t *b = heap_first;
    while(b) {
        if(b->used) total_used += b->size;
        b = b->next;
    }
    heap_used = total_used;
    __asm__ volatile("sti");
    return total_used;
}'''
text = text.replace(bad_func, '')

text = text.replace('b->used = 1;', 'b->used = 1;\n        heap_used += b->size;')
text = text.replace('b->used = 0;', 'b->used = 0;\n    if (heap_used >= b->size) heap_used -= b->size; else heap_used = 0;')

with open('heap.c', 'w') as f:
    f.write(text)

with open('taskman.c', 'r') as f:
    text = f.read()
text = text.replace('    extern u32 get_heap_used(void);\n    heap_used = get_heap_used();', '')
with open('taskman.c', 'w') as f:
    f.write(text)

with open('virtio_gpu.c', 'r') as f:
    text = f.read()

text = text.replace('volatile u16 *notify;', 'volatile u16 *notify;\n    volatile u16 *ctrl_notify_ptr;')

with open('virtio_gpu.c', 'w') as f:
    f.write(text)

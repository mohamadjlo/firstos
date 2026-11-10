with open('virtio_gpu.c', 'r') as f:
    text = f.read()

text = text.replace('transport.common->queue_enable = 1;\n    transport.common->queue_enable = 0;', 'transport.common->queue_enable = 1;')

with open('virtio_gpu.c', 'w') as f:
    f.write(text)

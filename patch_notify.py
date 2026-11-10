with open('virtio_gpu.h', 'r') as f:
    text = f.read()
text = text.replace('volatile u16 *notify;', 'volatile u16 *notify;\n    volatile u16 *ctrl_notify_ptr;')
with open('virtio_gpu.h', 'w') as f:
    f.write(text)

with open('virtio_gpu.c', 'r') as f:
    text = f.read()

old_notify = '''static void virtio_gpu_notify_queue(void) {
    u16 notify_off;
    volatile u16 *notify_ptr;
    if (!transport.notify || !transport.common) return;
    transport.common->queue_select = VIRTIO_GPU_QUEUE_CTRL;
    __sync_synchronize();
    notify_off = transport.common->queue_notify_off;
    notify_ptr = (volatile u16*)((volatile u8*)transport.notify + (u32)notify_off * transport.notify_off_multiplier);
    *notify_ptr = VIRTIO_GPU_QUEUE_CTRL;
}'''

new_notify = '''static void virtio_gpu_notify_queue(void) {
    if (!transport.ctrl_notify_ptr) return;
    *transport.ctrl_notify_ptr = VIRTIO_GPU_QUEUE_CTRL;
}'''

text = text.replace(old_notify, new_notify)

setup_hook = '''transport.ctrl_ready = (transport.common->queue_enable == 1);'''
setup_new = '''transport.ctrl_ready = (transport.common->queue_enable == 1);
    transport.ctrl_notify_ptr = (volatile u16*)((volatile u8*)transport.notify + (u32)notify_off * transport.notify_off_multiplier);'''

text = text.replace(setup_hook, setup_new)

with open('virtio_gpu.c', 'w') as f:
    f.write(text)

with open('virtio_gpu.c', 'r') as f:
    text = f.read()

bad_struct = '''typedef struct {
    volatile u32 device_feature_select;
    volatile u32 device_feature;
    volatile u32 driver_feature_select;
    volatile u32 driver_feature;
    volatile u16 msix_config;
    volatile u16 num_queues;
    volatile u8 device_status;
    volatile u8 config_generation;
    volatile u16 queue_select;
    volatile u16 queue_size;
    volatile u16 queue_msix_vector;
    volatile u16 queue_enable;
    volatile u16 queue_notify_off;
    volatile u64 queue_desc;
    volatile u64 queue_driver;
    volatile u64 queue_device;
    volatile u16 queue_notify_data;
    volatile u16 queue_reset;
    volatile u16 admin_queue_index;
    volatile u16 admin_queue_num;
} __attribute__((packed)) virtio_pci_common_cfg_t;'''

good_struct = '''typedef struct {
    u32 device_feature_select;
    u32 device_feature;
    u32 driver_feature_select;
    u32 driver_feature;
    u16 msix_config;
    u16 num_queues;
    u8 device_status;
    u8 config_generation;
    u16 queue_select;
    u16 queue_size;
    u16 queue_msix_vector;
    u16 queue_enable;
    u16 queue_notify_off;
    u64 queue_desc;
    u64 queue_driver;
    u64 queue_device;
    u16 queue_notify_data;
    u16 queue_reset;
    u16 admin_queue_index;
    u16 admin_queue_num;
} __attribute__((packed)) virtio_pci_common_cfg_t;'''

text = text.replace(bad_struct, good_struct)
with open('virtio_gpu.c', 'w') as f:
    f.write(text)

#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H

#include "pci.h"
#include "virtio_input_protocol.h"

typedef struct {
    int present;
    int initialized;
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u32 bar0;
    u32 bar1;
    int is_mouse;
    int is_tablet;
    u32 abs_x_min, abs_x_max;
    u32 abs_y_min, abs_y_max;
} virtio_input_info_t;

void virtio_input_probe(void);
const virtio_input_info_t* virtio_input_info(void);
int virtio_input_available(void);
int virtio_input_init(void);
int virtio_input_poll_event(virtio_input_event_t *evt);
void virtio_input_requeue_event(void);

#endif

#ifndef VIRTIO_INPUT_PROTOCOL_H
#define VIRTIO_INPUT_PROTOCOL_H

/* VirtIO Input Device IDs */
#define VIRTIO_INPUT_ID_BUS_PCI     0x00
#define VIRTIO_INPUT_ID_BUS_USB     0x03

/* Event types */
#define EV_SYN                    0x00
#define EV_KEY                    0x01
#define EV_REL                    0x02
#define EV_ABS                    0x03
#define EV_MSC                    0x04
#define EV_SW                     0x05
#define EV_LED                    0x11
#define EV_SND                    0x12
#define EV_REP                    0x14
#define EV_FF                     0x15
#define EV_PWR                    0x16
#define EV_FF_STATUS              0x17

/* Key codes (mouse) */
#define BTN_LEFT                  0x110
#define BTN_RIGHT                 0x111
#define BTN_MIDDLE                0x112

/* Relative axes */
#define REL_X                     0x00
#define REL_Y                     0x01
#define REL_WHEEL                 0x08

/* Absolute axes */
#define ABS_X                     0x00
#define ABS_Y                     0x01

/* Config select/subsel */
#define VIRTIO_INPUT_CFG_ID_NAME        0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL      0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS      0x03
#define VIRTIO_INPUT_CFG_EV_BITS        0x10
#define VIRTIO_INPUT_CFG_ABS_INFO       0x11

/* Event structure */
typedef struct __attribute__((packed)) {
    u16 type;
    u16 code;
    u32 value;
} virtio_input_event_t;

/* Config space structure */
typedef struct __attribute__((packed)) {
    u8 select;
    u8 subsel;
    u32 size;
    u8 data[1024];
} virtio_input_config_t;

/* Absolute info structure */
typedef struct __attribute__((packed)) {
    u32 min;
    u32 max;
    u32 fuzz;
    u32 flat;
    u32 res;
} virtio_input_absinfo_t;

/* Device IDs structure */
typedef struct __attribute__((packed)) {
    u16 bustype;
    u16 vendor;
    u16 product;
    u16 version;
} virtio_input_devids_t;

#endif

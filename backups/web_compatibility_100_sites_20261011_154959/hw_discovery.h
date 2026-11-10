#ifndef HW_DISCOVERY_H
#define HW_DISCOVERY_H

typedef struct {
    int device_id;
    char name[64];
    int is_active;
} audio_device_t;

typedef struct {
    int width;
    int height;
    int refresh_rate;
} display_resolution_t;

typedef struct {
    char adapter_name[128];
    display_resolution_t resolutions[8];
    int num_resolutions;
    int active_res_index;
} display_info_t;

typedef struct {
    int port_id;
    char name[32];
} serial_port_t;

typedef struct {
    char name[64];
    int battery_percent;
} gamepad_device_t;

void hw_discovery_init(void);
int hw_get_audio_outputs(audio_device_t *out_devices, int max);
display_info_t hw_get_display_info(void);

int hw_get_serial_ports(serial_port_t *ports, int max);
int hw_get_gamepads(gamepad_device_t *pads, int max);

#endif

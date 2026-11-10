#include "hw_discovery.h"
#include "pci.h"

void hw_discovery_init(void) {
    // Initialization if caching is needed
}

int hw_get_audio_outputs(audio_device_t *out_devices, int max) {
    if (max < 1) return 0;
    int count = 0;
    
    // FirstOS uses a generic HDA driver. Let's see if we can find an HDA controller on PCI.
    // Class 0x04 (Multimedia), Subclass 0x03 (Audio Device)
    const pci_device_t* hda_dev = pci_find_class(0x04, 0x03);
    
    if (hda_dev) {
        out_devices[count].device_id = 1;
        // Basic vendor identification
        if (hda_dev->vendor_id == 0x8086) {
            char* name = "Intel HDA Controller";
            int i = 0; while (name[i]) { out_devices[count].name[i] = name[i]; i++; } out_devices[count].name[i] = 0;
        } else {
            char* name = "Generic HD Audio";
            int i = 0; while (name[i]) { out_devices[count].name[i] = name[i]; i++; } out_devices[count].name[i] = 0;
        }
        out_devices[count].is_active = 1;
        count++;
    } else {
        // Fallback fake device for UI demonstration if no HDA found in QEMU
        char* name = "Virtual Audio Sink";
        int i = 0; while (name[i]) { out_devices[count].name[i] = name[i]; i++; } out_devices[count].name[i] = 0;
        out_devices[count].device_id = 99;
        out_devices[count].is_active = 1;
        count++;
    }
    
    return count;
}

display_info_t hw_get_display_info(void) {
    display_info_t info;
    info.num_resolutions = 0;
    info.active_res_index = 0;
    
    // Query PCI
    pci_describe_primary_display(info.adapter_name, sizeof(info.adapter_name));
    
    // Add resolutions supported by FirstOS
    info.resolutions[0].width = 1024; info.resolutions[0].height = 768;  info.resolutions[0].refresh_rate = 60;
    info.resolutions[1].width = 1280; info.resolutions[1].height = 720;  info.resolutions[1].refresh_rate = 60;
    info.resolutions[2].width = 1280; info.resolutions[2].height = 800;  info.resolutions[2].refresh_rate = 60;
    info.resolutions[3].width = 1440; info.resolutions[3].height = 900;  info.resolutions[3].refresh_rate = 60;
    info.resolutions[4].width = 1600; info.resolutions[4].height = 900;  info.resolutions[4].refresh_rate = 60;
    info.resolutions[5].width = 1920; info.resolutions[5].height = 1080; info.resolutions[5].refresh_rate = 60;
    info.num_resolutions = 6;
    
    extern struct { int width; int height; } fb;
    for (int i = 0; i < info.num_resolutions; i++) {
        if (info.resolutions[i].width == fb.width && info.resolutions[i].height == fb.height) {
            info.active_res_index = i;
            break;
        }
    }
    
    return info;
}

int hw_get_serial_ports(serial_port_t *ports, int max) {
    if (max < 1) return 0;
    // FirstOS serial.c maps COM1 at 0x3F8
    ports[0].port_id = 0x3F8;
    char* name = "COM1 (0x3F8) - Serial Port";
    int i = 0; while (name[i]) { ports[0].name[i] = name[i]; i++; } ports[0].name[i] = 0;
    return 1;
}

int hw_get_gamepads(gamepad_device_t *pads, int max) {
    if (max < 1) return 0;
    // Mock gamepad
    char* name = "Virtual Xbox Controller (USB)";
    int i = 0; while (name[i]) { pads[0].name[i] = name[i]; i++; } pads[0].name[i] = 0;
    pads[0].battery_percent = 85;
    return 1;
}

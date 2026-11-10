#include "framebuffer.h"
#include "gui.h"
#include "compositor.h"
#include "gpu_registry.h"
#include "gpu_generic_fb.h"
#include "gpu_virtio.h"
#include "gpu_detect.h"
#include "cpu.h"
#include "serial.h"
#include "ps2.h"
#include "fs.h"
#include "virtio_blk.h"
#include "pci.h"
#include "hda.h"
#include "sound.h"
#include "io.h"
#include "settings.h"
#include "net.h"
#include "heap.h"
#include "memory.h"
#include "string.h"
#include "download_manager.h"
#include "browser_storage.h"
#include "browser_host_ipc.h"
#include "browser_network_service.h"
#include "browser_renderer_pipeline.h"
#include "html5_engine.h"
#include "html5_script.h"
#include "browser_logger.h"
#include "tls.h"
#include "include/firstos/security/browser_security.h"
#include "process.h"
#include "include/firstos/uapi/syscall.h"
#include "ktrace.h"
#include "kernel_cmd.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct __attribute__((packed)) {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    union {
        struct {
            u32 framebuffer_palette_addr;
            u16 framebuffer_palette_num_colors;
        } palette;
        struct {
            u8 red_field_position;
            u8 red_mask_size;
            u8 green_field_position;
            u8 green_mask_size;
            u8 blue_field_position;
            u8 blue_mask_size;
        } rgb;
    } color;
} multiboot_info_t;

typedef struct __attribute__((packed)) {
    u32 mod_start;
    u32 mod_end;
    u32 string;
    u32 reserved;
} multiboot_module_t;

static volatile unsigned short* const vga = (unsigned short*)0xB8000;

static void vga_message(const char* msg) {
    int i = 0;
    while (msg[i] && i < 80 * 25) {
        vga[i] = (unsigned short)msg[i] | ((unsigned short)0x4F << 8);
        i++;
    }
}

static void fail(const char* message) {
    serial_write("FAIL: ");
    serial_write(message);
    serial_write("\n");
    vga_message(message);
    for (;;) {
        ps2_poll();
        if (gui_power_off_requested()) {
            serial_write("[kernel] shutdown requested\n");
            outb(0xF4, 0x00);
            outb(0x604, 0x00);
            cpu_halt_forever();
        }
        if (gui_needs_redraw()) {
            gui_draw();
            serial_write("[kernel] frame end\n");
        }
        cpu_pause();
    }
}

static multiboot_module_t *boot_modules;
static u32 boot_module_count;

static void platform_poweroff(void) {
    serial_write("[kernel] attempting qemu poweroff\n");
    if (fs_ready()) {
        serial_write("[kernel] poweroff fs_ready=1\n");
        if (fs_is_dirty()) {
            serial_write("[kernel] poweroff syncing dirty fs\n");
            if (!fs_sync()) serial_write("[kernel] poweroff fs_sync failed\n");
            else serial_write("[kernel] poweroff fs_sync ok\n");
        } else {
            serial_write("[kernel] poweroff fs already clean\n");
        }
    } else {
        serial_write("[kernel] poweroff fs_ready=0\n");
    }
    serial_write("[kernel] poweroff via 0x604/0x2000\n");
    sound_play_event(SOUND_EVENT_SHUTDOWN);
    sound_wait_finished(1500);
    outw(0x604, 0x2000);
    serial_write("[kernel] poweroff via 0xB004/0x2000\n");
    outw(0xB004, 0x2000);
    cpu_halt_forever();
}

void kernel_post_boot_fs_load(void) {
    serial_write("[kernel] post-boot fs load start\n");
    settings_load_from_disk();
    serial_write("[kernel] post-boot fs load done\n");
}

int gui_is_idle = 0;

/* Note: serial_poll_commands moved to kernel_cmd.c */


void kernel_main(u32 multiboot_magic, u32 multiboot_addr) {
    multiboot_info_t* mb = (multiboot_info_t*)(unsigned long)multiboot_addr;
    u32 palette_addr = 0;
    u16 palette_size = 0;

    serial_init();
    ktrace_init();
    ktrace_write(KTRACE_SUBSYS_KERNEL, KTRACE_LEVEL_INFO, 0, "entered kernel_main");
    kernel_cmd_init();

    fpu_init();
    serial_write("[kernel] FPU/SSE initialized\n");

    extern void gdt_init(void);
    gdt_init();
    
    extern void idt_init(void);
    idt_init();

    extern void syscall_init(void);
    syscall_init();

    extern void cpu_smp_init(void);
    cpu_smp_init();

    boot_modules = 0;
    boot_module_count = 0;
    serial_write("[kernel] multiboot_magic=");
    serial_write_hex(multiboot_magic);
    serial_write("\n");

    memory_init(mb);

    extern void heap_init(void);
    heap_init();
    serial_write("[kernel] heap initialized\n");
    
    extern void paging_init(void);
    paging_init();
    
    extern void pic_init(void);
    pic_init();
    serial_write("[kernel] PIC initialized\n");
    
    extern void tasking_init(void);
    tasking_init();
    
    asm volatile("sti");
    serial_write("[kernel] interrupts enabled!\n");

    extern void clock_monotonic_init(void);
    clock_monotonic_init();
    extern void firstos_platform_init(void);
    firstos_platform_init();
    extern void html5_engine_init(void);
    html5_engine_init();
    extern void heap_dump_diagnostics(void);
    heap_dump_diagnostics();
    extern int ring3_hello_task_spawn(void);
    extern int ring3_fault_task_spawn(void);
    extern int userspace_hello_spawn(void);

    // Preflight tasks superseded by filesystem-based ELF loader:
    // ring3_hello_task_spawn();
    // userspace_hello_spawn();
    // ring3_fault_task_spawn();

    if (multiboot_magic != 0x2BADB002) fail("BAD MULTIBOOT MAGIC");
    if (!mb) fail("NULL MULTIBOOT INFO");

    serial_write("[kernel] multiboot info present\n");
    int have_multiboot_fb = (mb->flags & (1u << 12)) &&
                            mb->framebuffer_type == 1 &&
                            mb->framebuffer_addr &&
                            mb->framebuffer_width >= 640 &&
                            mb->framebuffer_height >= 480 &&
                            mb->framebuffer_pitch &&
                            mb->framebuffer_bpp >= 15;
    u32 boot_width = 0;
    u32 boot_height = 0;

    if (have_multiboot_fb) {
        serial_write("[kernel] framebuffer data present\n");
        serial_write("[kernel] fb addr=");
        serial_write_hex((u32)mb->framebuffer_addr);
        serial_write(" pitch=");
        serial_write_hex(mb->framebuffer_pitch);
        serial_write(" width=");
        serial_write_hex(mb->framebuffer_width);
        serial_write(" height=");
        serial_write_hex(mb->framebuffer_height);
        serial_write(" bpp=");
        serial_write_hex(mb->framebuffer_bpp);
        serial_write("\n");

        if (mb->framebuffer_type == 0) {
            palette_addr = mb->color.palette.framebuffer_palette_addr;
            palette_size = mb->color.palette.framebuffer_palette_num_colors;
            serial_write("[kernel] indexed framebuffer palette detected\n");
        }

        boot_width = mb->framebuffer_width;
        boot_height = mb->framebuffer_height;

        serial_write("[kernel] calling framebuffer_init\n");
        compositor_init((int)boot_width, (int)boot_height);
        framebuffer_init(
            mb->framebuffer_addr,
            mb->framebuffer_width,
            mb->framebuffer_height,
            mb->framebuffer_pitch,
            mb->framebuffer_bpp,
            mb->framebuffer_type,
            mb->color.rgb.red_field_position,
            mb->color.rgb.red_mask_size,
            mb->color.rgb.green_field_position,
            mb->color.rgb.green_mask_size,
            mb->color.rgb.blue_field_position,
            mb->color.rgb.blue_mask_size,
            palette_addr,
            palette_size
        );

        serial_write("[kernel] framebuffer_init returned\n");
        if (!fb.ready) fail("FRAMEBUFFER INIT FAILED");

        serial_write("[kernel] framebuffer ready\n");
    } else {
        serial_write("[kernel] no multiboot framebuffer, using virtio-gpu path\n");
        boot_width = 1024;
        boot_height = 768;
        compositor_init((int)boot_width, (int)boot_height);
        framebuffer_init_virtual(boot_width, boot_height);
        if (!fb.ready) fail("VIRTUAL FRAMEBUFFER INIT FAILED");
    }
    gpu_registry_init();
    gpu_generic_fb_register();
    virtio_gpu_driver_register();
    pci_init();
    gpu_detect_scan();
    net_init();    /* initializes virtio-net NIC, runs DHCP */
    hda_init();
    /* Phase 3: Initialize active GPU via unified runtime (with capabilities snapshot & policy) */
    gpu_init_active(boot_width, boot_height);
    if (!have_multiboot_fb) {
        u32 act_w = gpu_get_active_width();
        u32 act_h = gpu_get_active_height();
        if (act_w > 0 && act_h > 0 && (act_w != boot_width || act_h != boot_height)) {
            boot_width = act_w;
            boot_height = act_h;
            gpu_set_active_resolution(boot_width, boot_height, 32);
        }
    }
    pci_debug_dump();
    serial_write("[kernel] before virtio_blk_init\n");
    virtio_blk_init();
    serial_write("[kernel] after virtio_blk_init\n");
    int mounted_from_disk = 0;
    if (virtio_blk_ready()) {
        static u8 s_boot_sec[512] __attribute__((aligned(16)));
        if (virtio_blk_read(0, s_boot_sec, 1)) {
            u32 magic = *(u32*)s_boot_sec;
            u32 ver = *(u32*)(s_boot_sec + 4);
            u32 img_size = *(u32*)(s_boot_sec + 8);
            u32 file_count = *(u32*)(s_boot_sec + 12);
            if (magic == 0x314F5346 && ver == 2 && img_size > 0 && img_size <= 256 * 1024 * 1024) {
                serial_write("[kernel] mounting fs from virtio-blk disk size=");
                serial_write_hex(img_size);
                serial_write(" files=");
                serial_write_hex(file_count);
                serial_write("\n");
                void *fs_dest = (void*)0;
                if ((mb->flags & (1u << 3)) && mb->mods_count && mb->mods_addr) {
                    boot_modules = (multiboot_module_t*)(unsigned long)mb->mods_addr;
                    boot_module_count = mb->mods_count;
                    u32 mod_size = boot_modules[0].mod_end - boot_modules[0].mod_start;
                    if (mod_size >= img_size) {
                        fs_dest = (void*)(unsigned long)boot_modules[0].mod_start;
                    }
                }
                if (!fs_dest) {
                    fs_dest = kmalloc(img_size);
                }
                if (fs_dest) {
                    u32 total_sec = (img_size + 511u) / 512u;
                    u32 chunk = 256;
                    int read_ok = 1;
                    for (u32 s = 0; s < total_sec; s += chunk) {
                        u32 rem = total_sec - s;
                        u32 cnt = (rem < chunk) ? rem : chunk;
                        if (!virtio_blk_read(s, (u8*)fs_dest + s * 512u, cnt)) {
                            read_ok = 0;
                            break;
                        }
                    }
                    if (read_ok) {
                        fs_init(fs_dest, img_size);
                        mounted_from_disk = 1;
                        serial_write("[kernel] virtio-blk fs mounted successfully\n");
                    }
                }
            }
        }
    }

    if (!mounted_from_disk) {
        if ((mb->flags & (1u << 3)) && mb->mods_count && mb->mods_addr) {
            boot_modules = (multiboot_module_t*)(unsigned long)mb->mods_addr;
            boot_module_count = mb->mods_count;
            multiboot_module_t *mods = boot_modules;
            u32 mod_size = mods[0].mod_end - mods[0].mod_start;
            serial_write("[kernel] module image size=");
            serial_write_hex(mod_size);
            serial_write("\n");
            fs_init((void*)(unsigned long)mods[0].mod_start, mod_size);
            serial_write("[kernel] after fs_init\n");
            
            if (!fs_ready()) {
                serial_write("[kernel] formatting fs module\n");
                fs_format((void*)(unsigned long)mods[0].mod_start, mod_size);
            }
        }
    }

    if (fs_ready()) {
        extern void vfs_init(void);
        vfs_init();
        extern void net_rescan_wifi(void);
        net_rescan_wifi();

#if 0
        extern int process_spawn(int parent_pid, const char *path, const char *const *argv);
        extern int process_waitpid(int caller_pid, int target_pid, int *out_status, int options);
        extern int userspace_hello_spawn(void);
        const char *fs_args[] = {
            "/userspace_hello.elf",
            "fs_mode_arg1",
            "fs_mode_arg2",
            0
        };
        serial_write("[kernel] spawning Ring 3 ELF directly from filesystem...\n");
        int fs_tid = process_spawn(0, "/userspace_hello.elf", fs_args);
        if (fs_tid < 0) {
            fs_tid = userspace_hello_spawn();
        }
        if (fs_tid >= 0) {
            serial_write("[kernel] waiting for FS-loaded userspace task to complete...\n");
            int st = 0;
            process_waitpid(0, fs_tid, &st, 0);
            serial_write("[kernel] resumed from FS-loaded userspace task successfully!\n");
        }
#endif
        extern void app_studio_init(void);
        app_studio_init();
    }
    serial_write("[kernel] calling gui_init\n");
    serial_write("[kernel] before gui_init\n");
    gui_init();
    serial_write("[kernel] after gui_init\n");
    
    extern int elf_load_and_run(const char *path);
    serial_write("[kernel] loading /hello.elf\n");
    // elf_load_and_run("/hello.elf");
    serial_write("[kernel] finished running /hello.elf\n");
    serial_write("[kernel] before ps2_init\n");
    ps2_init();
    serial_write("[kernel] after ps2_init\n");
    serial_write("[kernel] calling gui_draw\n");
    serial_write("[kernel] before gui_draw\n");
    gui_draw();
            serial_write("[kernel] frame end\n");
    serial_write("[kernel] after gui_draw\n");
    fs_set_auto_sync(1);
    serial_write("[kernel] gui_draw done\n");
    while (1) {
        hda_tick();
        ps2_poll();
        extern void eth_poll(void);
        eth_poll();
        serial_poll_commands();
        if (gui_power_off_requested()) {
            serial_write("[kernel] shutdown requested\n");
            platform_poweroff();
        }
        
        if (gui_needs_redraw()) {
            gui_is_idle = 0;
            gfx_telemetry_render_begin();
            gui_draw();
            gfx_telemetry_render_end();
        } else {
            extern int ps2_has_input(void);
            if (ps2_has_input()) {
                continue;
            }
            if (fs_ready() && fs_is_dirty() && virtio_blk_ready()) {
                fs_sync();
            }
            gui_is_idle = 1;
            __asm__ volatile("hlt" ::: "memory");
            gfx_telemetry_idle_wakeup();
            gui_is_idle = 0;
        }
    }
}

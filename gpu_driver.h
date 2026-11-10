#ifndef GPU_DRIVER_H
#define GPU_DRIVER_H

#include "io.h"
#include "pci.h"

/* Driver priority rankings */
#define GPU_PRIORITY_DEDICATED       100
#define GPU_PRIORITY_VENDOR_GENERIC   50
#define GPU_PRIORITY_CLASS_GENERIC    25
#define GPU_PRIORITY_FALLBACK          1

/* Match criteria wildcards and flags */
#define GPU_MATCH_ANY                0xFFFF
#define GPU_MATCH_FLAG_VENDOR        (1 << 0)
#define GPU_MATCH_FLAG_DEVICE        (1 << 1)
#define GPU_MATCH_FLAG_CLASS         (1 << 2)
#define GPU_MATCH_FLAG_SUBCLASS      (1 << 3)

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u8  class_code;
    u8  subclass;
    u8  prog_if;
    u32 match_flags;
    u32 driver_data;
} gpu_match_entry_t;

typedef struct gpu_caps {
    int has_hardware_cursor;
    int has_hardware_blt;
    int has_vsync_irq;
    int has_partial_present;
    int has_async_present;
    int has_page_flip;
    int is_accelerated;
    /* Phase 6 Capabilities */
    int has_completion_irq;
    int has_error_irq;
    int has_device_reset;
} gpu_caps_t;

/* Phase 6 Scheduler Modes */
typedef enum {
    GPU_SCHED_RUNNING = 0,
    GPU_SCHED_DRAINING,
    GPU_SCHED_IRQ_ACTIVE,
    GPU_SCHED_POLLING,
    GPU_SCHED_RECOVERY
} gpu_scheduler_mode_t;

/* Phase 7 Device States */
typedef enum {
    GPU_DEVICE_READY = 0,
    GPU_DEVICE_SUBMITTING,
    GPU_DEVICE_LOST,
    GPU_DEVICE_RESETTING,
    GPU_DEVICE_REINITIALIZING,
    GPU_DEVICE_DEGRADED,
    GPU_DEVICE_FALLBACK,
    GPU_DEVICE_FAILED
} gpu_device_state_t;

/* Phase 7 Reset Reasons */
typedef enum {
    GPU_RESET_TIMEOUT = 0,
    GPU_RESET_QUEUE_ERROR,
    GPU_RESET_IRQ_FAILURE,
    GPU_RESET_DEVICE_LOSS,
    GPU_RESET_MANUAL_TEST
} gpu_reset_reason_t;

/* Forward declarations */
typedef struct active_gpu_device active_gpu_device_t;
typedef struct gpu_driver gpu_driver_t;

/* Phase 5 Buffer Abstractions */
typedef enum {
    GPU_BUFFER_FREE = 0,
    GPU_BUFFER_DRAWING,
    GPU_BUFFER_QUEUED,
    GPU_BUFFER_SCANOUT,
    GPU_BUFFER_COMPLETE,
    GPU_BUFFER_ERROR,
    GPU_BUFFER_QUARANTINED
} gpu_buffer_state_t;

typedef struct {
    u32 x;
    u32 y;
    u32 w;
    u32 h;
} gpu_present_region_t;

struct gpu_resource;

typedef struct gpu_buffer {
    gpu_buffer_state_t    state;
    struct gpu_resource  *resource;
    u64                  frame_id;
    u32                  generation;
    int                  device_owned;
    void                *cpu_addr;
    u64                  phys_addr;
    u32                  width;
    u32                  height;
    u32                  pitch;
    u32                  bpp;
    u32                  size;
    int                  mapped;
} gpu_buffer_t;

typedef struct {
    int enabled;
    int visible;
    int hardware;
    int x;
    int y;
    u32 width;
    u32 height;
    u32 hotspot_x;
    u32 hotspot_y;
} gpu_cursor_state_t;

typedef struct {
    int  (*probe)(const pci_device_t *pdev, const gpu_match_entry_t *match);
    int  (*init)(active_gpu_device_t *gpu, u32 width, u32 height);
    int  (*set_resolution)(active_gpu_device_t *gpu, u32 width, u32 height, u32 bpp);
    int  (*present)(active_gpu_device_t *gpu, const void *backbuffer, u32 width, u32 height, u32 pitch);
    int  (*present_rect)(active_gpu_device_t *gpu, const void *backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch);
    int  (*cursor_update)(active_gpu_device_t *gpu, int visible, u32 x, u32 y, const u32 *pixels);
    int  (*cursor_create)(active_gpu_device_t *gpu, const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y);
    int  (*cursor_move)(active_gpu_device_t *gpu, int x, int y);
    int  (*cursor_show)(active_gpu_device_t *gpu, int visible);
    void (*cursor_destroy)(active_gpu_device_t *gpu);
    /* Phase 5 driver ops */
    int  (*buffer_create)(active_gpu_device_t *gpu, gpu_buffer_t *buffer);
    void (*buffer_destroy)(active_gpu_device_t *gpu, gpu_buffer_t *buffer);
    int  (*buffer_map)(active_gpu_device_t *gpu, gpu_buffer_t *buffer);
    void (*buffer_unmap)(active_gpu_device_t *gpu, gpu_buffer_t *buffer);
    int  (*queue_present)(active_gpu_device_t *gpu, gpu_buffer_t *buffer, const gpu_present_region_t *region);
    int  (*poll_present_complete)(active_gpu_device_t *gpu, gpu_buffer_t *buffer);
    int  (*wait_present_complete)(active_gpu_device_t *gpu, gpu_buffer_t *buffer, u32 timeout_ticks);
    /* Phase 6 driver ops */
    int  (*setup_events)(active_gpu_device_t *gpu);
    void (*shutdown_events)(active_gpu_device_t *gpu);
    int  (*ack_events)(active_gpu_device_t *gpu);
    int  (*read_event)(active_gpu_device_t *gpu, void *out_event);
    int  (*mask_events)(active_gpu_device_t *gpu, int masked);
    int  (*get_caps)(active_gpu_device_t *gpu, gpu_caps_t *out_caps);
    void (*shutdown)(active_gpu_device_t *gpu);
    /* Phase 7 driver ops */
    int  (*device_health_check)(active_gpu_device_t *gpu);
    int  (*quiesce)(active_gpu_device_t *gpu);
    int  (*reset_device)(active_gpu_device_t *gpu, gpu_reset_reason_t reason);
    int  (*reinit_device)(active_gpu_device_t *gpu);
    int  (*recreate_resources)(active_gpu_device_t *gpu);
    int  (*resume_device)(active_gpu_device_t *gpu);
    void (*abort_pending)(active_gpu_device_t *gpu);
} gpu_driver_ops_t;

struct gpu_driver {
    const char               *name;
    int                       priority;
    const gpu_match_entry_t  *match_table;
    u32                       match_count;
    gpu_driver_ops_t          ops;
    struct gpu_driver        *next;
};

struct active_gpu_device {
    int                 is_active;
    int                 initialized;
    int                 is_accelerated;
    int                 is_fallback;
    const pci_device_t *pci_dev;
    const gpu_driver_t *driver;
    gpu_caps_t          caps;
    gpu_cursor_state_t  cursor_state;
    u32                 current_width;
    u32                 current_height;
    u32                 current_pitch;
    u32                 current_bpp;
    u8                 *linear_fb_base;
    void               *priv_data;
    u32                 frames_presented;
    u32                 partial_presents;
    u32                 full_presents;
    u32                 fallback_events;
    u32                 last_error_code;
    /* Phase 4 metrics */
    u32                 cursor_hw_moves;
    u32                 cursor_sw_moves;
    u32                 cursor_fallbacks;
    u32                 present_requests;
    u32                 presents_coalesced;
    u32                 presents_dropped_clean;
    u32                 present_retries;
    u32                 vsync_waits;
    u32                 scheduler_overruns;
    /* Phase 5: Buffer Lifecycle Management */
    gpu_buffer_t       *buffers;
    u32                 buffer_count;
    u32                 draw_buffer;
    u32                 scanout_buffer;
    u32                 queued_buffer;
    u64                 next_frame_id;
    /* Phase 5 Metrics */
    u32                 buffers_created;
    u32                 buffers_destroyed;
    u32                 buffer_starvation;
    u32                 buffer_reuses;
    u32                 frames_queued;
    u32                 frames_completed;
    u32                 completion_timeouts;
    u32                 illegal_state_transitions;
    u32                 dropped_inflight_frames;
    u32                 recovery_events;
    /* Phase 6: Scheduler and Event State */
    gpu_scheduler_mode_t scheduler_mode;
    u32                 irq_vector;
    int                 irq_enabled;
    u64                 last_submitted_frame_id;
    u64                 last_submitted_tick;
    /* Phase 6 Metrics */
    u32                 irq_events;
    u32                 completion_irq_events;
    u32                 error_irq_events;
    u32                 events_dropped;
    u32                 spurious_events;
    u32                 stale_frame_events;
    u32                 irq_ack_failures;
    u32                 watchdog_timeouts;
    u32                 polling_fallbacks;
    u32                 recovery_attempts;
    u32                 recovery_successes;
    u32                 recovery_failures;
    u64                 irq_to_recycle_ticks;
    /* Phase 7: State Machine, Recovery & Fault-Tolerance */
    gpu_device_state_t  device_state;
    u32                 device_generation;
    u32                 reset_in_progress;
    u64                 last_reset_tick;
    u32                 reset_count_window;
    /* Phase 7 Metrics */
    u32                 device_loss_events;
    u32                 reset_attempts;
    u32                 reset_successes;
    u32                 reset_failures;
    u32                 resources_recreated;
    u32                 frames_quarantined;
    u32                 recovery_fallbacks;
    u32                 recovery_storm_prevented;
    u32                 health_probe_failures;
    u32                 transport_resets;
    u32                 safe_no_present_entries;
    /* Phase 8: Resource Management & Memory Safety Metrics */
    u32                 resource_allocations;
    u32                 resource_frees;
    u32                 resource_destroy_deferred;
    u32                 dma_map_failures;
    u32                 dma_unmap_failures;
    u32                 address_validation_failures;
    u32                 alignment_failures;
    u32                 overflow_rejections;
    u32                 generation_mismatches;
    u32                 use_after_free_rejections;
    u32                 double_free_rejections;
    u32                 quarantine_resources;
    u64                 peak_gpu_memory;
};

/* Phase 3 & 4 Public Runtime Query & Mode Management APIs */
const gpu_caps_t*    gpu_get_caps(void);
int                  gpu_supports_partial_present(void);
int                  gpu_supports_hw_cursor(void);
int                  gpu_supports_hw_blt(void);
int                  gpu_set_active_resolution(u32 width, u32 height, u32 bpp);
u32                  gpu_get_frames_presented(void);
u32                  gpu_get_partial_presents(void);
u32                  gpu_get_full_presents(void);
u32                  gpu_get_fallback_events(void);

/* Phase 4: Cursor & Scheduler APIs */
int                  gpu_cursor_init(void);
int                  gpu_cursor_set_shape(const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y);
int                  gpu_cursor_move(int x, int y);
int                  gpu_cursor_set_visible(int visible);
const gpu_cursor_state_t* gpu_cursor_get_state(void);

void                 gpu_present_scheduler_init(void);
void                 gpu_schedule_present(void);
int                  gpu_flush_if_needed(void);
int                  gpu_set_vsync(int enabled);
int                  gpu_set_max_fps(u32 fps);

u32                  gpu_get_present_requests(void);
u32                  gpu_get_coalesced_requests(void);
u32                  gpu_get_cursor_fallbacks(void);
u32                  gpu_get_vsync_waits(void);

/* Phase 5: Buffer Lifecycle & Tearing-Safe APIs */
int                  gpu_buffers_init(u32 width, u32 height, u32 bpp);
void                 gpu_buffers_shutdown(void);
gpu_buffer_t*        gpu_acquire_draw_buffer(void);
int                  gpu_queue_buffer_present(gpu_buffer_t *buffer, const gpu_present_region_t *region);
int                  gpu_poll_completions(void);
int                  gpu_wait_buffer_available(u32 timeout_ticks);
gpu_buffer_t*        gpu_get_draw_buffer(void);
gpu_buffer_t*        gpu_get_scanout_buffer(void);

u32                  gpu_get_buffer_starvation(void);
u32                  gpu_get_frames_queued(void);
u32                  gpu_get_frames_completed(void);
u32                  gpu_get_completion_timeouts(void);
u32                  gpu_get_recovery_events(void);

/* Phase 6: Event & IRQ APIs */
u32                  gpu_get_irq_events(void);
u32                  gpu_get_completion_irq_events(void);
u32                  gpu_get_events_dropped(void);
u32                  gpu_get_watchdog_timeouts(void);
u32                  gpu_get_recovery_attempts(void);
u32                  gpu_get_recovery_failures(void);
int                  gpu_recover_submission_path(active_gpu_device_t *gpu);

/* Phase 7: GPU Reset, Device Loss & Fault-Tolerance APIs */
gpu_device_state_t   gpu_get_device_state(void);
const char*          gpu_device_state_name(gpu_device_state_t state);
u32                  gpu_get_device_generation(void);
u32                  gpu_get_reset_attempts(void);
u32                  gpu_get_reset_successes(void);
u32                  gpu_get_reset_failures(void);
u32                  gpu_get_device_loss_events(void);
u32                  gpu_get_recovery_fallbacks(void);
u32                  gpu_get_frames_quarantined(void);
u32                  gpu_get_resources_recreated(void);
u32                  gpu_get_recovery_storm_prevented(void);
u32                  gpu_get_safe_no_present_entries(void);
int                  gpu_device_can_present(void);

#endif

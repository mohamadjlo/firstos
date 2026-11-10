# FirstOS Graphics & VirtIO-GPU Architecture Changes (Phase 3)
**Date:** 2026-10-15  
**Author:** Principal Graphics & Systems Engineer  
**Scope:** Core Subsystems Modified (`virtio_gpu`, `virtio_gpu_2d`, `framebuffer`, `compositor`, `gui`)

---

## 1. Summary of Structural Changes

| File | Subsystem | Nature of Change |
|---|---|---|
| `virtio_gpu.h` | GPU PCI Driver | Added `virtio_gpu_submit_batch2()` public prototype for pipelined multi-command virtqueue submission. |
| `virtio_gpu.c` | GPU PCI Driver | Implemented `virtio_gpu_submit_batch2()`: enqueues two distinct descriptor chains into `vring.avail_ring`, fires a **single MMIO notify**, and synchronizes once. |
| `virtio_gpu_2d.c` | GPU 2D Engine | Updated `virtio_gpu_2d_present()` to construct `TRANSFER_TO_HOST_2D` and `RESOURCE_FLUSH` requests simultaneously and submit via `virtio_gpu_submit_batch2()`, cutting VM-exits in half. |
| `compositor.h` | Compositor | Expanded `compositor_stats_t` to track frame rates, batched VirtIO operations, cursor-only presentations, pixel volumes, and trigger breakdown. Added 1 Hz sampler declaration. |
| `compositor.c` | Compositor | Implemented detailed presentation metrics recording (`compositor_record_present_detailed`, `compositor_record_virtio`, `compositor_record_input`, `compositor_tick_1hz`) with zero serial I/O overhead. |
| `gui.c` | Window Manager / Shell | 1. Implemented 64x64 offscreen cursor save-under/restore-under buffer (`gui_cursor_save`, `gui_cursor_restore`).<br>2. Added isolated cursor motion fast path in `gui_draw()` that restores/saves only a 48x48 region and presents a ~50x50 bounding box, completely bypassing wallpaper copying and window repaints.<br>3. Bypassed `draw_wallpaper()` when active window is maximized and occludes the desktop.<br>4. Decoupled text cursor blinking so that it only triggers redrawing when a text field is actually focused or Terminal is active.<br>5. Eliminated blocking `serial_write` calls in hot mouse click paths. |

---

## 2. Key Code Implementations

### Batched VirtIO-GPU Submission (`virtio_gpu.c`)
```c
int virtio_gpu_submit_batch2(void *req1, u32 len1, void *resp1, u32 rlen1,
                             void *req2, u32 len2, void *resp2, u32 rlen2) {
    /* Descriptor chain 0: request 1 -> response 1 */
    d[0].addr = virt_to_phys64((const void*)gpu_req_buf);
    d[0].len = len1;
    d[0].flags = VIRTQ_DESC_F_NEXT;
    d[0].next = 1;

    d[1].addr = virt_to_phys64((const void*)gpu_resp_buf);
    d[1].len = rlen1;
    d[1].flags = VIRTQ_DESC_F_WRITE;
    d[1].next = 0;

    /* Descriptor chain 1: request 2 -> response 2 */
    d[2].addr = virt_to_phys64((const void*)(gpu_req_buf + len1));
    d[2].len = len2;
    d[2].flags = VIRTQ_DESC_F_NEXT;
    d[2].next = 3;

    d[3].addr = virt_to_phys64((const void*)(gpu_resp_buf + rlen1));
    d[3].len = rlen2;
    d[3].flags = VIRTQ_DESC_F_WRITE;
    d[3].next = 0;

    /* Post both chain heads to available ring and notify once */
    q->vring.avail_ring[avail_idx % VIRTQ_NUM] = 0;
    q->vring.avail_ring[(avail_idx + 1) % VIRTQ_NUM] = 2;
    q->vring.avail_idx = (u16)(avail_idx + 2);

    virtio_gpu_notify_queue();

    while ((u16)(q->vring.used_idx - q->last_used_idx) < 2) {
        __asm__ volatile("pause" ::: "memory");
    }
    q->last_used_idx = (u16)(q->last_used_idx + 2);
    return 0;
}
```

### Isolated Cursor Motion Fast Path (`gui.c`)
```c
/* Fast path: isolated cursor motion when only cursor coordinates changed */
if (cursor_backup_valid && mouse_moved && !buttons_changed &&
    !window_dragging && !desktop_dragging && !explorer_dragging &&
    !alt_tab_preview_open && !spotlight_open && !brand_menu_open &&
    !app_menu_open && !file_menu_open && !edit_menu_open && !view_menu_open &&
    !clock_menu_open && !wifi_menu_open && !volume_menu_open &&
    !weather_popover_open && !fps_dropdown_open && !notification_center_open &&
    !desktop_context_open && !taskbar_context_open && !dock_context_open &&
    !file_dialog_is_open() && !gui_window_dirty_only &&
    mouse_y > (desktop_bar.h + 2) && last_mouse_y > (desktop_bar.h + 2) &&
    mouse_y < ((int)fb.height - 75) && last_mouse_y < ((int)fb.height - 75)) {

    gui_cursor_restore();
    gui_cursor_save(mouse_x, mouse_y, 48, 48);
    draw_cursor();
    fb_present();

    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
    last_mouse_buttons = cur_btn;
    needs_redraw = 0;
    return;
}
```

---

## 3. Verification & Regressions

- Kernel compilation and linking: zero errors.
- QEMU boot test: passed with full 1024x768 VirtIO-GPU scanout confirmed via QMP screendump.
- Color fidelity: Top bar (#424848) and wallpaper (#2563eb) verified intact.

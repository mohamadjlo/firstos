# FirstOS GUI & VirtIO-GPU Dimension Consistency Report (`gui_dimension_consistency.md`)

**Date:** 2026-10-16  
**Status:** **CONSISTENT (No Dimension, Stride, or Boundary Mismatch)**  
**Analyzed Files:**
- [`framebuffer.h`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.h) / [`framebuffer.c`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.c)
- [`virtio_gpu.h`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu.h) / [`virtio_gpu.c`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu.c)
- [`virtio_gpu_2d.h`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.h) / [`virtio_gpu_2d.c`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.c)
- [`display_server.c`](file:///Users/mohamadjlo/Desktop/FirstOS/display_server.c)
- [`compositor.c`](file:///Users/mohamadjlo/Desktop/FirstOS/compositor.c)
- [`gui.c`](file:///Users/mohamadjlo/Desktop/FirstOS/gui.c)

---

## 1. Primary Dimension Matrix

| Subsystem / Layer | Width | Height | Stride (Pitch) | Bytes Per Pixel | Buffer Base Pointer | Bounds Check Status |
|---|---|---|---|---|---|---|
| **Multiboot/GRUB Framebuffer** | `1024` | `768` | `4096` | `4` (32 bpp) | `mb->framebuffer_addr` | **Verified** |
| **Virtual Framebuffer (`fb`)** | `1024` | `768` | `4096` | `4` (32 bpp) | `framebuffer_backing` (32 MiB) | **Verified** |
| **VirtIO-GPU 2D Context (`gpu2d`)** | `1024` | `768` | `4096` | `4` (Format B8G8R8X8) | `fb.backbuffer` | **Verified** |
| **VirtIO 2D Resource (ID=1)** | `1024` | `768` | `4096` | `4` (`bytes = 3,145,728`) | Backing attached via `mem_entry` | **Verified** |
| **VirtIO Scanout (ID=0)** | `1024` | `768` | `4096` | `4` | Mapped to Resource ID 1 | **Verified** |
| **Compositor Bounds** | `1024` | `768` | N/A | N/A | Surfaces clamped to `(0,0,1024,768)` | **Verified** |
| **Display Server Surface** | `944` | `620` | `3776` | `4` (32 bpp) | `kmalloc_aligned(2,341,120)` | **Verified** (embedded window) |
| **Hardware Cursor Resource (ID=2)** | `64` | `64` | `256` | `4` (`16,384` bytes) | `cursor_pixels` (4096-aligned) | **Hardware cursor disabled (`0`)** |

---

## 2. Invariant Verification

### 2.1 Invariant: `fb dimensions == resource dimensions == scanout dimensions`
- In `framebuffer.c`:
  `fb.width = 1024`, `fb.height = 768`.
- In `virtio_gpu_2d_init()` ([`virtio_gpu_2d.c:150-162`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.c#L150-L162)):
  - `g->width = w = 1024`, `g->height = h = 768`.
  - `cmd_create(g)` issues `VIRTIO_GPU_CMD_RESOURCE_CREATE_2D` with `width = 1024, height = 768`.
  - `cmd_scanout(g)` issues `VIRTIO_GPU_CMD_SET_SCANOUT` with `q.r.width = 1024, q.r.height = 768, q.scanout_id = 0, q.resource_id = 1`.
- **Verdict:** **MATCH (1024x768 across all layers).**

### 2.2 Invariant: `stride == width * bytes_per_pixel`
- `bytes_per_pixel = (bpp + 7) / 8 = 4`.
- `width * 4 = 1024 * 4 = 4096`.
- `fb.pitch = 4096`.
- `g->stride = 4096`.
- **Verdict:** **MATCH (Exactly 4096 bytes per scanline).**

### 2.3 Invariant: `transfer rectangles` inside resource bounds
- In `virtio_gpu_2d_present()` ([`virtio_gpu_2d.c:200-203`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.c#L200-L203)):
  ```c
  if (x >= g->width || y >= g->height) return -1;
  if (x + w > g->width) w = g->width - x;
  if (y + h > g->height) h = g->height - y;
  ```
- In `cmd_transfer()` ([`virtio_gpu_2d.c:83-85`](file:///Users/mohamadjlo/Desktop/FirstOS/virtio_gpu_2d.c#L83-L85)):
  ```c
  if (x >= g->width || y >= g->height) return -1;
  if (x + w > g->width) w = g->width - x;
  if (y + h > g->height) h = g->height - y;
  ```
- **Verdict:** **SAFE (All transfer rectangles are clamped strictly to `[0, width]` and `[0, height]`).**

### 2.4 Invariant: `damage rectangles` inside framebuffer bounds
- In `fb_mark_dirty_rect()` ([`framebuffer.c:88-92`](file:///Users/mohamadjlo/Desktop/FirstOS/framebuffer.c#L88-L92)):
  ```c
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (int)fb.width) x1 = (int)fb.width;
  if (y1 > (int)fb.height) y1 = (int)fb.height;
  if (x0 >= x1 || y0 >= y1) return;
  ```
- **Verdict:** **SAFE (Damage rectangles cannot exceed framebuffer extent).**

### 2.5 Invariant: No integer overflow in `x/y/width/height` calculations
- Row offset formula: `offset = (u64)(y * g->stride + x * 4)`.
- For `y = 768, stride = 4096, x = 1024`:
  $\text{offset} = 768 \times 4096 + 1024 \times 4 = 3,149,824\text{ bytes} \approx 3.003\text{ MiB}$.
- This fits easily within standard 32-bit registers ($2^{31}-1 = 2,147,483,647$) and 64-bit offsets.
- **Verdict:** **SAFE (Zero risk of integer overflow).**

---

## 3. Conclusion
There is **no mismatch in dimensions, pitch, stride, or resource limits** between FirstOS, the framebuffer driver, VirtIO-GPU, or the compositor. The visual anomalies seen in `gui.mov` are **not caused by dimension or stride misalignment**.

#ifndef GPU_IRQ_H
#define GPU_IRQ_H

#include "gpu_event.h"
#include "gpu_driver.h"

int  gpu_irq_init(void);
int  gpu_setup_completion_path(void);
void gpu_irq_shutdown(void);
void gpu_irq_mask(int masked);
void gpu_irq_handler(void);
int  gpu_irq_is_enabled(void);
void gpu_irq_simulate_event(u64 frame_id, u32 buffer_index, int status);

#endif

#ifndef GPU_RECOVERY_H
#define GPU_RECOVERY_H

#include "gpu_driver.h"
#include "gpu_reset.h"

void gpu_recovery_init(void);
int  gpu_recovery_trigger(gpu_reset_reason_t reason);
int  gpu_recovery_process(void);
int  gpu_recovery_is_in_progress(void);
void gpu_recovery_abort_pending(void);
void gpu_recovery_simulate_fail(int fail_attempt_1, int fail_attempt_2);

#endif

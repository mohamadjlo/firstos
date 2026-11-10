#ifndef CLOCK_H
#define CLOCK_H

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the high-resolution monotonic timer subsystem and calibrate TSC */
void clock_monotonic_init(void);

/* Get monotonic time in nanoseconds since boot */
u64  clock_monotonic_get_ns(void);

/* Get monotonic time in microseconds since boot */
u64  clock_monotonic_get_us(void);

/* Get monotonic time in milliseconds since boot */
u32  clock_monotonic_get_ms(void);

/* Get raw system timer tick count (100 Hz = 10ms per tick) */
u32  clock_monotonic_get_ticks(void);

/* Busy-wait / spin sleep for specified milliseconds with bounded spin limit */
void clock_monotonic_sleep_ms(u32 ms);
void clock_monotonic_sleep_ms_bounded(u32 ms, u32 max_spins);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_H */

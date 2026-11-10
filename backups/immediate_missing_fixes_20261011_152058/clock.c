#include "clock.h"
#include "serial.h"

#ifdef __cplusplus
extern "C" {
#endif

/* System ticks from task.c (100 Hz = 10ms per tick) */
extern volatile unsigned int system_ticks;

static u64 s_tsc_at_boot = 0;
static u32 s_tsc_khz = 0; /* TSC cycles per millisecond */
static int s_clock_ready = 0;
static u64 s_last_monotonic_ns = 0;

static inline u64 rdtsc(void) {
#if defined(__i386__) || defined(__x86_64__)
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
#elif defined(__APPLE__) && defined(__MACH__)
    extern unsigned long long mach_absolute_time(void);
    return (u64)mach_absolute_time();
#else
    return (u64)system_ticks * 2000000ULL;
#endif
}

#if defined(__i386__) || defined(__x86_64__)
#define CPU_PAUSE() __asm__ volatile("pause" ::: "memory")
#elif defined(__aarch64__) || defined(__arm64__)
#define CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
#else
#define CPU_PAUSE() ((void)0)
#endif

static inline u64 div64_32(u64 num, u32 den, u32 *rem) {
#if defined(__i386__)
    u32 high = (u32)(num >> 32);
    u32 low  = (u32)num;
    u32 q_high = 0, q_low = 0, r = 0;
    if (high >= den) {
        q_high = high / den;
        high = high % den;
    }
    __asm__ volatile (
        "divl %4"
        : "=a"(q_low), "=d"(r)
        : "d"(high), "a"(low), "r"(den)
    );
    if (rem) *rem = r;
    return ((u64)q_high << 32) | (u64)q_low;
#else
    if (rem) *rem = (u32)(num % den);
    return num / den;
#endif
}

void clock_monotonic_init(void) {
    s_tsc_at_boot = rdtsc();

    /* Calibrate TSC against PIT/system_ticks over 2 ticks (approx 20ms) */
    u32 start_tick = system_ticks;
    int spins = 0;
    while (system_ticks == start_tick && spins < 500000) {
        CPU_PAUSE();
        spins++;
    }

    u32 cal_start_tick = system_ticks;
    u64 tsc_start = rdtsc();

    /* Wait for 2 ticks to elapse */
    spins = 0;
    while (system_ticks - cal_start_tick < 2 && spins < 1000000) {
        CPU_PAUSE();
        spins++;
    }

    u64 tsc_end = rdtsc();
    u32 elapsed_ticks = system_ticks - cal_start_tick;
    u64 elapsed_cycles = tsc_end - tsc_start;

    if (elapsed_ticks > 0 && elapsed_cycles > 0) {
        /* elapsed_ticks * 10 ms = elapsed milliseconds */
        u32 elapsed_ms = elapsed_ticks * 10;
        s_tsc_khz = (u32)div64_32(elapsed_cycles, elapsed_ms, (void*)0);
    } else {
        /* Fallback: assume typical 2.0 GHz virtual CPU if calibration failed or standalone */
        s_tsc_khz = 2000000;
    }

    s_clock_ready = 1;
    s_last_monotonic_ns = 0;

    serial_write("[clock] Monotonic timer initialized. TSC kHz=");
    serial_write_hex(s_tsc_khz);
    serial_write("\n");
}

u32 clock_monotonic_get_ticks(void) {
    return system_ticks;
}

u64 clock_monotonic_get_ns(void) {
    u64 ns = 0;

    if (s_clock_ready && s_tsc_khz > 0) {
        u64 current_tsc = rdtsc();
        u64 cycles = (current_tsc > s_tsc_at_boot) ? (current_tsc - s_tsc_at_boot) : 0;
        u32 rem_cycles = 0;
        u64 us = div64_32(cycles, s_tsc_khz, &rem_cycles);
        ns = (us * 1000ULL) + div64_32((u64)rem_cycles * 1000ULL, s_tsc_khz, (void*)0);
    } else {
        /* Fallback: 100 Hz timer tick (10,000,000 ns per tick) */
        ns = (u64)system_ticks * 10000000ULL;
    }

    /* Strict monotonic guarantee: time must never flow backwards */
    if (ns < s_last_monotonic_ns) {
        ns = s_last_monotonic_ns;
    }
    s_last_monotonic_ns = ns;
    return ns;
}

u64 clock_monotonic_get_us(void) {
    return div64_32(clock_monotonic_get_ns(), 1000, (void*)0);
}

u32 clock_monotonic_get_ms(void) {
    return (u32)div64_32(clock_monotonic_get_ns(), 1000000, (void*)0);
}

void clock_monotonic_sleep_ms_bounded(u32 ms, u32 max_spins) {
    u32 start = clock_monotonic_get_ms();
    u32 spins = 0;
    while ((clock_monotonic_get_ms() - start < ms) && (spins < max_spins)) {
        CPU_PAUSE();
        spins++;
    }
}

void clock_monotonic_sleep_ms(u32 ms) {
    /* 500,000 spins per ms + 50,000 margin ensures bounded execution under any stalled tick state */
    u32 max_spins = ms * 500000u + 50000u;
    clock_monotonic_sleep_ms_bounded(ms, max_spins);
}

#ifdef __cplusplus
}
#endif

#include "heap.h"
#include "hda.h"
#include "pci.h"
#include "serial.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long long u64;

#define HDA_GCAP     0x00
#define HDA_GCTL     0x08
#define HDA_STATESTS 0x0E
#define HDA_INTCTL   0x20
/* Immediate Command registers (bypass CORB/RIRB) */
#define HDA_IC       0x60   /* Immediate Command */
#define HDA_IR       0x64   /* Immediate Result */
#define HDA_IRS      0x68   /* Immediate Command Status */

static u32 hda_base;

static u32 r32(u32 r) { return *(volatile u32*)(hda_base + r); }
static void w32(u32 r, u32 v) { *(volatile u32*)(hda_base + r) = v; }
static u16 r16(u32 r) { return *(volatile u16*)(hda_base + r); }
static void w8 (u32 r, u8  v) { *(volatile u8 *)(hda_base + r) = v; }

static u8* audio_buf = 0;
static int hda_stream_running = 0;
/* busy-spin */
static void hda_delay(u32 n) { volatile u32 i; for(i=0;i<n;i++); }

/* Send verb via Immediate Command interface - no CORB/RIRB needed */
static u32 hda_verb(u32 verb) {
    u32 irs;
    u32 i;
    /* Wait for IRS busy to clear */
    for (i = 0; i < 1000000; i++) {
        irs = r32(HDA_IRS);
        if (!(irs & 0x01)) break;
    }
    w32(HDA_IC, verb);
    /* Set ICB (bit 0) and clear IRV (bit 1) to start command */
    w32(HDA_IRS, 0x01);
    /* Wait for IRV to be set (response valid) */
    for (i = 0; i < 1000000; i++) {
        irs = r32(HDA_IRS);
        if (irs & 0x02) break;
        hda_delay(10);
    }
    return r32(HDA_IR);
}

int hda_init(void) {
    const pci_device_t* dev = pci_find_class(0x04, 0x03);
    u32 gcap, i;
    if (!dev) { serial_write("[hda] not found\n"); return -1; }
    pci_enable_bus_master(dev);
    hda_base = pci_read_bar32(dev, 0) & ~0xFu;
    if (!hda_base) { serial_write("[hda] bad BAR0\n"); return -1; }
    serial_write("[hda] BAR0="); serial_write_hex(hda_base); serial_write("\n");

    /* Reset controller */
    w32(HDA_GCTL, 0);
    for (i = 0; i < 1000000; i++) if (!(r32(HDA_GCTL) & 1)) break;
    w32(HDA_GCTL, 1);
    for (i = 0; i < 1000000; i++) if (r32(HDA_GCTL) & 1) break;
    hda_delay(100000); /* wait for codec enumeration */

    gcap = r32(HDA_GCAP);
    serial_write("[hda] GCAP="); serial_write_hex(gcap);
    serial_write(" STATESTS="); serial_write_hex(r16(HDA_STATESTS));
    serial_write("\n");

    /* Allocate audio buffer early while heap is fresh (6MB enough for most WAVs) */
    audio_buf = (u8*)kmalloc(6 * 1024 * 1024);
    if (!audio_buf) serial_write("[hda] warn: audio buffer alloc failed\n");
    else serial_write("[hda] audio buffer allocated\n");

    return 0;
}

/* ---- Audio playback ---- */
#include "fs.h"
#include "heap.h"
#include "memory.h"

typedef struct { u32 addr_lo, addr_hi, length, flags; } bdl_t;
static bdl_t  bdl[2]       __attribute__((aligned(128)));

void hda_set_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    u32 vol = (pct * 87) / 100; /* 0..87 for ICH HDA */
    /* Mute off, set gain: verb 0x3xx payload: bit7=1 (mute off), bits6:0=gain */
    /* Out amp on DAC node 2 (hda-output has DAC=2, pin=3 typically in QEMU) */
    /* B=1(output), L=1, R=1, gain bits */
    u32 amp = 0xB000 | vol;   /* Output, L+R, no mute */
    hda_verb((0 << 28) | (2 << 20) | (0x3 << 16) | amp); /* NID 2 */
    hda_verb((0 << 28) | (3 << 20) | (0x3 << 16) | amp); /* NID 3 */
}

static int hda_stream_paused = 0;
static const short *hda_audio_start_ptr = 0;
static u32 hda_audio_total_bytes = 0;
static u32 hda_last_lpib = 0;
static u32 hda_oss_base = 0;

void hda_stop_audio(void) {
    if (!hda_stream_running) return;
    u32 gcap = r32(HDA_GCAP);
    u32 iss = (gcap >> 8) & 0x0F;
    u32 oss_base = 0x80 + iss * 0x20;
    
    /* Stop stream */
    w32(oss_base + 0x00, r32(oss_base + 0x00) & ~0x02u);
    hda_stream_running = 0;
    hda_stream_paused = 0;
    serial_write("[hda] stream stopped\n");
}

void hda_pause_audio(void) {
    if (!hda_stream_running || hda_stream_paused) return;
    w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) & ~0x02u);
    hda_stream_paused = 1;
    serial_write("[hda] stream paused\n");
}

void hda_resume_audio(void) {
    if (!hda_stream_running || !hda_stream_paused) return;
    w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) | 0x02u);
    hda_stream_paused = 0;
    serial_write("[hda] stream resumed\n");
}

int hda_is_playing(void) {
    return hda_stream_running && !hda_stream_paused;
}

int hda_is_paused(void) {
    return hda_stream_running && hda_stream_paused;
}

u32 hda_get_current_offset(void) {
    if (!hda_stream_running) return 0;
    return r32(hda_oss_base + 0x04);
}

u32 hda_get_total_length(void) {
    return hda_audio_total_bytes;
}

int hda_get_pcm_samples(short *out_samples, int max_count) {
    if (!out_samples || max_count <= 0) return 0;
    if (!hda_stream_running || !hda_audio_start_ptr || hda_audio_total_bytes == 0) {
        for (int i = 0; i < max_count; i++) out_samples[i] = 0;
        return 0;
    }
    u32 lpib = r32(hda_oss_base + 0x04);
    u32 sample_offset = lpib / 2;
    u32 total_samples = hda_audio_total_bytes / 2;
    for (int i = 0; i < max_count; i++) {
        u32 idx = sample_offset + i;
        if (idx < total_samples) {
            out_samples[i] = hda_audio_start_ptr[idx];
        } else {
            out_samples[i] = 0;
        }
    }
    return max_count;
}

int hda_seek_byte_offset(u32 byte_offset) {
    if (!hda_stream_running || !audio_buf || hda_audio_total_bytes == 0) return -1;
    if (byte_offset >= hda_audio_total_bytes) byte_offset = (hda_audio_total_bytes > 128) ? (hda_audio_total_bytes - 128) : 0;
    byte_offset &= ~127u;
    
    /* Stop stream */
    w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) & ~0x02u);
    for (u32 i = 0; i < 100000; i++) if (!(r32(hda_oss_base) & 0x02)) break;
    w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) | 0x01u); /* SRST=1 */
    for (u32 i = 0; i < 100000; i++) if (r32(hda_oss_base) & 0x01) break;
    w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) & ~0x01u); /* SRST=0 */
    for (u32 i = 0; i < 100000; i++) if (!(r32(hda_oss_base) & 0x01)) break;
    
    u32 curr_phys = virt_to_phys((void*)(unsigned long)((u32)(unsigned long)hda_audio_start_ptr + byte_offset));
    u32 rem_len = hda_audio_total_bytes - byte_offset;
    
    bdl[0].addr_lo = curr_phys;
    bdl[0].addr_hi = 0;
    bdl[0].length  = rem_len;
    bdl[0].flags   = 1;
    
    w32(hda_oss_base + 0x18, virt_to_phys(bdl));
    w32(hda_oss_base + 0x1C, 0);
    w32(hda_oss_base + 0x08, rem_len);
    *(volatile u16*)(hda_base + hda_oss_base + 0x0C) = 0;
    *(volatile u16*)(hda_base + hda_oss_base + 0x12) = 0x4011;
    w32(hda_oss_base + 0x00, (r32(hda_oss_base + 0x00) & 0xFF0FFFFF) | (1u << 20));
    
    if (!hda_stream_paused) {
        w32(hda_oss_base + 0x00, r32(hda_oss_base + 0x00) | 0x02u);
    }
    hda_last_lpib = 0;
    return 0;
}

void hda_tick(void) {
    if (!hda_stream_running || hda_stream_paused) return;
    u32 lpib = r32(hda_oss_base + 0x04);
    if (lpib < hda_last_lpib) {
        /* LPIB wrapped around, which means the buffer finished playing once */
        hda_stop_audio();
        extern int media_player_is_playing;
        media_player_is_playing = 0;
        extern int needs_redraw;
        needs_redraw = 1;
    } else {
        hda_last_lpib = lpib;
    }
}

int hda_play_pcm(const short *samples, int sample_count, int hz, int channels) {
    u32 gcap, iss, oss_base;
    u32 start, aligned, skip, len, phys;
    u32 i;
    extern int system_volume;

    if (!audio_buf || !samples || sample_count <= 0) {
        serial_write("[hda] play_pcm: invalid args or no buf\n");
        return -1;
    }

    gcap     = r32(HDA_GCAP);
    iss      = (gcap >> 8) & 0x0F;          /* input stream count */
    oss_base = 0x80 + iss * 0x20;           /* first output stream descriptor */

    /* --- Stop & reset stream --- */
    w32(oss_base + 0x00, r32(oss_base + 0x00) & ~0x02u); /* clear RUN */
    for (i = 0; i < 100000; i++) if (!(r32(oss_base) & 0x02)) break;
    w32(oss_base + 0x00, r32(oss_base + 0x00) | 0x01u);  /* SRST=1 */
    for (i = 0; i < 100000; i++) if (r32(oss_base) & 0x01) break;
    w32(oss_base + 0x00, r32(oss_base + 0x00) & ~0x01u); /* SRST=0 */
    for (i = 0; i < 100000; i++) if (!(r32(oss_base) & 0x01)) break;

    short *dst = (short*)audio_buf;
    u32 total_bytes;
    if (channels == 1) {
        /* Mono -> duplicate into stereo */
        total_bytes = (u32)sample_count * 2 * sizeof(short);
        if (total_bytes > 6 * 1024 * 1024) total_bytes = 6 * 1024 * 1024;
        int frames = total_bytes / 4;
        for (int j = 0; j < frames; j++) {
            dst[2 * j]     = samples[j];
            dst[2 * j + 1] = samples[j];
        }
    } else {
        /* Stereo */
        total_bytes = (u32)sample_count * sizeof(short);
        if (total_bytes > 6 * 1024 * 1024) total_bytes = 6 * 1024 * 1024;
        if (dst != samples) {
            for (u32 j = 0; j < total_bytes / 2; j++) dst[j] = samples[j];
        }
    }

    start   = (u32)(unsigned long)dst;
    aligned = (start + 127) & ~127u;
    skip    = aligned - start;
    len     = (total_bytes > skip) ? (total_bytes - skip) : 0;
    len    &= ~127u;  /* multiple of 128 */
    phys    = virt_to_phys((void*)(unsigned long)aligned);

    bdl[0].addr_lo = phys;
    bdl[0].addr_hi = 0;
    bdl[0].length  = len;
    bdl[0].flags   = 1; /* IOC=1 */

    u16 fmt = (hz == 48000) ? 0x0011 : 0x4011;

    w32(oss_base + 0x18, virt_to_phys(bdl)); /* BDLPL */
    w32(oss_base + 0x1C, 0);                  /* BDLPU */
    w32(oss_base + 0x08, len);                /* CBL  */
    *(volatile u16*)(hda_base + oss_base + 0x0C) = 0;    /* LVI=0 */
    *(volatile u16*)(hda_base + oss_base + 0x12) = fmt;

    /* Set stream tag=1 in SD_CTL bits [23:20] */
    w32(oss_base + 0x00, (r32(oss_base + 0x00) & 0xFF0FFFFF) | (1u << 20));

    /* --- Configure codec via Immediate Command --- */
    hda_verb(0x00170500); /* NID1 SetPowerState D0  */
    hda_verb(0x00270500); /* NID2 SetPowerState D0  */
    hda_verb(0x00370500); /* NID3 SetPowerState D0  */

    hda_verb(0x00220000 | fmt); /* NID2 SetStreamFormat */
    hda_verb(0x00270610);        /* NID2 SetStreamChannel (stream=1, chan=0) */

    /* Amplifier: output, L+R, no mute, gain */
    u32 vol = ((system_volume * 87) / 100);
    hda_verb(0x00200000 | 0xB000 | vol); /* NID2 out amp  */
    hda_verb(0x00300000 | 0xB000 | vol); /* NID3 out amp  */

    /* Pin widget: HP+OUT enabled */
    hda_verb(0x00370C40); /* NID3 PinWidgetCtrl OUT */
    hda_verb(0x00370740); /* NID3 EAPD enable       */

    hda_delay(50000);

    /* --- Start stream --- */
    hda_stream_paused = 0;
    hda_audio_start_ptr = (const short*)(unsigned long)aligned;
    hda_audio_total_bytes = len;
    w32(oss_base + 0x00, r32(oss_base + 0x00) | 0x02u); /* RUN=1 */
    hda_stream_running = 1;
    hda_oss_base = oss_base;
    hda_last_lpib = 0;
    serial_write("[hda] stream started len=");
    serial_write_hex(len);
    serial_write("\n");
    return 0;
}

void hda_wait_completion(int max_ms) {
    int waited = 0;
    while (hda_is_playing() && waited < max_ms) {
        hda_tick();
        for (volatile int d = 0; d < 200000; d++);
        waited += 10;
    }
}

int hda_play_audio(const char* path) {
    if (!path || !path[0]) return -1;
    char norm_path[256];
    int p = 0;
    while (path[p] && p < 255) {
        norm_path[p] = (path[p] == '\\') ? '/' : path[p];
        p++;
    }
    norm_path[p] = 0;

    if (!audio_buf) { serial_write("[hda] no audio buffer\n"); return -1; }

    int n = fs_read_at(norm_path, (char*)audio_buf, 6 * 1024 * 1024);
    serial_write("[hda] fs_read="); serial_write_hex(n);
    serial_write(" path="); serial_write(norm_path); serial_write("\n");
    if (n <= 0) return -1;

    /* Check if WAV */
    if (n > 44 && audio_buf[0] == 'R' && audio_buf[1] == 'I' && audio_buf[2] == 'F' && audio_buf[3] == 'F') {
        int is_startup = 1;
        const char *s1 = norm_path; const char *s2 = "/audio/start_up.wav";
        while (*s1 && *s2) { if (*s1++ != *s2++) { is_startup = 0; break; } }
        if (*s1 != *s2) is_startup = 0;
        if (is_startup && n > 882044) n = 882044;

        int channels = *(u16*)(audio_buf + 22);
        int hz = *(u32*)(audio_buf + 24);
        if (channels <= 0) channels = 2;
        if (hz <= 0) hz = 44100;
        return hda_play_pcm((const short*)(audio_buf + 44), (n - 44) / 2, hz, channels);
    }

    /* Otherwise treat as MP3 */
    extern int decode_mp3_to_pcm(const u8 *mp3_data, int mp3_len, short *out_pcm, int max_shorts, int *out_hz, int *out_channels);
    short *pcm_temp = (short*)kmalloc(4 * 1024 * 1024);
    if (!pcm_temp) return -1;
    int out_hz = 44100, out_ch = 2;
    int samples = decode_mp3_to_pcm(audio_buf, n, pcm_temp, (4 * 1024 * 1024) / 2, &out_hz, &out_ch);
    if (samples <= 0) {
        kfree(pcm_temp);
        serial_write("[hda] mp3 decode failed\n");
        return -1;
    }
    int res = hda_play_pcm(pcm_temp, samples * out_ch, out_hz, out_ch);
    kfree(pcm_temp);
    return res;
}


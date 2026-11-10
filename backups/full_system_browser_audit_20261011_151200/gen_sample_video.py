#!/usr/bin/env python3
import io
import math
import os
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH = 320
HEIGHT = 240
FPS = 24
NUM_FRAMES = 48  # 2.0-second loop

def create_frame(frame_idx: int) -> bytes:
    img = Image.new("RGB", (WIDTH, HEIGHT), (12, 18, 30))
    draw = ImageDraw.Draw(img)

    t = frame_idx / NUM_FRAMES
    angle = t * 2 * math.pi

    # Draw gradient background stripes
    for y in range(HEIGHT):
        fac = y / HEIGHT
        r = int(12 + fac * 25)
        g = int(22 + fac * 35)
        b = int(45 + fac * 60)
        draw.line([(0, y), (WIDTH, y)], fill=(r, g, b))

    # Grid lines
    for gx in range(0, WIDTH, 32):
        draw.line([(gx, 0), (gx, HEIGHT)], fill=(25, 40, 65))
    for gy in range(0, HEIGHT, 24):
        draw.line([(0, gy), (WIDTH, gy)], fill=(25, 40, 65))

    # Rotating 3D-like wireframe / orbiting orbs
    cx = WIDTH // 2
    cy = HEIGHT // 2 - 10

    # Orbiting glowing particles
    num_particles = 8
    for i in range(num_particles):
        p_angle = angle + (i * 2 * math.pi / num_particles)
        rad_x = 75 * math.cos(p_angle)
        rad_y = 35 * math.sin(p_angle)
        px = int(cx + rad_x)
        py = int(cy + rad_y)
        p_depth = (math.sin(p_angle) + 1.0) / 2.0  # 0 to 1
        p_size = int(5 + 5 * p_depth)
        r_c = int(80 + 175 * p_depth)
        g_c = int(140 + 115 * (1 - p_depth))
        b_c = 255
        draw.ellipse([px - p_size, py - p_size, px + p_size, py + p_size], fill=(r_c, g_c, b_c))

    # Central pulsing core
    core_rad = int(26 + 6 * math.sin(angle * 2))
    draw.ellipse([cx - core_rad - 6, cy - core_rad - 6, cx + core_rad + 6, cy + core_rad + 6], outline=(70, 130, 210), width=2)
    draw.ellipse([cx - core_rad, cy - core_rad, cx + core_rad, cy + core_rad], fill=(45, 105, 195), outline=(130, 195, 255), width=2)

    # FirstOS logo symbol inside core
    draw.line([(cx - 10, cy - 8), (cx + 10, cy - 8)], fill=(255, 255, 255), width=3)
    draw.line([(cx, cy - 8), (cx, cy + 10)], fill=(255, 255, 255), width=3)
    draw.line([(cx - 6, cy + 3), (cx + 6, cy + 3)], fill=(200, 230, 255), width=2)

    # Title header banner
    draw.rectangle([(0, 0), (WIDTH, 24)], fill=(8, 14, 24))
    draw.line([(0, 24), (WIDTH, 24)], fill=(60, 95, 145))
    draw.text((10, 5), "FirstOS Media Studio", fill=(220, 235, 255))
    draw.text((WIDTH - 85, 5), f"{FPS} FPS MJPEG", fill=(140, 180, 225))

    # Animated progress bar at bottom
    bar_y = HEIGHT - 28
    draw.rectangle([(16, bar_y), (WIDTH - 16, bar_y + 12)], fill=(16, 26, 42), outline=(50, 80, 120))
    prog_w = int((WIDTH - 36) * ((frame_idx + 1) / NUM_FRAMES))
    if prog_w > 0:
        draw.rectangle([(18, bar_y + 2), (18 + prog_w, bar_y + 10)], fill=(0, 180, 255))

    # Frame counter string
    draw.text((20, HEIGHT - 14), f"Frame {frame_idx + 1:02d}/{NUM_FRAMES:02d}  |  Duration: 2.0s", fill=(160, 185, 210))

    out_bytes = io.BytesIO()
    img.save(out_bytes, format="JPEG", quality=82)
    return out_bytes.getvalue()

def build_avi(frames: list, out_path: Path):
    us_per_frame = int(1000000 / FPS)
    num_frames = len(frames)

    # 1. Build movi LIST
    movi_data = bytearray(b"movi")
    idx_entries = []

    for f_idx, f_bytes in enumerate(frames):
        chunk_offset = len(movi_data)  # offset within movi LIST content (relative to movi)
        f_size = len(f_bytes)
        # 00dc chunk
        movi_data += b"00dc" + struct.pack("<I", f_size) + f_bytes
        if f_size % 2 == 1:
            movi_data += b"\x00"  # pad to 2-byte boundary
        # idx1 entry: ckid, flags (0x10 = keyframe), offset (rel to movi content start), length
        idx_entries.append((b"00dc", 0x10, chunk_offset, f_size))

    # 2. Build avih chunk (56 bytes)
    avih_payload = struct.pack(
        "<IIIIIIIIIIIIII",
        us_per_frame,    # dwMicroSecPerFrame (offset 0)
        0,               # dwMaxBytesPerSec (offset 4)
        0,               # dwPaddingGranularity (offset 8)
        0x10,            # dwFlags (AVIF_HASINDEX) (offset 12)
        num_frames,      # dwTotalFrames (offset 16)
        0,               # dwInitialFrames (offset 20)
        1,               # dwStreams (offset 24)
        65536,           # dwSuggestedBufferSize (offset 28)
        WIDTH,           # dwWidth (offset 32)
        HEIGHT,          # dwHeight (offset 36)
        0, 0, 0, 0       # dwReserved (offset 40-55)
    )
    avih_chunk = b"avih" + struct.pack("<I", len(avih_payload)) + avih_payload

    # 3. Build strh chunk (56 bytes)
    strh_payload = struct.pack(
        "<4s4sIHHIIIIIIIIhhhh",
        b"vids",         # fccType
        b"MJPG",         # fccHandler
        0,               # dwFlags
        0,               # wPriority
        0,               # wLanguage
        0,               # dwInitialFrames
        1,               # dwScale
        FPS,             # dwRate
        0,               # dwStart
        num_frames,      # dwLength
        65536,           # dwSuggestedBufferSize
        0xFFFFFFFF,      # dwQuality (-1)
        0,               # dwSampleSize
        0, 0, WIDTH, HEIGHT  # rcFrame
    )
    strh_chunk = b"strh" + struct.pack("<I", len(strh_payload)) + strh_payload

    # 4. Build strf chunk (BITMAPINFOHEADER: 40 bytes)
    strf_payload = struct.pack(
        "<IIIHHIIIIII",
        40,              # biSize
        WIDTH,           # biWidth
        HEIGHT,          # biHeight
        1,               # biPlanes
        24,              # biBitCount
        0x47504A4D,      # biCompression ('MJPG')
        WIDTH * HEIGHT * 3,  # biSizeImage
        0, 0, 0, 0
    )
    strf_chunk = b"strf" + struct.pack("<I", len(strf_payload)) + strf_payload

    # 5. Build strl LIST
    strl_data = b"strl" + strh_chunk + strf_chunk
    strl_list = b"LIST" + struct.pack("<I", len(strl_data)) + strl_data

    # 6. Build hdrl LIST
    hdrl_data = b"hdrl" + avih_chunk + strl_list
    hdrl_list = b"LIST" + struct.pack("<I", len(hdrl_data)) + hdrl_data

    # 7. Build movi LIST
    movi_list = b"LIST" + struct.pack("<I", len(movi_data)) + movi_data

    # 8. Build idx1 chunk
    idx_data = bytearray()
    for ckid, flags, off, sz in idx_entries:
        idx_data += struct.pack("<4sIII", ckid, flags, off, sz)
    idx_chunk = b"idx1" + struct.pack("<I", len(idx_data)) + idx_data

    # 9. Assemble full RIFF AVI
    riff_data = b"AVI " + hdrl_list + movi_list + idx_chunk
    riff_chunk = b"RIFF" + struct.pack("<I", len(riff_data)) + riff_data

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(riff_chunk)
    print(f"Generated {out_path} ({len(riff_chunk)} bytes, {num_frames} frames, {WIDTH}x{HEIGHT} @ {FPS} fps)")

def main():
    movies_dir = Path("movies")
    movies_dir.mkdir(exist_ok=True)
    out_file = movies_dir / "sample.avi"

    print(f"Generating {NUM_FRAMES} frames...")
    frames = [create_frame(i) for i in range(NUM_FRAMES)]
    build_avi(frames, out_file)

if __name__ == "__main__":
    main()

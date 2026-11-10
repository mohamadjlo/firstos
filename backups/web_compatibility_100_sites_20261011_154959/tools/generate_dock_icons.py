import os
import struct
from pathlib import Path
from PIL import Image, ImageDraw

def make_dib_ico(img_rgba, out_size=32):
    if img_rgba.size != (out_size, out_size):
        img_rgba = img_rgba.resize((out_size, out_size), Image.Resampling.LANCZOS)
    w, h = img_rgba.size
    pixels = []
    for y in reversed(range(h)):
        for x in range(w):
            r, g, b, a = img_rgba.getpixel((x, y))
            pixels.append(struct.pack('<BBBB', b, g, r, a))
    xor_data = b''.join(pixels)
    row_bytes = (w + 31) // 32 * 4
    and_mask = bytearray(row_bytes * h)
    for y_idx, y in enumerate(reversed(range(h))):
        for x in range(w):
            r, g, b, a = img_rgba.getpixel((x, y))
            if a < 128:
                byte_i = y_idx * row_bytes + (x // 8)
                bit_i = 7 - (x % 8)
                and_mask[byte_i] |= (1 << bit_i)
    
    dib_header = struct.pack('<IIIHHIIIIII',
        40, w, h * 2, 1, 32, 0, len(xor_data), 0, 0, 0, 0)
    image_data = dib_header + xor_data + bytes(and_mask)
    
    ico_header = struct.pack('<HHH', 0, 1, 1)
    ico_entry = struct.pack('<BBBBHHII',
        w if w < 256 else 0,
        h if h < 256 else 0,
        0, 0, 1, 32, len(image_data), 6 + 16)
    return ico_header + ico_entry + image_data

def strip_white_background(img_rgba):
    data = []
    for p in img_rgba.getdata():
        if p[0] > 240 and p[1] > 240 and p[2] > 240:
            data.append((0, 0, 0, 0))
        else:
            data.append(p)
    out = Image.new('RGBA', img_rgba.size)
    out.putdata(data)
    return out

def generate_icons(icons_dir_path):
    icons_dir = Path(icons_dir_path)
    icons_dir.mkdir(parents=True, exist_ok=True)
    
    # 1. FirstOS Astronaut brand icon (os.ico, firstOS.ico)
    src_os_png = icons_dir / 'os.png'
    if not src_os_png.exists() and (icons_dir / 'os .png').exists():
        src_os_png = icons_dir / 'os .png'
    if src_os_png.exists():
        src = Image.open(src_os_png).convert('RGBA')
        im_clean = strip_white_background(src)
        os_ico_data = make_dib_ico(im_clean, 32)
        (icons_dir / 'os.ico').write_bytes(os_ico_data)
        (icons_dir / 'firstOS.ico').write_bytes(os_ico_data)
        print('  Generated os.ico and firstOS.ico')

    # 2. Settings icon from settings.png
    settings_png = icons_dir / 'settings.png'
    if settings_png.exists():
        im = strip_white_background(Image.open(settings_png).convert('RGBA'))
        (icons_dir / 'settings.ico').write_bytes(make_dib_ico(im, 32))
        print('  Generated settings.ico')

    # 3. System icon from system.png
    system_png = icons_dir / 'system.png'
    if system_png.exists():
        im = strip_white_background(Image.open(system_png).convert('RGBA'))
        (icons_dir / 'system.ico').write_bytes(make_dib_ico(im, 32))
        print('  Generated system.ico')

    # 4. Calculator icon (calculator.ico)
    calc_img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(calc_img)
    d.rounded_rectangle([4, 4, 60, 60], radius=10, fill=(38, 44, 56, 255), outline=(70, 82, 100, 255), width=2)
    # LCD Display
    d.rounded_rectangle([10, 10, 54, 22], radius=4, fill=(24, 28, 36, 255), outline=(55, 65, 80, 255))
    d.text((40, 10), '42', fill=(225, 235, 245, 255))
    # Keypad
    for row in range(3):
        for col in range(3):
            bx = 10 + col * 15
            by = 26 + row * 11
            fill_c = (235, 135, 45, 255) if col == 2 else (60, 70, 88, 255)
            d.rounded_rectangle([bx, by, bx + 13, by + 9], radius=2, fill=fill_c)
    (icons_dir / 'calculator.ico').write_bytes(make_dib_ico(calc_img, 32))
    print('  Generated calculator.ico')

    # 5. Files icon (files.ico)
    folder_img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(folder_img)
    # back tab & body
    d.rounded_rectangle([6, 12, 30, 24], radius=4, fill=(30, 110, 210, 255))
    d.rounded_rectangle([6, 18, 58, 56], radius=6, fill=(40, 130, 230, 255))
    # paper preview inside
    d.rounded_rectangle([14, 14, 50, 40], radius=3, fill=(245, 248, 252, 255), outline=(200, 215, 235, 255))
    d.line([(18, 22), (36, 22)], fill=(160, 180, 205, 255), width=2)
    d.line([(18, 28), (44, 28)], fill=(160, 180, 205, 255), width=2)
    # front flap
    d.rounded_rectangle([6, 26, 58, 56], radius=6, fill=(65, 160, 255, 255), outline=(100, 180, 255, 255), width=1)
    (icons_dir / 'files.ico').write_bytes(make_dib_ico(folder_img, 32))
    print('  Generated files.ico')

    # 6. IDE icon (ide.ico)
    ide_img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(ide_img)
    d.rounded_rectangle([4, 4, 60, 60], radius=10, fill=(32, 30, 52, 255), outline=(85, 75, 140, 255), width=2)
    # code brackets < / >
    d.line([(24, 22), (14, 32), (24, 42)], fill=(80, 210, 255, 255), width=3)
    d.line([(40, 22), (50, 32), (40, 42)], fill=(80, 210, 255, 255), width=3)
    d.line([(35, 20), (29, 44)], fill=(255, 185, 80, 255), width=3)
    (icons_dir / 'ide.ico').write_bytes(make_dib_ico(ide_img, 32))
    print('  Generated ide.ico')

    # 7. Task Manager icon (taskman.ico)
    taskman_img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(taskman_img)
    d.rounded_rectangle([4, 4, 60, 60], radius=10, fill=(22, 30, 40, 255), outline=(45, 70, 95, 255), width=2)
    # grid
    d.line([(6, 32), (58, 32)], fill=(35, 50, 70, 255), width=1)
    d.line([(32, 6), (32, 58)], fill=(35, 50, 70, 255), width=1)
    # EKG pulse line
    pulse = [(8, 32), (20, 32), (26, 16), (32, 48), (38, 22), (44, 34), (48, 32), (56, 32)]
    for p1, p2 in zip(pulse[:-1], pulse[1:]):
        d.line([p1, p2], fill=(50, 230, 140, 255), width=3)
    (icons_dir / 'taskman.ico').write_bytes(make_dib_ico(taskman_img, 32))
    print('  Generated taskman.ico')

    # 8. FirstOS Web Browser icon (browser.ico)
    browser_img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    d_browser = ImageDraw.Draw(browser_img)
    d_browser.ellipse([6, 6, 58, 58], fill=(25, 34, 58, 255), outline=(92, 190, 255, 255), width=3)
    d_browser.arc([13, 10, 51, 54], start=0, end=180, fill=(54, 205, 255, 255), width=3)
    d_browser.arc([13, 10, 51, 54], start=180, end=360, fill=(40, 120, 220, 255), width=3)
    d_browser.line([(9, 32), (55, 32)], fill=(117, 225, 255, 255), width=2)
    d_browser.line([(32, 8), (32, 56)], fill=(117, 225, 255, 255), width=2)
    d_browser.polygon([(39, 38), (58, 56), (50, 55), (46, 62), (42, 59), (46, 52), (38, 49)], fill=(255, 255, 255, 255), outline=(20, 30, 50, 255))
    (icons_dir / 'browser.ico').write_bytes(make_dib_ico(browser_img, 32))
    print('  Generated browser.ico')

    # 9. Convert remaining PNG icons to ICO if present
    for png_name in ['calendar', 'games', 'preview', 'spotlight', 'trash']:
        p = icons_dir / f'{png_name}.png'
        if p.exists():
            im = strip_white_background(Image.open(p).convert('RGBA'))
            (icons_dir / f'{png_name}.ico').write_bytes(make_dib_ico(im, 32))
            print(f'  Generated {png_name}.ico')

if __name__ == '__main__':
    generate_icons('icons')

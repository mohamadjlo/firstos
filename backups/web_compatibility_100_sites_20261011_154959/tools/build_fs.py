from pathlib import Path
import struct
import sys

FS_MAGIC = 0x314F5346
FS_VERSION = 2
FS_MAX_FILES = 1024
FS_NAME_SIZE = 128
FS_PATH_SIZE = 256
FS_HEADER_SIZE = 16
FS_ENTRY_SIZE = 408
FS_DISK_STRUCT = struct.Struct('<128s256sIIIIIi')


def padded_cstr(text: str, size: int) -> bytes:
    raw = text.encode('utf-8')
    trimmed = raw[: max(0, size - 1)]
    return trimmed + b'\0' * (size - len(trimmed))


def find_payload_offset(img: bytearray, used_slots: int) -> int:
    payload_off = (FS_HEADER_SIZE + FS_ENTRY_SIZE * FS_MAX_FILES + 3) & ~3
    for i in range(used_slots):
        entry_off = FS_HEADER_SIZE + i * FS_ENTRY_SIZE
        _, _, off, _, cap, used, _, _ = FS_DISK_STRUCT.unpack_from(img, entry_off)
        if used:
            payload_off = max(payload_off, (off + cap + 3) & ~3)
    return payload_off


def pack_entry(name: str, fs_path: str, payload_off: int, size: int, is_dir: bool = False, parent: int = -1) -> bytes:
    return FS_DISK_STRUCT.pack(
        padded_cstr(name, FS_NAME_SIZE),
        padded_cstr(fs_path, FS_PATH_SIZE),
        payload_off,
        size,
        size,
        1,
        1 if is_dir else 0,
        parent,
    )


def add_dir(img: bytearray, slot: int, name: str, fs_path: str, parent: int = -1) -> None:
    entry_off = FS_HEADER_SIZE + slot * FS_ENTRY_SIZE
    img[entry_off:entry_off + FS_DISK_STRUCT.size] = pack_entry(name, fs_path, 0, 0, True, parent)


def add_file(img: bytearray, slot: int, src_path: Path, fs_path: str, parent: int = -1) -> None:
    data = src_path.read_bytes()
    payload_off = find_payload_offset(img, slot)
    payload_end = payload_off + len(data)
    if payload_end > len(img):
        raise SystemExit(f'fs.img too small for {src_path}')

    entry_off = FS_HEADER_SIZE + slot * FS_ENTRY_SIZE
    img[entry_off:entry_off + FS_DISK_STRUCT.size] = pack_entry(src_path.name, fs_path, payload_off, len(data), False, parent)
    img[payload_off:payload_end] = data


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit('usage: build_fs.py <output> <size>')

    out_path = Path(sys.argv[1])
    fs_image_size = int(sys.argv[2])
    img = bytearray(fs_image_size)
    struct.pack_into('<IIII', img, 0, FS_MAGIC, FS_VERSION, fs_image_size, 0)

    slot = 0
    icons_dir = Path('icons')
    if icons_dir.exists():
        try:
            repo_root = Path(__file__).resolve().parent.parent
            if str(repo_root) not in sys.path:
                sys.path.insert(0, str(repo_root))
            from tools.generate_dock_icons import generate_icons
            generate_icons(icons_dir)
        except Exception as e:
            print(f'  [build_fs] icon gen notice: {e}')

        for icon_file in sorted(icons_dir.iterdir()):
            if icon_file.is_file() and not icon_file.name.startswith('.'):
                if slot >= FS_MAX_FILES:
                    raise SystemExit('too many files for fs.img')
                add_file(img, slot, icon_file, f'/icons/{icon_file.name}')
                slot += 1

    wallpapers_dir = Path('wallpapers')
    wallpapers_dir_slot = -1
    if wallpapers_dir.exists():
        # Auto-convert any JPG/PNG/BMP/TIFF/GIF to .raw (BGRX u32 pixels, 1024x768) for the kernel
        image_exts = {'.jpg', '.jpeg', '.png', '.bmp', '.tif', '.tiff', '.gif'}

        try:
            from PIL import Image as PILImage
            has_pil = True
        except ImportError:
            has_pil = False
            print('  [build_fs] PIL not available, skipping image conversion')

        if has_pil:
            for src in sorted(wallpapers_dir.iterdir()):
                if src.is_file() and src.suffix.lower() in image_exts:
                    dst = src.with_suffix('.raw')
                    if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
                        print(f'  [build_fs] converting {src.name} -> {dst.name}')
                        img_pil = PILImage.open(src).convert('RGB').resize((1024, 768), PILImage.LANCZOS)
                        raw = bytearray()
                        for y in range(768):
                            for x in range(1024):
                                r, g, b = img_pil.getpixel((x, y))
                                raw += struct.pack('<I', (r << 16) | (g << 8) | b)
                        dst.write_bytes(raw)
                        print(f'  [build_fs]   -> {len(raw)} bytes')

        # Add wallpapers/ dir entry, then add only .raw files
        if slot >= FS_MAX_FILES:
            raise SystemExit('too many files for fs.img')
        wallpapers_dir_slot = slot
        add_dir(img, slot, 'wallpapers', '/wallpapers')
        slot += 1
        for wallpaper_file in sorted(wallpapers_dir.iterdir()):
            if wallpaper_file.is_file() and not wallpaper_file.name.startswith('.'):
                if slot >= FS_MAX_FILES:
                    raise SystemExit('too many files for fs.img')
                add_file(img, slot, wallpaper_file, f'/wallpapers/{wallpaper_file.name}', wallpapers_dir_slot)
                print(f'  [build_fs] added wallpaper: {wallpaper_file.name} ({wallpaper_file.stat().st_size} bytes)')
                slot += 1

    audio_dir = Path('audio')
    audio_dir_slot = -1
    if audio_dir.exists():
        if slot >= FS_MAX_FILES:
            raise SystemExit('too many files for fs.img')
        audio_dir_slot = slot
        add_dir(img, slot, 'audio', '/audio')
        slot += 1
        for audio_file in sorted(audio_dir.iterdir()):
            if audio_file.is_file() and not audio_file.name.startswith('.'):
                if slot >= FS_MAX_FILES:
                    raise SystemExit('too many files for fs.img')
                add_file(img, slot, audio_file, f'/audio/{audio_file.name}', audio_dir_slot)
                print(f'  [build_fs] added audio: {audio_file.name} ({audio_file.stat().st_size} bytes)')
                slot += 1

        requested_aliases = [
            ("Click.mp3", "click.mp3"),
            ("Drag and drop.mp3", "drag and drop.mp3"),
            ("System notifications.mp3", "System notification.mp3"),
            ("Dialog pop up.mp3", "Dialog pop up.mp3"),
            ("shutdown.mp3", "shutdown.mp3"),
        ]
        existing_names = {f.name.lower(): f for f in audio_dir.iterdir() if f.is_file()}
        for req_name, fallback in requested_aliases:
            if req_name.lower() in existing_names:
                src_f = existing_names[req_name.lower()]
                has_exact = any(f.name == req_name for f in audio_dir.iterdir() if f.is_file())
                if not has_exact and slot < FS_MAX_FILES:
                    add_file(img, slot, src_f, f'/audio/{req_name}', audio_dir_slot)
                    print(f'  [build_fs] added exact audio alias: {req_name}')
                    slot += 1

    movies_dir = Path('movies')
    movies_dir_slot = -1
    if not movies_dir.exists() or not any(movies_dir.glob('*.avi')):
        try:
            import subprocess
            subprocess.run([sys.executable, 'tools/gen_sample_video.py'], check=True)
        except Exception as e:
            print(f'  [build_fs] notice: {e}')

    if movies_dir.exists():
        if slot >= FS_MAX_FILES:
            raise SystemExit('too many files for fs.img')
        movies_dir_slot = slot
        add_dir(img, slot, 'movies', '/movies')
        print('  [build_fs] added movies directory')
        slot += 1
        for movie_file in sorted(movies_dir.iterdir()):
            if movie_file.is_file() and not movie_file.name.startswith('.'):
                if movie_file.stat().st_size > 10 * 1024 * 1024:
                    print(f'  [build_fs] skipping large movie (>10MB): {movie_file.name}')
                    continue
                if slot >= FS_MAX_FILES:
                    raise SystemExit('too many files for fs.img')
                add_file(img, slot, movie_file, f'/movies/{movie_file.name}', movies_dir_slot)
                print(f'  [build_fs] added movie: {movie_file.name} ({movie_file.stat().st_size} bytes)')
                slot += 1

    start_menu = icons_dir / 'start_menu.ico'
    if start_menu.exists() and slot < FS_MAX_FILES:
        add_file(img, slot, start_menu, '/start_menu.ico')
        slot += 1

    brand_icon = icons_dir / 'firstOS.ico'
    if brand_icon.exists() and slot < FS_MAX_FILES:
        add_file(img, slot, brand_icon, '/firstOS.ico')
        slot += 1

    wifi_file = Path('wifi_networks.txt')
    if wifi_file.exists() and slot < FS_MAX_FILES:
        add_file(img, slot, wifi_file, '/wifi_networks.txt')
        print('  [build_fs] added /wifi_networks.txt')
        slot += 1

    for fl_file in Path('.').glob('*.fl'):
        if fl_file.is_file() and slot < FS_MAX_FILES:
            add_file(img, slot, fl_file, f'/{fl_file.name}')
            print(f'  [build_fs] added firstlang script: {fl_file.name}')
            slot += 1

    for tml_file in Path('.').glob('*.tml'):
        if tml_file.is_file() and slot < FS_MAX_FILES:
            add_file(img, slot, tml_file, f'/{tml_file.name}')
            print(f'  [build_fs] added terminal script: {tml_file.name}')
            slot += 1
            
    # Create Apps dir and add apps
    if Path('Apps/hello/hello.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('Apps/hello/hello.elf'), '/hello.elf')
        print('  [build_fs] added /hello.elf')
        slot += 1

    if Path('baremetal_browser/baremetal_browser.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('baremetal_browser/baremetal_browser.elf'), '/baremetal_browser.elf')
        print('  [build_fs] added /baremetal_browser.elf')
        slot += 1

    if Path('userspace/apps/userspace_hello/userspace_hello.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('userspace/apps/userspace_hello/userspace_hello.elf'), '/userspace_hello.elf')
        print('  [build_fs] added /userspace_hello.elf')
        slot += 1

    if Path('userspace/apps/userspace_child/userspace_child.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('userspace/apps/userspace_child/userspace_child.elf'), '/userspace_child.elf')
        print('  [build_fs] added /userspace_child.elf')
        slot += 1

    if Path('userspace/apps/userspace_exec_target/userspace_exec_target.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('userspace/apps/userspace_exec_target/userspace_exec_target.elf'), '/userspace_exec_target.elf')
        print('  [build_fs] added /userspace_exec_target.elf')
        slot += 1

    if Path('userspace/apps/browser_renderer/browser_renderer.elf').exists() and slot < FS_MAX_FILES:
        add_file(img, slot, Path('userspace/apps/browser_renderer/browser_renderer.elf'), '/browser_renderer.elf')
        print('  [build_fs] added /browser_renderer.elf')
        slot += 1
        
    apps_dir_slot = -1
    if slot < FS_MAX_FILES:
        apps_dir_slot = slot
        add_dir(img, slot, 'Apps', '/Apps')
        print('  [build_fs] added Apps directory')
        slot += 1
        
        for fl_file in Path('.').glob('*.fl'):
            if fl_file.is_file() and slot < FS_MAX_FILES:
                app_name = fl_file.with_suffix('.app').name
                add_file(img, slot, fl_file, f'/Apps/{app_name}', apps_dir_slot)
                print(f'  [build_fs] added app: {app_name}')
                slot += 1

    # Create Desktop dir and add desktop files
    desktop_dir_slot = -1
    if slot < FS_MAX_FILES:
        desktop_dir_slot = slot
        add_dir(img, slot, 'Desktop', '/Desktop')
        print('  [build_fs] added Desktop directory')
        slot += 1
        
        desktop_path = Path('Desktop')
        if desktop_path.exists():
            for df in sorted(desktop_path.iterdir()):
                if df.name.startswith('.'):
                    continue
                if df.is_dir() and slot < FS_MAX_FILES:
                    add_dir(img, slot, df.name, f'/Desktop/{df.name}', desktop_dir_slot)
                    print(f'  [build_fs] added desktop directory: {df.name}')
                    slot += 1
                elif df.is_file() and slot < FS_MAX_FILES:
                    add_file(img, slot, df, f'/Desktop/{df.name}', desktop_dir_slot)
                    print(f'  [build_fs] added desktop file: {df.name}')
                    slot += 1

    # Create System dir and add users.cfg
    system_dir_slot = -1
    if slot < FS_MAX_FILES:
        system_dir_slot = slot
        add_dir(img, slot, 'System', '/System')
        print('  [build_fs] added System directory')
        slot += 1

        users_cfg_data = (
            b"# FirstOS User Accounts Configuration\n"
            b"# username:password:display_name:hint:is_root\n"
            b"admin:1234:Administrator:Default admin PIN is 1234:1\n"
            b"user:password:Standard User:Password is 'password':0\n"
            b"guest::Guest Account:No password required:0\n"
        )
        if slot < FS_MAX_FILES:
            payload_off = find_payload_offset(img, slot)
            payload_end = payload_off + len(users_cfg_data)
            entry_off = FS_HEADER_SIZE + slot * FS_ENTRY_SIZE
            img[entry_off:entry_off + FS_DISK_STRUCT.size] = pack_entry('users.cfg', '/System/users.cfg', payload_off, len(users_cfg_data), False, system_dir_slot)
            img[payload_off:payload_end] = users_cfg_data
            print('  [build_fs] added /System/users.cfg')
            slot += 1

    struct.pack_into('<I', img, 12, slot)
    out_path.write_bytes(img)


if __name__ == '__main__':
    main()

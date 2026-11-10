import sys
content = open('tools/build_fs.py').read()
content = content.replace("    wallpapers_dir = Path('wallpapers')", """    # Add icons dir to root
    for f in icons_dir.glob('*.png'):
        if f.is_file() and slot < FS_MAX_FILES:
            add_file(img, slot, f, f'/icons/{f.name}')
            print(f'  [build_fs] added icon: {f.name}')
            slot += 1
    for f in icons_dir.glob('*.ico'):
        if f.is_file() and slot < FS_MAX_FILES:
            add_file(img, slot, f, f'/icons/{f.name}')
            print(f'  [build_fs] added icon: {f.name}')
            slot += 1

    wallpapers_dir = Path('wallpapers')""")
open('tools/build_fs.py', 'w').write(content)

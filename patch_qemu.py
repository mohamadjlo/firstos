import sys
content = open('test_qemu.sh').read()
content = content.replace('-cdrom firstos.iso', '-kernel kernel.bin -initrd fs.img')
open('test_qemu.sh', 'w').write(content)

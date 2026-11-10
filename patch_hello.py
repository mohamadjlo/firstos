with open('kernel.c', 'r') as f:
    text = f.read()

text = text.replace('elf_load_and_run("/hello.elf");', '// elf_load_and_run("/hello.elf");')

with open('kernel.c', 'w') as f:
    f.write(text)

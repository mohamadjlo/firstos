with open('kernel.c', 'r') as f:
    text = f.read()

# Disable hello.elf to avoid 50% CPU starvation
text = text.replace('elf_load_and_run("/hello.elf");', '// elf_load_and_run("/hello.elf");')

with open('kernel.c', 'w') as f:
    f.write(text)

with open('task.c', 'r') as f:
    text = f.read()

# Force GUI to redraw every second so Task Manager charts animate automatically!
bad = '''        tick_sec_counter = 0;
        taskman_record_history();
    }'''
good = '''        tick_sec_counter = 0;
        taskman_record_history();
        extern int needs_redraw;
        needs_redraw = 1;
    }'''
text = text.replace(bad, good)

with open('task.c', 'w') as f:
    f.write(text)

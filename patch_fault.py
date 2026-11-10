with open('fault.c', 'r') as f:
    text = f.read()

text = text.replace('while(1) {} // Halt', '__asm__ volatile("cli"); while(1) { __asm__ volatile("hlt"); } // Halt')

with open('fault.c', 'w') as f:
    f.write(text)

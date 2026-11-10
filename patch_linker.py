import sys
content = open('linker.ld').read()
content = content.replace('    .bss ALIGN(4K) :\n    {\n        *(COMMON)\n        *(.bss*)\n    }', '    .bss ALIGN(4K) :\n    {\n        *(COMMON)\n        *(.bss*)\n    }\n    _end = .;\n    __end = .;')
open('linker.ld', 'w').write(content)

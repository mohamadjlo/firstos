with open('firstlang.c', 'r') as f:
    code = f.read()

code = code.replace(
    'case FN_COMPUTE: exec_compute(fn); break;',
    'case FN_COMPUTE: exec_compute(fn); break;\n        case FN_SCRIPT:  exec_script(fn);  needs_redraw=1; break;'
)

with open('firstlang.c', 'w') as f:
    f.write(code)
print("Hook fixed")

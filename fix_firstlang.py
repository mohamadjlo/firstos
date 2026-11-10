with open('firstlang.c', 'r') as f:
    code = f.read()

# Fix fl_fn_kind
code = code.replace(
    'FN_UI=0, FN_CHECK, FN_COPY, FN_COMPUTE, FN_UNKNOWN',
    'FN_UI=0, FN_CHECK, FN_COPY, FN_COMPUTE, FN_SCRIPT, FN_UNKNOWN'
)

# Fix fl_func_t (add script_body[2048])
# In the original file:
#    char      expr[128];
# } fl_func_t;
code = code.replace(
    'char      expr[128];',
    'char      expr[128];\n    char      script_body[2048];'
)

with open('firstlang.c', 'w') as f:
    f.write(code)
print("Fixed")

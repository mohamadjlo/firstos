with open('firstlang.c', 'r') as f:
    text = f.read()

search = r'''static int fl_parse(const char *src) {
    fl_ntoks = 0;
    fl_lex(src);
    fl_tokpos=0; fl_nfuncs=0; fl_nclasses=0;
    
    // Pre-process imports before real parsing
    int initial_toks = fl_ntoks;
    for (int ti=0; ti < initial_toks; ti++) {
        if (fl_toks[ti].type == TK_IDENT && fl_eq(fl_toks[ti].val, "import") && ti + 1 < initial_toks && fl_toks[ti+1].type == TK_STRING) {
            char path[128]; fl_cpy(path, fl_toks[ti+1].val, 128);
            char buf[4096];
            int r = fs_read_at(path, buf, 4095);
            if (r > 0) { buf[r] = 0; fl_lex(buf); }
            else { char e[64]; e[0]=0; fl_cat(e, "import failed: ", 64); fl_cat(e, path, 64); fle(e); }
        }
    }
    
    fl_skip_nl();'''

replace = r'''static int fl_parse(const char *src) {
    fl_lex(src);
    fl_tokpos=0; fl_nfuncs=0; fl_nclasses=0;
    fl_skip_nl();'''

text = text.replace(search, replace)
with open('firstlang.c', 'w') as f:
    f.write(text)


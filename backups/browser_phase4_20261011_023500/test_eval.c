#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int fl_contains_sub(const char *str, const char *sub) {
    if(!sub || !sub[0]) return 0;
    const char *p = str;
    while(*p) {
        const char *s = p, *t = sub;
        while(*s && *t && *s == *t) { s++; t++; }
        if(!*t) return 1;
        p++;
    }
    return 0;
}

int fl_digit(char c) { return c>='0' && c<='9'; }

void fl_cpy(char *d, const char *s, int m) {
    int i=0; while(*s && i<m-1) d[i++]=*s++; d[i]=0;
}
void fl_rtrim(char *s) {
    int len = strlen(s);
    while(len>0 && s[len-1]==' ') { s[len-1]=0; len--; }
}
void fl_cat(char *d, const char *s, int m) {
    int len = strlen(d);
    fl_cpy(d+len, s, m-len);
}
const char* fl_ltrim(const char *s) {
    while(*s==' ') s++; return s;
}
void fl_itos(int val, char *buf, int max) {
    snprintf(buf, max, "%d", val);
}

int eval_expr_int(const char *ex) { return 0; }
void resolve_var_name(char *v, const char *r) { strcpy(v, r); }
void get_var_str(const char *v, char *o) { strcpy(o, "0"); }

static void eval_expr_str(const char *expr, char *out) {
    out[0]=0;
    char ex[128]; fl_cpy(ex,expr,128); fl_rtrim(ex);
    
    int has_quotes = fl_contains_sub(ex, "\"");
    int has_math = fl_contains_sub(ex, "-") || fl_contains_sub(ex, "*") || fl_contains_sub(ex, "/") || fl_contains_sub(ex, "%");
    
    if (!has_quotes && (has_math || fl_contains_sub(ex, "+"))) {
        int val = eval_expr_int(ex);
        fl_itos(val, out, 128);
        return;
    }
    
    if (!has_quotes && !has_math && !fl_contains_sub(ex, "+")) {
        if(fl_digit(ex[0])) {
            fl_cpy(out, ex, 128);
            return;
        }
        char vname[32]; resolve_var_name(vname, ex);
        get_var_str(vname, out);
        return;
    }
    
    const char *p=fl_ltrim(ex);
    while(*p) {
        p=fl_ltrim(p);
        if(!*p) break;
        if(*p=='+') { p++; continue; }
        if(*p=='"') {
            p++; char tmp[128]; int ti=0;
            while(*p && *p!='"') tmp[ti++] = *p++; tmp[ti]=0;
            if(*p=='"') p++;
            fl_cat(out, tmp, 128);
        } else if(fl_digit(*p)) {
            int val=atoi(p); while(fl_digit(*p)) p++;
            char tmp[16]; fl_itos(val, tmp, 16); fl_cat(out, tmp, 128);
        } else {
            char raw[32]; int vi=0;
            while(*p && *p!=' ' && *p!='+') raw[vi++] = *p++;
            raw[vi]=0;
            char vname[32]; resolve_var_name(vname, raw);
            char tmp[128]; get_var_str(vname, tmp);
            fl_cat(out, tmp, 128);
        }
    }
}

int main() {
    char out[128];
    eval_expr_str("\"mohamad\"", out);
    printf("out: '%s'\n", out);
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int fl_digit(char c) { return c>='0' && c<='9'; }
const char* fl_ltrim(const char *s) { while(*s==' ') s++; return s; }
void resolve_var_name(char *v, const char *r) { strcpy(v, r); }
int get_var_int(const char *name) {
    if(strcmp(name, "x")==0) return 5;
    return 0;
}

static int eval_expr_int(const char *expr) {
    const char *p = fl_ltrim(expr);
    int val = 0;
    char op = '+';
    while(*p) {
        p = fl_ltrim(p);
        if(!*p) break;
        if(*p=='+' || *p=='-' || *p=='*' || *p=='/' || *p=='%') { op = *p; p++; continue; }
        int term = 0;
        if(fl_digit(*p)) { term=atoi(p); while(fl_digit(*p)) p++; }
        else if(*p=='"') { while(*p && *p!='"') p++; if(*p=='"') p++; }
        else {
            char raw[32]; int vi=0;
            while(*p && *p!=' ' && *p!='+' && *p!='-' && *p!='*' && *p!='/' && *p!='%') raw[vi++] = *p++;
            raw[vi]=0;
            char vname[32]; resolve_var_name(vname, raw);
            term = get_var_int(vname);
        }
        if(op=='+') val += term;
        else if(op=='-') val -= term;
        else if(op=='*') val *= term;
        else if(op=='/') { if(term!=0) val /= term; }
        else if(op=='%') { if(term!=0) val %= term; }
    }
    return val;
}

int main() {
    printf("eval(x + 1) = %d\n", eval_expr_int("x + 1"));
    printf("eval(x - 2) = %d\n", eval_expr_int("x - 2"));
    return 0;
}

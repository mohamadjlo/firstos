#include <stdio.h>
#include <string.h>

int fl_starts(const char *str, const char *prefix) {
    while(*prefix) {
        if(*str != *prefix) return 0;
        str++; prefix++;
    }
    return 1;
}

int main() {
    const char *p = "let myname be \"mohamad\"";
    if(fl_starts(p, "let ")) p+=4;
    
    char raw[32]; int vi=0;
    while(*p && !fl_starts(p, " to ") && !fl_starts(p, " be ") && !fl_starts(p, " is ") && !fl_starts(p, " into ") && !fl_starts(p, " with label ") && *p!='=' && *p!=' ') raw[vi++] = *p++;
    raw[vi]=0;
    
    printf("raw: '%s'\n", raw);
    printf("p: '%s'\n", p);
    
    if(fl_starts(p, " to ")) p+=4;
    else if(fl_starts(p, " be ")) p+=4;
    else if(fl_starts(p, " is ")) p+=4;
    else if(fl_starts(p, " into ")) p+=6;
    else if(*p=='=') p+=1;
    
    printf("after skip p: '%s'\n", p);
    
    return 0;
}

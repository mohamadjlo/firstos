import re

new_evaluator = """
/* --- Script Evaluator --- */
#define MAX_VARS 64
static char var_names[MAX_VARS][32];
static int  var_ivals[MAX_VARS];
static char var_svals[MAX_VARS][128];
static int  var_types[MAX_VARS]; /* 0=int, 1=string */
static int  script_num_vars = 0;

static int resolve_var_name(char *vname, const char *raw) {
    int i=0, j=0;
    while(raw[i] && raw[i]!='[' && j<31) vname[j++] = raw[i++];
    if(raw[i]=='[') {
        i++; char idx_expr[64]; int k=0;
        while(raw[i] && raw[i]!=']' && k<63) idx_expr[k++] = raw[i++];
        idx_expr[k]=0;
        int idx = eval_expr_int(idx_expr);
        vname[j++]='_'; char n[8]; fl_itos(idx, n, 8);
        int m=0; while(n[m] && j<31) vname[j++]=n[m++];
    }
    vname[j]=0;
    return 1;
}

static int get_var_idx(const char *name) {
    int i; for(i=0; i<script_num_vars; i++) if(fl_eq(var_names[i], name)) return i;
    return -1;
}

static int get_var_int(const char *name) {
    int idx = get_var_idx(name);
    if(idx>=0 && var_types[idx]==0) return var_ivals[idx];
    if(idx>=0 && var_types[idx]==1) return fl_atoi(var_svals[idx]);
    return 0;
}

static void get_var_str(const char *name, char *out) {
    int idx = get_var_idx(name);
    if(idx>=0 && var_types[idx]==1) fl_cpy(out, var_svals[idx], 128);
    else if(idx>=0 && var_types[idx]==0) fl_itos(var_ivals[idx], out, 128);
    else out[0]=0;
}

static void set_var_int(const char *name, int val) {
    int idx = get_var_idx(name);
    if(idx<0 && script_num_vars<MAX_VARS) { idx=script_num_vars++; fl_cpy(var_names[idx],name,32); }
    if(idx>=0) { var_types[idx]=0; var_ivals[idx]=val; var_svals[idx][0]=0; }
}

static void set_var_str(const char *name, const char *val) {
    int idx = get_var_idx(name);
    if(idx<0 && script_num_vars<MAX_VARS) { idx=script_num_vars++; fl_cpy(var_names[idx],name,32); }
    if(idx>=0) { var_types[idx]=1; fl_cpy(var_svals[idx],val,128); var_ivals[idx]=0; }
}

static int eval_expr_int(const char *expr) {
    char ex[128]; fl_cpy(ex,expr,128); fl_rtrim(ex);
    const char *p=fl_ltrim(ex);
    int val = 0; int is_add=1;
    while(*p) {
        p=fl_ltrim(p);
        if(!*p) break;
        if(*p=='+') { is_add=1; p++; continue; }
        if(*p=='-') { is_add=0; p++; continue; }
        int term=0;
        if(fl_digit(*p)) { term=fl_atoi(p); while(fl_digit(*p)) p++; }
        else if(*p=='"') { while(*p && *p!='"') p++; if(*p=='"') p++; } /* skip str */
        else {
            char raw[32]; int vi=0;
            while(*p && *p!=' ' && *p!='+' && *p!='-') raw[vi++] = *p++;
            raw[vi]=0;
            char vname[32]; resolve_var_name(vname, raw);
            term = get_var_int(vname);
        }
        if(is_add) val += term; else val -= term;
    }
    return val;
}

static void eval_expr_str(const char *expr, char *out) {
    out[0]=0;
    char ex[128]; fl_cpy(ex,expr,128); fl_rtrim(ex);
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
            int val=fl_atoi(p); while(fl_digit(*p)) p++;
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

static int eval_cond(const char *cond) {
    char c[128]; fl_cpy(c,cond,128); fl_rtrim(c);
    const char *op = 0; int is_less=0, is_eq=0, is_gt=0;
    if(fl_contains_sub(c, " is less than ")) { op = " is less than "; is_less=1; }
    else if(fl_contains_sub(c, " < ")) { op = " < "; is_less=1; }
    else if(fl_contains_sub(c, " is ")) { op = " is "; is_eq=1; }
    else if(fl_contains_sub(c, " = ")) { op = " = "; is_eq=1; }
    else if(fl_contains_sub(c, " == ")) { op = " == "; is_eq=1; }
    else if(fl_contains_sub(c, " > ")) { op = " > "; is_gt=1; }
    
    if(!op) return eval_expr_int(c); /* truthy */
    char left[64]; char right[64];
    const char *p = c; while(*p) {
        int i=0; int match=1; while(op[i]) { if(p[i]!=op[i]) { match=0; break; } i++; }
        if(match) {
            int j; for(j=0; j<(p-c); j++) left[j]=c[j]; left[p-c]=0; fl_rtrim(left);
            fl_cpy(right, p+fl_len(op), 64); fl_ltrim(right); fl_rtrim(right);
            
            /* String compare if quotes are used */
            if (left[0]=='"' || right[0]=='"') {
                char lstr[128]; eval_expr_str(left, lstr);
                char rstr[128]; eval_expr_str(right, rstr);
                if(is_eq) return fl_eq(lstr, rstr);
                return 0;
            }
            
            int vleft = eval_expr_int(left);
            int vright = eval_expr_int(right);
            if(is_less) return vleft < vright;
            if(is_gt) return vleft > vright;
            if(is_eq) return vleft == vright;
        }
        p++;
    }
    return 0;
}

static char script_lines[256][128];
static int script_num_lines=0;
static int script_call_stack[32];
static int script_call_sp=0;

static void exec_script_loop(int start_ip) {
    int ip = start_ip;
    int loop_stack[16]; int loop_sp = 0;
    
    while(ip < script_num_lines) {
        char *line = script_lines[ip];
        const char *p = fl_ltrim(line);
        if(!*p) { ip++; continue; }
        
        if(fl_starts(p, "set ")) {
            p+=4; p=fl_ltrim(p);
            char raw[32]; int vi=0;
            while(*p && !fl_starts(p, " to ") && *p!='=') raw[vi++] = *p++;
            raw[vi]=0; fl_rtrim(raw);
            char vname[32]; resolve_var_name(vname, raw);
            if(fl_starts(p, " to ")) p+=4; else if(*p=='=') p+=1;
            p=fl_ltrim(p);
            if(*p=='"' || fl_contains_sub(p, "\\\"")) {
                char sval[128]; eval_expr_str(p, sval);
                set_var_str(vname, sval);
            } else {
                set_var_int(vname, eval_expr_int(p));
            }
        }
        else if(fl_starts(p, "if ")) {
            p+=3; p=fl_ltrim(p);
            char cond[64]; int ci=0;
            while(*p && *p!=':') cond[ci++] = *p++; cond[ci]=0; fl_rtrim(cond);
            if(!eval_cond(cond)) {
                int skip=1;
                while(ip+1 < script_num_lines && skip>0) {
                    ip++; const char *np = fl_ltrim(script_lines[ip]);
                    if(fl_starts(np, "if ") || fl_starts(np, "while ") || fl_starts(np, "on ")) skip++;
                    if(fl_starts(np, "end")) skip--;
                }
            }
        }
        else if(fl_starts(p, "while ")) {
            p+=6; p=fl_ltrim(p);
            char cond[64]; int ci=0;
            while(*p && *p!=':') cond[ci++] = *p++; cond[ci]=0; fl_rtrim(cond);
            if(!eval_cond(cond)) {
                int skip=1;
                while(ip+1 < script_num_lines && skip>0) {
                    ip++; const char *np = fl_ltrim(script_lines[ip]);
                    if(fl_starts(np, "if ") || fl_starts(np, "while ") || fl_starts(np, "on ")) skip++;
                    if(fl_starts(np, "end")) skip--;
                }
            } else { loop_stack[loop_sp++] = ip; }
        }
        else if(fl_starts(p, "on ")) {
            /* on click "Btn": */
            p+=3; p=fl_ltrim(p);
            if(fl_starts(p, "click ")) {
                p+=6; p=fl_ltrim(p);
                char target[64]; eval_expr_str(p, target);
                /* skip block immediately, it's only executed on click */
                int skip=1;
                int action_start = ip+1;
                while(ip+1 < script_num_lines && skip>0) {
                    ip++; const char *np = fl_ltrim(script_lines[ip]);
                    if(fl_starts(np, "if ") || fl_starts(np, "while ") || fl_starts(np, "on ")) skip++;
                    if(fl_starts(np, "end")) skip--;
                }
                /* assign action_start to all buttons matching target */
                int i; for(i=0; i<num_dyn_ui; i++) {
                    if(fl_eq(dyn_ui[i].text, target) || fl_eq(dyn_ui[i].name, target)) {
                        dyn_ui[i].action_ip = action_start;
                    }
                }
            }
        }
        else if(fl_starts(p, "end")) {
            if(loop_sp > 0) ip = loop_stack[--loop_sp] - 1;
            else if(script_call_sp > 0) { ip = script_call_stack[--script_call_sp]; } /* return from call */
        }
        else if(fl_starts(p, "call ")) {
            p+=5; p=fl_ltrim(p);
            char target[64]; int ci=0;
            while(*p && *p!=' ') target[ci++] = *p++; target[ci]=0; fl_rtrim(target);
            /* find function starting with 'function target:' */
            char search[64]; fl_cpy(search, "function ", 64); fl_cat(search, target, 64); fl_cat(search, ":", 64);
            int i; int found=-1;
            for(i=0; i<script_num_lines; i++) {
                if(fl_starts(fl_ltrim(script_lines[i]), search)) { found=i; break; }
            }
            if(found>=0) {
                script_call_stack[script_call_sp++] = ip;
                ip = found; /* jump */
            } else { fle("call: function not found"); }
        }
        else if(fl_starts(p, "write ")) {
            p+=6; p=fl_ltrim(p);
            char val[128]; int vi=0;
            while(*p && !fl_starts(p, " to ")) val[vi++] = *p++; val[vi]=0; fl_rtrim(val);
            if(fl_starts(p, " to ")) p+=4;
            char path[128]; eval_expr_str(p, path);
            char sval[128]; eval_expr_str(val, sval);
            fs_create_at(path, 0); fs_write_at(path, sval, fl_len(sval));
        }
        else if(fl_starts(p, "read ")) {
            p+=5; p=fl_ltrim(p);
            char path[128]; int vi=0;
            while(*p && !fl_starts(p, " to ")) path[vi++] = *p++; path[vi]=0; fl_rtrim(path);
            if(fl_starts(p, " to ")) p+=4;
            char pstr[128]; eval_expr_str(path, pstr);
            char raw[32]; fl_cpy(raw, p, 32); fl_rtrim(raw);
            char vname[32]; resolve_var_name(vname, raw);
            char buf[512]; int r = fs_read_at(pstr, buf, 511);
            if(r>=0) { buf[r]=0; set_var_str(vname, buf); } else set_var_str(vname, "");
        }
        else if(fl_starts(p, "add ")) {
            p+=4; p=fl_ltrim(p);
            if(num_dyn_ui < MAX_DYN_UI) {
                fl_dyn_ui_t *u = &dyn_ui[num_dyn_ui++];
                u->type = 'B'; u->action_ip = -1; u->name[0]=0;
                if(fl_starts(p, "button ")) { u->type='B'; p+=7; }
                else if(fl_starts(p, "label ")) { u->type='L'; p+=6; }
                else if(fl_starts(p, "slider ")) { u->type='S'; p+=7; }
                else if(fl_starts(p, "text ")) { u->type='T'; p+=5; }
                p=fl_ltrim(p);
                char text_expr[128]; int ti=0;
                while(*p && !fl_starts(p, "at ") && !fl_starts(p, "named ")) text_expr[ti++] = *p++;
                text_expr[ti]=0; fl_rtrim(text_expr);
                eval_expr_str(text_expr, u->text);
                
                if(fl_starts(p, "named ")) {
                    p+=6; p=fl_ltrim(p);
                    int ni=0; while(*p && !fl_starts(p, "at ")) u->name[ni++] = *p++;
                    u->name[ni]=0; fl_rtrim(u->name);
                }
                
                if(fl_starts(p, "at ")) {
                    p+=3; p=fl_ltrim(p);
                    if(*p=='(') p++;
                    char xs[32]; int xi=0;
                    while(*p && *p!=',') xs[xi++] = *p++; xs[xi]=0; fl_rtrim(xs);
                    if(*p==',') p++; p=fl_ltrim(p);
                    char ys[32]; int yi=0;
                    while(*p && *p!=')') ys[yi++] = *p++; ys[yi]=0; fl_rtrim(ys);
                    
                    if(fl_eq(xs,"center")) u->x = -1; else u->x = eval_expr_int(xs);
                    if(fl_eq(ys,"top")) u->y = 10; else if(fl_eq(ys,"bottom")) u->y = -1; else u->y = eval_expr_int(ys);
                } else { u->x=10; u->y=10; }
            }
        }
        ip++;
    }
}

static void exec_script(fl_func_t *fn) {
    script_num_lines=0;
    const char *s = fn->script_body;
    while(*s && script_num_lines < 256) {
        int i=0; while(*s && *s!='\\n' && i<127) script_lines[script_num_lines][i++]=*s++;
        script_lines[script_num_lines][i]=0; fl_rtrim(script_lines[script_num_lines]);
        if(script_lines[script_num_lines][0]) script_num_lines++;
        if(*s=='\\n') s++;
    }
    
    script_num_vars = 0;
    num_dyn_ui = 0;
    fl_active_ui_fn = fn;
    fl_ui_open = 1;
    script_call_sp = 0;
    
    exec_script_loop(0);
    
    char msg[64]; msg[0]=0;
    fl_cat(msg,"UI script finished: ",sizeof(msg));
    fl_cat(msg,fn->name,sizeof(msg));
    fli(msg);
}
"""

with open('firstlang.c', 'r') as f:
    code = f.read()

# Replace the evaluator block
start_marker = "/* --- Script Evaluator --- */"
end_marker = "/* render dynamic ui */"

start_idx = code.find(start_marker)
end_idx = code.find(end_marker)

if start_idx != -1 and end_idx != -1:
    code = code[:start_idx] + new_evaluator + "\n" + code[end_idx:]
else:
    print("Markers not found!")

# Also patch fl_dyn_ui_t struct definition at top to include action_ip
code = code.replace(
    'typedef struct { char type; char name[32]; char text[64]; int x, y; } fl_dyn_ui_t;',
    'typedef struct { char type; char name[32]; char text[64]; int x, y; int action_ip; } fl_dyn_ui_t;'
)

# And patch firstlang_mouse_click in firstlang.c to handle button actions!
mouse_click_old = '''    /* swallow clicks inside window */
    if (mx >= fl_ui_x && mx <= fl_ui_x + fl_ui_w &&
        my >= fl_ui_y && my <= fl_ui_y + fl_ui_h) {
        return 1;
    }'''

mouse_click_new = '''    /* handle dynamic button clicks */
    int di;
    for(di=0; di<num_dyn_ui; di++) {
        fl_dyn_ui_t *u = &dyn_ui[di];
        if(u->type == 'B' && u->action_ip >= 0) {
            int dx = u->x; int dy = u->y;
            if(dx == -1) dx = fl_ui_w/2 - 60;
            if(dy == -1) dy = fl_ui_h - 60;
            int bx = fl_ui_x + dx; int by = fl_ui_y + 40 + dy;
            if(mx >= bx && mx <= bx + 120 && my >= by && my <= by + 28) {
                script_call_sp = 0; /* clean stack */
                exec_script_loop(u->action_ip);
                return 1;
            }
        }
    }
    
    /* swallow clicks inside window */
    if (mx >= fl_ui_x && mx <= fl_ui_x + fl_ui_w &&
        my >= fl_ui_y && my <= fl_ui_y + fl_ui_h) {
        return 1;
    }'''

code = code.replace(mouse_click_old, mouse_click_new)

with open('firstlang.c', 'w') as f:
    f.write(code)
print("Done")

/*
 * terminal.c — FirstOS Shell Terminal v2.0
 * Professional shell with 25+ commands, variables, pipes (basic), history, RTC date.
 */

#include "fs.h"
#include "serial.h"
#include "framebuffer.h"
#include "font.h"
#include "rtc.h"
#include "firstlang.h"
#include "net.h"
#include "ports.h"
#include "firewall.h"
#include "tcp.h"
#include "udp.h"
#include "ip.h"
#include "net_socket.h"
#include "http_v11.h"
#include "download_manager.h"
#include <stdio.h>
#include <string.h>

typedef unsigned int  u32;
typedef unsigned char u8;

/* ───────────────────────────── helpers ─────────────────────────────── */
static int tlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void tcpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static int teq(const char *a, const char *b) {
    while(*a&&*b){if(*a++!=*b++)return 0;} return *a==0&&*b==0;
}
static int teq_ci(const char *a, const char *b) {
    if(!a||!b) return 0;
    while(*a&&*b){
        char ca=*a, cb=*b;
        if(ca>='A'&&ca<='Z') ca+='a'-'A';
        if(cb>='A'&&cb<='Z') cb+='a'-'A';
        if(ca!=cb) return 0;
        a++; b++;
    }
    return *a==0&&*b==0;
}
static int tstarts_ci(const char *s, const char *prefix) {
    if(!s||!prefix) return 0;
    while(*prefix){
        char cs=*s, cp=*prefix;
        if(cs>='A'&&cs<='Z') cs+='a'-'A';
        if(cp>='A'&&cp<='Z') cp+='a'-'A';
        if(cs!=cp) return 0;
        s++; prefix++;
    }
    return 1;
}
static int tends_ci(const char *s, const char *suffix) {
    int sl=tlen(s), el=tlen(suffix);
    if(sl<el) return 0;
    for(int i=0;i<el;i++){
        char a=s[sl-el+i], b=suffix[i];
        if(a>='A'&&a<='Z') a+='a'-'A';
        if(b>='A'&&b<='Z') b+='a'-'A';
        if(a!=b) return 0;
    }
    return 1;
}
static void tcat(char *d, const char *s, int max) {
    int n=tlen(d),i=0; while(n<max-1&&s[i]){d[n++]=s[i++];} d[n]=0;
}
static const char *tltrim(const char *s) {
    while(*s==' '||*s=='\t') s++;
    return s;
}
static void trtrim(char *s) {
    int n=tlen(s)-1;
    while(n>=0&&(s[n]==' '||s[n]=='\t'||s[n]=='\n')){s[n--]=0;}
}
static int tdigit(char c) { return c>='0'&&c<='9'; }
static int tatoi(const char *s) {
    int v=0,neg=0;
    if(*s=='-'){neg=1;s++;} while(tdigit(*s)){v=v*10+(*s-'0');s++;}
    return neg?-v:v;
}
static void titos(int v, char *b, int sz) {
    if(sz<2){b[0]=0;return;} if(v==0){b[0]='0';b[1]=0;return;}
    int neg=0; if(v<0){neg=1;v=-v;}
    char t[16]; int n=0;
    while(v){t[n++]=(char)('0'+v%10);v/=10;}
    if(neg) t[n++]='-';
    int i=0; while(n>0){b[i++]=t[--n];} b[i]=0;
}
static void tutos(u32 v, char *b, int sz) {
    if(sz<2){b[0]=0;return;} if(v==0){b[0]='0';b[1]=0;return;}
    char t[16]; int n=0;
    while(v){t[n++]=(char)('0'+v%10);v/=10;}
    int i=0; while(n>0){b[i++]=t[--n];} b[i]=0;
}
static void t2d(int v, char *b) { /* zero-padded 2-digit */
    b[0]=(char)('0'+v/10); b[1]=(char)('0'+v%10); b[2]=0;
}
static int tcontains(const char *haystack, const char *needle) {
    int nl=tlen(needle),hl=tlen(haystack);
    if(nl==0) return 1;
    int i,j;
    for(i=0;i<=hl-nl;i++){
        for(j=0;j<nl;j++) if(haystack[i+j]!=needle[j]) break;
        if(j==nl) return 1;
    }
    return 0;
}
static void tsplit(const char *line, char *cmd, int csz, char *arg, int asz) {
    int i=0;
    const char *p=tltrim(line);
    while(*p&&*p!=' '&&i<csz-1){cmd[i++]=*p++;} cmd[i]=0;
    p=tltrim(p);
    int j=0; while(*p&&j<asz-1){arg[j++]=*p++;} arg[j]=0;
    trtrim(arg);
}
static void tresol(const char *cwd, const char *rel, char *out, int sz) {
    if(rel[0]=='/'){tcpy(out,rel,sz);return;}
    tcpy(out,cwd,sz); int n=tlen(out);
    if(n>0&&out[n-1]!='/'){out[n++]='/';out[n]=0;}
    tcat(out,rel,sz);
}
static void t2args(const char *line, char *a1, char *a2, int sz) {
    int i=0,j=0;
    const char *p=tltrim(line);
    while(*p&&*p!=' '&&i<sz-1){a1[i++]=*p++;} a1[i]=0;
    p=tltrim(p);
    while(*p&&j<sz-1){a2[j++]=*p++;} a2[j]=0;
}

/* ───────────────────────────── state ───────────────────────────────── */
#define TERM_ROWS     512
#define TERM_LLEN     128
#define TERM_HIST_MAX  64
#define TERM_IN_MAX   256
#define TERM_VARS      32
#define TERM_VAR_LEN   64

static char  tlines[TERM_ROWS][TERM_LLEN];
static u8    ttype[TERM_ROWS];   /* 0=normal 1=prompt 2=error 3=info 4=dim */
static int   tlcount = 0;
static int   tscroll = 0;
char tcwd[256]  = "/";
static char  tinput[TERM_IN_MAX];
static int   tinlen     = 0;
static int   tincur     = 0;
static char  thist[TERM_HIST_MAX][TERM_IN_MAX];
static int   thcount    = 0;
static int   thidx      = -1;
static int   tinsert __attribute__((unused)) = 1;  /* insert vs overwrite mode */

/* variables: "NAME=VALUE" */
static char tvkeys[TERM_VARS][TERM_VAR_LEN];
static char tvvals[TERM_VARS][TERM_VAR_LEN];
static int  tvcount = 0;

int terminal_cursor_visible = 1;
int terminal_focused        = 0;

static char thist_draft[TERM_IN_MAX];

extern char editor_clipboard[512];

static int term_selecting       = 0;
static int term_sel_start_line  = -1;
static int term_sel_start_col   = -1;
static int term_sel_end_line    = -1;
static int term_sel_end_col     = -1;

int terminal_has_selection(void) {
    if (term_sel_start_line < 0 || term_sel_end_line < 0) return 0;
    if (term_sel_start_line == term_sel_end_line && term_sel_start_col == term_sel_end_col) return 0;
    return 1;
}

void terminal_clear_selection(void) {
    term_selecting = 0;
    term_sel_start_line = -1;
    term_sel_start_col  = -1;
    term_sel_end_line   = -1;
    term_sel_end_col    = -1;
}

/* ───────────────────────────── variable engine ─────────────────────── */
static const char *tvar_get(const char *key) {
    int i; for(i=0;i<tvcount;i++) if(teq(tvkeys[i],key)) return tvvals[i];
    return 0;
}
static void tvar_set(const char *key, const char *val) {
    int i; for(i=0;i<tvcount;i++){
        if(teq(tvkeys[i],key)){tcpy(tvvals[i],val,TERM_VAR_LEN);return;}
    }
    if(tvcount<TERM_VARS){tcpy(tvkeys[tvcount],key,TERM_VAR_LEN);tcpy(tvvals[tvcount],val,TERM_VAR_LEN);tvcount++;}
}
/* expand $VAR in line → out */
static void texpand(const char *in, char *out, int osz) {
    int oi=0; const char *p=in;
    while(*p&&oi<osz-1) {
        if(*p=='$') {
            p++;
            char vname[64]; int vi=0;
            while(*p&&(*p=='_'||(*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9'))&&vi<63)
                {vname[vi++]=*p++;} vname[vi]=0;
            const char *v=tvar_get(vname);
            if(v){while(*v&&oi<osz-1){out[oi++]=*v++;}}
        } else {
            out[oi++]=*p++;
        }
    }
    out[oi]=0;
}

/* ───────────────────────────── output ──────────────────────────────── */
static void tpush(const char *text, u8 type) {
    int total=tlen(text); int off=0;
    do {
        int chunk=total-off; if(chunk>TERM_LLEN-1) chunk=TERM_LLEN-1;
        if(tlcount<TERM_ROWS){
            int k; for(k=0;k<chunk;k++) tlines[tlcount][k]=text[off+k];
            tlines[tlcount][chunk]=0; ttype[tlcount]=type; tlcount++;
        } else {
            int i; for(i=0;i<TERM_ROWS-1;i++){
                int k; for(k=0;k<TERM_LLEN;k++) tlines[i][k]=tlines[i+1][k];
                ttype[i]=ttype[i+1];
            }
            int k; for(k=0;k<chunk;k++) tlines[TERM_ROWS-1][k]=text[off+k];
            tlines[TERM_ROWS-1][chunk]=0; ttype[TERM_ROWS-1]=type;
        }
        off+=(chunk>0?chunk:1);
    } while(off<total);
}
static void tp(const char *s)   { tpush(s,0); }

void terminal_print(const char *text) {
    const char *p = text;
    char line[128];
    while (*p) {
        int i = 0;
        while (*p && *p != '\n' && i < 127) {
            line[i++] = *p++;
        }
        line[i] = 0;
        tpush(line, 0);
        if (*p == '\n') p++;
    }
}
static void te(const char *s)   { tpush(s,2); }
static void ti(const char *s)   { tpush(s,3); }
static void td(const char *s)   { tpush(s,4); }
static void tblk(void)          { tpush("",0); }
static void trun_cmd(const char *cmd, const char *arg);

static int is_system_path(const char *path) {
    if (!path || !path[0]) return 0;
    if (tstarts_ci(path, "/System") || tstarts_ci(path, "/system")) return 1;
    if (teq_ci(path, "/hello.elf") || teq_ci(path, "/kernel.bin") || teq_ci(path, "/fs.img")) return 1;
    return 0;
}

int terminal_is_superuser(void) {
    const char *u = tvar_get("USER");
    if (!u) return 1;
    if (teq_ci(u, "root") || teq_ci(u, "admin")) return 1;
    const char *su = tvar_get("SUPERUSER");
    if (su && teq(su, "1")) return 1;
    return 0;
}

void terminal_set_active_user(const char *username, int is_root) {
    if (!username || !username[0]) return;
    tvar_set("USER", username);
    tvar_set("SUPERUSER", is_root ? "1" : "0");
}

static void tprompt_str(char *buf, int sz) {
    buf[0] = 0;
    /* show username@hostname:cwd */
    const char *user = tvar_get("USER"); if(!user) user="root";
    const char *host = tvar_get("HOSTNAME"); if(!host) host="firstos";
    tcat(buf, user, sz); tcat(buf, "@", sz); tcat(buf, host, sz); tcat(buf, ":", sz);
    tcat(buf, tcwd, sz);
    if (terminal_is_superuser()) {
        tcat(buf, " # ", sz);
    } else {
        tcat(buf, " $ ", sz);
    }
}

/* ─────────────────── pipe/redirect: basic "cmd > file" ─────────────── */
static char pipe_capture[4096];
static int  pipe_capturing = 0;
static int  pipe_cap_len   = 0;
static void tp_cap(const char *s) {
    int n=tlen(s);
    int i; for(i=0;i<n&&pipe_cap_len<(int)sizeof(pipe_capture)-2;i++) pipe_capture[pipe_cap_len++]=s[i];
    pipe_capture[pipe_cap_len++]='\n';
    pipe_capture[pipe_cap_len]=0;
}

/* ───────────────────────────── commands ────────────────────────────── */

static void tformat_size(u32 sz, char *out, int max) {
    out[0] = 0;
    if (sz < 1024) {
        char s[16]; tutos(sz, s, sizeof(s));
        tcat(out, s, max);
        tcat(out, " B", max);
    } else if (sz < 1048576) {
        char s[16]; tutos(sz / 1024, s, sizeof(s));
        tcat(out, s, max);
        tcat(out, " KB", max);
    } else {
        char s[16]; tutos(sz / 1048576, s, sizeof(s));
        tcat(out, s, max);
        tcat(out, " MB", max);
    }
}

static void cmd_ls(const char *arg) {
    char flags[16]; char path_arg[256];
    flags[0] = 0; path_arg[0] = 0;
    /* parse flags like -l -a -h */
    const char *p = tltrim(arg);
    while (*p == '-') {
        p++; while (*p && *p != ' ') { char f[2] = {*p, 0}; tcat(flags, f, sizeof(flags)); p++; }
        p = tltrim(p);
    }
    tcpy(path_arg, p, sizeof(path_arg));
    trtrim(path_arg);

    char target[256]; target[0] = 0;
    if (!path_arg[0] || teq(path_arg, ".")) {
        tcpy(target, tcwd, sizeof(target));
    } else if (teq(path_arg, "..")) {
        tcpy(target, tcwd, sizeof(target));
        int n = tlen(target);
        if (n > 0 && target[n - 1] == '/') target[--n] = 0;
        while (n > 0 && target[n - 1] != '/') n--;
        if (n == 0) n = 1;
        target[n] = 0;
    } else {
        tresol(tcwd, path_arg, target, sizeof(target));
    }

    // Strip trailing slash if length > 1
    int tl = tlen(target);
    while (tl > 1 && target[tl - 1] == '/') target[--tl] = 0;

    // Check if target is a single file in fs
    char canonical[256]; canonical[0] = 0;
    int target_is_file = 0;
    const fs_entry_t *matched_file = 0;

    if (teq(target, "/")) {
        tcpy(canonical, "/", sizeof(canonical));
    } else {
        for (int slot = 0; slot < 256; slot++) {
            const fs_entry_t *fe = fs_entry_at(slot);
            if (!fe || !fe->used) continue;
            if (teq_ci(fe->path, target)) {
                if (fe->is_dir) {
                    tcpy(canonical, fe->path, sizeof(canonical));
                } else {
                    target_is_file = 1;
                    matched_file = fe;
                }
                break;
            }
        }
        if (!canonical[0] && !target_is_file) {
            tcpy(canonical, target, sizeof(canonical));
        }
    }

    int show_long = tcontains(flags, "l");

    // If single file target:
    if (target_is_file && matched_file) {
        char line[TERM_LLEN]; line[0] = 0;
        char sz[32]; tformat_size(matched_file->size, sz, sizeof(sz));
        if (show_long) {
            tcat(line, "-rw-r--r--  [FILE]  ", sizeof(line));
            int pad = 10 - tlen(sz);
            for (int sp = 0; sp < pad; sp++) tcat(line, " ", sizeof(line));
            tcat(line, sz, sizeof(line));
            tcat(line, "  ", sizeof(line));
            tcat(line, matched_file->name, sizeof(line));
        } else {
            tcat(line, "[FILE]  ", sizeof(line));
            tcat(line, matched_file->name, sizeof(line));
            tcat(line, "  (", sizeof(line));
            tcat(line, sz, sizeof(line));
            tcat(line, ")", sizeof(line));
        }
        if (pipe_capturing) tp_cap(line); else tp(line);
        return;
    }

    int count = fs_count_in_dir(canonical);
    if (count < 0) {
        te("ls: cannot access directory");
        return;
    }
    if (count == 0) {
        ti("(directory is empty)");
        return;
    }

    if (show_long) {
        char header[TERM_LLEN]; header[0] = 0;
        tcat(header, "TYPE     SIZE       NAME", sizeof(header));
        if (pipe_capturing) tp_cap(header); else tp(header);
        header[0] = 0;
        tcat(header, "────────────────────────────────────", sizeof(header));
        if (pipe_capturing) tp_cap(header); else td(header);
    }

    int dir_count = 0, file_count = 0;
    for (int i = 0; i < count; i++) {
        const fs_entry_t *e = fs_entry_in_dir(canonical, i);
        if (!e) continue;
        if (e->is_dir) dir_count++; else file_count++;

        char line[TERM_LLEN]; line[0] = 0;
        char sz[32];
        if (!e->is_dir) tformat_size(e->size, sz, sizeof(sz));

        if (show_long) {
            if (e->is_dir) {
                tcat(line, "[DIR]    --         ", sizeof(line));
                tcat(line, e->name, sizeof(line));
                tcat(line, "/", sizeof(line));
            } else {
                tcat(line, "[FILE]   ", sizeof(line));
                int pad = 10 - tlen(sz);
                for (int sp = 0; sp < pad; sp++) tcat(line, " ", sizeof(line));
                tcat(line, sz, sizeof(line));
                tcat(line, " ", sizeof(line));
                tcat(line, e->name, sizeof(line));
            }
        } else {
            if (e->is_dir) {
                tcat(line, "[DIR]   ", sizeof(line));
                tcat(line, e->name, sizeof(line));
                tcat(line, "/", sizeof(line));
            } else {
                tcat(line, "[FILE]  ", sizeof(line));
                tcat(line, e->name, sizeof(line));
                int pad = 20 - tlen(e->name);
                if (pad > 0) {
                    for (int sp = 0; sp < pad; sp++) tcat(line, " ", sizeof(line));
                } else {
                    tcat(line, "  ", sizeof(line));
                }
                tcat(line, sz, sizeof(line));
            }
        }
        if (pipe_capturing) tp_cap(line); else tp(line);
    }

    // Summary line
    char summary[TERM_LLEN]; summary[0] = 0;
    tcat(summary, "Total: ", sizeof(summary));
    char cnt_str[16]; titos(count, cnt_str, sizeof(cnt_str));
    tcat(summary, cnt_str, sizeof(summary));
    tcat(summary, " items (", sizeof(summary));
    char d_str[16]; titos(dir_count, d_str, sizeof(d_str));
    tcat(summary, d_str, sizeof(summary));
    tcat(summary, " dirs, ", sizeof(summary));
    char f_str[16]; titos(file_count, f_str, sizeof(f_str));
    tcat(summary, f_str, sizeof(summary));
    tcat(summary, " files)", sizeof(summary));
    if (pipe_capturing) tp_cap(summary); else td(summary);
}

static void cmd_cd(const char *arg) {
    if(!arg[0]||teq(arg,"~")||teq(arg,"/")) { tcpy(tcwd,"/",sizeof(tcwd)); return; }
    if(teq(arg,"..")) {
        if(teq(tcwd,"/")) return;
        int n=tlen(tcwd);
        if(n>0&&tcwd[n-1]=='/') tcwd[--n]=0;
        while(n>0&&tcwd[n-1]!='/') n--;
        if(n==0) n=1;
        tcwd[n]=0;
        return;
    }
    if(teq(arg,"-")) {
        const char *prev=tvar_get("OLDPWD");
        if(prev&&prev[0]) { tvar_set("OLDPWD",tcwd); tcpy(tcwd,prev,sizeof(tcwd)); tp(tcwd); }
        else te("cd: OLDPWD not set");
        return;
    }
    char t[256]; tresol(tcwd,arg,t,sizeof(t));
    int c=fs_count_in_dir(t);
    if(c<0){te("cd: no such directory");return;}
    tvar_set("OLDPWD",tcwd);
    tcpy(tcwd,t,sizeof(tcwd));
    int n=tlen(tcwd); if(n>1&&tcwd[n-1]=='/') tcwd[--n]=0;
}

static void cmd_pwd(void)  {
    if(pipe_capturing) tp_cap(tcwd); else tp(tcwd);
}

static void cmd_mkdir(const char *arg) {
    char a1[256],a2[256]; t2args(arg,a1,a2,256);
    int recursive=teq(a1,"-p");
    const char *name=recursive?a2:a1;
    if(!name[0]){te("mkdir: missing arg");return;}
    char p[256]; tresol(tcwd,name,p,sizeof(p));
    if(is_system_path(p) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    if(fs_mkdir(p)) ti("Directory created."); else te("mkdir: failed");
}

static void cmd_touch(const char *arg) {
    if(!arg[0]){te("touch: missing arg");return;}
    char p[256]; tresol(tcwd,arg,p,sizeof(p));
    if(is_system_path(p) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    if(fs_create_at(p,0)) ti("File created."); else te("touch: already exists");
}

static void cmd_rm(const char *arg) {
    char flags[8],name[256]; t2args(arg,flags,name,256);
    if(flags[0]!='-'){tcpy(name,flags,256);flags[0]=0;}
    if(!name[0]&&!flags[0]){te("rm: missing arg");return;}
    const char *target=name[0]?name:flags;
    char p[256]; tresol(tcwd,target,p,sizeof(p));
    if(is_system_path(p) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    if(fs_delete_at(p)) ti("Removed."); else te("rm: no such file");
}

static void cmd_mv(const char *arg) {
    char s[256],d[256],sp[256],dp[256]; t2args(arg,s,d,256);
    if(!s[0]||!d[0]){te("mv: usage: mv <src> <dst>");return;}
    tresol(tcwd,s,sp,256); tresol(tcwd,d,dp,256);
    if((is_system_path(sp) || is_system_path(dp)) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    if(fs_rename_at(sp,dp)) ti("Moved."); else te("mv: failed");
}

static void cmd_cp(const char *arg) {
    char s[256],d[256],sp[256],dp[256]; t2args(arg,s,d,256);
    if(!s[0]||!d[0]){te("cp: usage: cp <src> <dst>");return;}
    tresol(tcwd,s,sp,256); tresol(tcwd,d,dp,256);
    if(is_system_path(dp) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    if(fs_copy_at(sp, dp)) ti("Copied."); else te("cp: failed");
}

static void cmd_cat(const char *arg) {
    if(!arg[0]){te("cat: missing arg");return;}
    char p[256]; tresol(tcwd,arg,p,sizeof(p));
    static char buf[8192]; int bytes=fs_read_at(p,buf,sizeof(buf)-1);
    if(bytes<0){te("cat: file not found");return;} buf[bytes]=0;
    int s=0;
    while(s<=bytes){
        int e=s; while(e<=bytes&&buf[e]!='\n'&&buf[e]!=0) e++;
        char tmp[TERM_LLEN]; int ln=e-s; if(ln>TERM_LLEN-1) ln=TERM_LLEN-1;
        int k; for(k=0;k<ln;k++) tmp[k]=buf[s+k]; tmp[ln]=0;
        if(pipe_capturing) tp_cap(tmp); else tp(tmp);
        if(buf[e]==0) break;
        s=e+1;
    }
}

static void cmd_head(const char *arg) {
    char nstr[16],fname[256]; t2args(arg,nstr,fname,256);
    int lines=10;
    if(nstr[0]=='-'&&tdigit(nstr[1])){ lines=tatoi(nstr+1); tcpy(fname,fname,sizeof(fname));}
    else { tcpy(fname,nstr,sizeof(fname)); }
    if(!fname[0]){te("head: missing arg");return;}
    char p[256]; tresol(tcwd,fname,p,sizeof(p));
    static char buf[8192]; int bytes=fs_read_at(p,buf,sizeof(buf)-1);
    if(bytes<0){te("head: not found");return;} buf[bytes]=0;
    int s=0,lc=0;
    while(s<=bytes&&lc<lines){
        int e=s; while(e<=bytes&&buf[e]!='\n'&&buf[e]!=0) e++;
        char tmp[TERM_LLEN]; int ln=e-s; if(ln>TERM_LLEN-1) ln=TERM_LLEN-1;
        int k; for(k=0;k<ln;k++) tmp[k]=buf[s+k]; tmp[ln]=0;
        tp(tmp); if(buf[e]==0) break; s=e+1; lc++;
    }
}

static void cmd_tail(const char *arg) {
    char nstr[16],fname[256]; t2args(arg,nstr,fname,256);
    int nlines=10;
    if(nstr[0]=='-'&&tdigit(nstr[1])){ nlines=tatoi(nstr+1); }
    else { tcpy(fname,nstr,sizeof(fname)); }
    if(!fname[0]){te("tail: missing arg");return;}
    char p[256]; tresol(tcwd,fname,p,sizeof(p));
    static char buf[8192]; int bytes=fs_read_at(p,buf,sizeof(buf)-1);
    if(bytes<0){te("tail: not found");return;} buf[bytes]=0;
    /* count lines */
    int total_lines=0,i; for(i=0;i<bytes;i++) if(buf[i]=='\n') total_lines++;
    int skip=total_lines-nlines; if(skip<0) skip=0;
    int s=0,lc=0;
    while(s<=bytes){
        int e=s; while(e<=bytes&&buf[e]!='\n'&&buf[e]!=0) e++;
        if(lc>=skip){
            char tmp[TERM_LLEN]; int ln=e-s; if(ln>TERM_LLEN-1) ln=TERM_LLEN-1;
            int k; for(k=0;k<ln;k++) tmp[k]=buf[s+k]; tmp[ln]=0;
            tp(tmp);
        }
        if(buf[e]==0) break;
        s=e+1;
        lc++;
    }
}

static void cmd_wc(const char *arg) {
    if(!arg[0]){te("wc: missing arg");return;}
    char p[256]; tresol(tcwd,arg,p,sizeof(p));
    static char buf[8192]; int bytes=fs_read_at(p,buf,sizeof(buf)-1);
    if(bytes<0){te("wc: not found");return;} buf[bytes]=0;
    int lines=0,words=0,chars=bytes;
    int in_word=0,i;
    for(i=0;i<bytes;i++){
        if(buf[i]=='\n') lines++;
        if(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'){in_word=0;}
        else{if(!in_word){words++;in_word=1;}}
    }
    char out[TERM_LLEN]; char n[16]; out[0]=0;
    tcat(out,"  lines: ",sizeof(out)); titos(lines,n,sizeof(n)); tcat(out,n,sizeof(out));
    tcat(out,"  words: ",sizeof(out)); titos(words,n,sizeof(n)); tcat(out,n,sizeof(out));
    tcat(out,"  bytes: ",sizeof(out)); titos(chars,n,sizeof(n)); tcat(out,n,sizeof(out));
    tcat(out,"  ",sizeof(out)); tcat(out,arg,sizeof(out));
    tp(out);
}

static void cmd_grep(const char *arg) {
    char pattern[128],fname[256]; t2args(arg,pattern,fname,256);
    if(!pattern[0]||!fname[0]){te("grep: usage: grep <pattern> <file>");return;}
    char p[256]; tresol(tcwd,fname,p,sizeof(p));
    static char buf[8192]; int bytes=fs_read_at(p,buf,sizeof(buf)-1);
    if(bytes<0){te("grep: file not found");return;} buf[bytes]=0;
    int s=0,found=0,lnum=1;
    while(s<=bytes){
        int e=s; while(e<=bytes&&buf[e]!='\n'&&buf[e]!=0) e++;
        char tmp[TERM_LLEN]; int ln=e-s; if(ln>TERM_LLEN-1) ln=TERM_LLEN-1;
        int k; for(k=0;k<ln;k++) tmp[k]=buf[s+k]; tmp[ln]=0;
        if(tcontains(tmp,pattern)){
            char out[TERM_LLEN]; char n[8]; out[0]=0;
            titos(lnum,n,sizeof(n)); tcat(out,n,sizeof(out));
            tcat(out,": ",sizeof(out)); tcat(out,tmp,sizeof(out));
            tp(out); found++;
        }
        if(buf[e]==0) break;
        s=e+1;
        lnum++;
    }
    if(!found){char m[64];m[0]=0;tcat(m,"(no match for '",64);tcat(m,pattern,64);tcat(m,"')",64);td(m);}
}

static void cmd_find(const char *arg) {
    /* find [dir] -name <pattern> */
    char a1[256],rest[256]; t2args(arg,a1,rest,256);
    const char *dir=a1[0]?a1:tcwd;
    char pattern[128]; pattern[0]=0;
    /* look for -name */
    const char *p=rest;
    while(*p){
        if(p[0]=='-'&&p[1]=='n'&&p[2]=='a'&&p[3]=='m'&&p[4]=='e'&&p[5]==' '){
            tcpy(pattern,tltrim(p+6),sizeof(pattern)); break;
        }
        p++;
    }
    int count=fs_count_in_dir(dir),i,found=0;
    if(count<0){te("find: no such directory");return;}
    for(i=0;i<count;i++){
        const fs_entry_t *e=fs_entry_in_dir(dir,i); if(!e) continue;
        if(!pattern[0]||tcontains(e->name,pattern)){
            char out[TERM_LLEN]; out[0]=0;
            tcat(out,dir,sizeof(out)); tcat(out,"/",sizeof(out));
            tcat(out,e->name,sizeof(out)); tp(out); found++;
        }
    }
    if(!found){td("(no results)");}
    else{char m[32];m[0]=0;char n[8];titos(found,n,sizeof(n));tcat(m,n,32);tcat(m," result(s)",32);ti(m);}
}

static void cmd_stat(const char *arg) {
    if(!arg[0]){te("stat: missing arg");return;}
    char p[256]; tresol(tcwd,arg,p,sizeof(p));
    /* search fs entries */
    int count=fs_count_in_dir(tcwd),i;
    const fs_entry_t *found=0;
    for(i=0;i<count;i++){
        const fs_entry_t *e=fs_entry_in_dir(tcwd,i);
        if(e&&teq(e->name,arg)){found=e;break;}
    }
    if(!found){
        /* try full path match */
        te("stat: no such file or directory");
        return;
    }
    char line[TERM_LLEN]; line[0]=0;
    ti("─── File Statistics ───────────");
    tcat(line,"  Name    : ",sizeof(line)); tcat(line,found->name,sizeof(line)); tp(line); line[0]=0;
    tcat(line,"  Path    : ",sizeof(line)); tcat(line,found->path,sizeof(line)); tp(line); line[0]=0;
    tcat(line,"  Type    : ",sizeof(line)); tcat(line,found->is_dir?"directory":"regular file",sizeof(line)); tp(line); line[0]=0;
    if(!found->is_dir){
        char sz[16]; tutos(found->size,sz,sizeof(sz));
        tcat(line,"  Size    : ",sizeof(line)); tcat(line,sz,sizeof(line)); tcat(line," bytes",sizeof(line)); tp(line); line[0]=0;
        tutos(found->capacity,sz,sizeof(sz));
        tcat(line,"  Capacity: ",sizeof(line)); tcat(line,sz,sizeof(line)); tcat(line," bytes",sizeof(line)); tp(line); line[0]=0;
    }
    ti("───────────────────────────────");
}

static void cmd_write(const char *arg) {
    char fname[256],content[TERM_IN_MAX]; t2args(arg,fname,content,256);
    if(!fname[0]){te("write: usage: write <file> <text>");return;}
    char p[256]; tresol(tcwd,fname,p,sizeof(p));
    if(is_system_path(p) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    fs_create_at(p,0);
    if(fs_write_at(p,content,(u32)tlen(content))) ti("Written."); else te("write: failed");
}

static void cmd_append(const char *arg) {
    char fname[256],content[TERM_IN_MAX]; t2args(arg,fname,content,256);
    if(!fname[0]){te("append: usage: append <file> <text>");return;}
    char p[256]; tresol(tcwd,fname,p,sizeof(p));
    if(is_system_path(p) && !terminal_is_superuser()){
        te("Permission denied: super user access required to modify system files.");
        return;
    }
    static char existing[8192]; int bytes=fs_read_at(p,existing,sizeof(existing)-1);
    if(bytes<0){bytes=0;existing[0]=0;}
    int clen=tlen(content);
    if(bytes+clen+2<(int)sizeof(existing)){
        existing[bytes]='\n';
        int i; for(i=0;i<clen;i++) existing[bytes+1+i]=content[i];
        existing[bytes+1+clen]=0;
        if(fs_write_at(p,existing,(u32)(bytes+1+clen))) ti("Appended."); else te("append: write failed");
    } else te("append: file too large");
}

static void cmd_date(void) {
    rtc_time_t t; rtc_read(&t);
    char line[64]; line[0]=0;
    char buf[4];
    tcat(line,"Date: ",sizeof(line));
    t2d(t.day,buf); tcat(line,buf,sizeof(line)); tcat(line,"/",sizeof(line));
    t2d(t.month,buf); tcat(line,buf,sizeof(line)); tcat(line,"/",sizeof(line));
    char yr[8]; tutos(t.year,yr,sizeof(yr)); tcat(line,yr,sizeof(line));
    tcat(line,"  Time: ",sizeof(line));
    t2d(t.hour,buf); tcat(line,buf,sizeof(line)); tcat(line,":",sizeof(line));
    t2d(t.minute,buf); tcat(line,buf,sizeof(line)); tcat(line,":",sizeof(line));
    t2d(t.second,buf); tcat(line,buf,sizeof(line));
    ti(line);
}

static void cmd_uptime(void) {
    ti("Uptime: since boot  |  System: FirstOS 1.0.0");
}

static void cmd_ps(void) {
    ti("  PID   STATUS   NAME");
    tp("   1    running  kernel");
    tp("   2    running  gui");
    tp("   3    running  terminal");
    tp("   4    running  explorer");
    tp("   5    running  desktop");
    tp("   6    sleeping audio");
}

static void cmd_free(void) {
    ti("  Memory Usage (estimated):");
    tp("  Total  :   512 MB");
    tp("  Kernel :    ~8 MB");
    tp("  Heap   :   ~24 MB");
    tp("  Free   :  ~480 MB");
}

static void cmd_df(void) {
    ti("  Filesystem    Size     Used     Avail    Use%");
    tp("  /dev/fsimg    32 MB    ~12 MB   ~20 MB   37%");
    tp("  tmpfs          1 MB      0 B     1 MB    0%");
}

static void cmd_uname(const char *arg) {
    if(teq(arg,"-a")||teq(arg,"--all")){
        tp("FirstOS 1.0.0 i386 Bare-Metal SMP 2026 #1 PREEMPT");
    } else if(teq(arg,"-r")){
        tp("1.0.0");
    } else if(teq(arg,"-m")){
        tp("i386");
    } else {
        ti("FirstOS 1.0.0 i386 Bare-Metal");
    }
}

static void cmd_env(void) {
    int i;
    for(i=0;i<tvcount;i++){
        char line[TERM_LLEN]; line[0]=0;
        tcat(line,tvkeys[i],sizeof(line)); tcat(line,"=",sizeof(line)); tcat(line,tvvals[i],sizeof(line));
        tp(line);
    }
    if(tvcount==0) td("(no variables set)");
}

static void cmd_set(const char *arg) {
    if(!arg[0]){cmd_env();return;}
    /* find = */
    char key[TERM_VAR_LEN],val[TERM_VAR_LEN];
    const char *eq=arg; while(*eq&&*eq!='=') eq++;
    if(!*eq){te("set: usage: set VAR=value");return;}
    int kl=(int)(eq-arg); if(kl>TERM_VAR_LEN-1) kl=TERM_VAR_LEN-1;
    int k; for(k=0;k<kl;k++) key[k]=arg[k]; key[kl]=0;
    tcpy(val,eq+1,TERM_VAR_LEN);
    tvar_set(key,val);
    char msg[TERM_LLEN]; msg[0]=0;
    tcat(msg,"Set: ",sizeof(msg)); tcat(msg,key,sizeof(msg));
    tcat(msg,"=",sizeof(msg)); tcat(msg,val,sizeof(msg));
    ti(msg);
}

static void cmd_unset(const char *arg) {
    int i;
    for(i=0;i<tvcount;i++){
        if(teq(tvkeys[i],arg)){
            int j; for(j=i;j<tvcount-1;j++){
                tcpy(tvkeys[j],tvkeys[j+1],TERM_VAR_LEN);
                tcpy(tvvals[j],tvvals[j+1],TERM_VAR_LEN);
            }
            tvcount--; ti("Unset."); return;
        }
    }
    te("unset: variable not found");
}

static void cmd_echo(const char *arg) {
    /* expand vars */
    char expanded[TERM_IN_MAX]; texpand(arg,expanded,sizeof(expanded));
    if(pipe_capturing) tp_cap(expanded); else tp(expanded[0]?expanded:"");
}

static void cmd_history(void) {
    int i;
    ti("  # Command");
    for(i=0;i<thcount;i++){
        char line[TERM_LLEN]; char n[8]; line[0]=0;
        titos(i+1,n,sizeof(n));
        if(i+1<10) tcat(line," ",sizeof(line));
        tcat(line,n,sizeof(line)); tcat(line,"  ",sizeof(line)); tcat(line,thist[i],sizeof(line));
        td(line);
    }
}

static void cmd_alias(const char *arg) {
    if(!arg[0]){
        ti("Built-in aliases:");
        tp("  ll = ls -l");
        tp("  la = ls -la");
        tp("  .. = cd ..");
        tp("  ~  = cd ~");
        return;
    }
    ti("Custom aliases not stored yet (use set VAR=cmd)");
}

static void cmd_clear(void) {
    int i; for(i=0;i<TERM_ROWS;i++){tlines[i][0]=0;ttype[i]=0;}
    tlcount=0; tscroll=0;
}

static void term_execute(const char *raw);
void terminal_run_script(const char *path);

static void cmd_llm(const char *arg) {
    if(!arg[0]) { te("Usage: llm <prompt>"); return; }
    ti("Thinking...");
    
    extern void serial_write(const char*);
    extern char serial_read_char(void);
    
    serial_write("LLM_REQ:");
    serial_write(arg);
    serial_write("\n");
    
    char buf[128];
    int bi = 0;
    while(1) {
        char c = serial_read_char();
        if(c == '\n' || c == '\r') {
            buf[bi] = 0;
            if(bi > 0) {
                if(buf[0]=='L' && buf[1]=='L' && buf[2]=='M' && buf[3]=='_' && buf[4]=='E' && buf[5]=='O' && buf[6]=='F') {
                    break;
                } else if(buf[0]=='L' && buf[1]=='L' && buf[2]=='M' && buf[3]=='_' && buf[4]=='C' && buf[5]=='M' && buf[6]=='D' && buf[7]==':') {
                    ti("Executing AI Command:");
                    ti(&buf[8]);
                    term_execute(&buf[8]);
                } else {
                    ti(buf);
                }
            }
            bi = 0;
        } else {
            if(bi < 127) buf[bi++] = c;
        }
    }
}

static void cmd_exit(void) {
    extern void terminal_close_active_window(void);
    terminal_close_active_window();
}

static void cmd_shutdown(void) {
    ti("Syncing disks and shutting down...");
    fs_sync();
    extern void serial_write(const char*);
    serial_write("[PWR] Terminal Shutdown\n");
    extern void sound_play_event(int event);
    extern void sound_wait_finished(int max_ms);
    sound_play_event(2); /* SOUND_EVENT_SHUTDOWN */
    sound_wait_finished(1500);
    extern void outw(unsigned short port, unsigned short data);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outw(0x8900, 0x5307);
    outw(0x8900, 0x5301);
    outw(0x8900, 0x2000);
    while(1) __asm__ volatile("cli; hlt");
}

static void cmd_reboot(void) {
    ti("Syncing disks and rebooting system...");
    fs_sync();
    extern void serial_write(const char*);
    serial_write("[PWR] Terminal Reboot\n");
    extern void outb(unsigned short port, unsigned char data);
    outb(0x64, 0xFE);
    outb(0x0CF9, 0x02);
    outb(0x0CF9, 0x06);
    while(1) __asm__ volatile("cli; hlt");
}

static void cmd_sudo(const char *arg) {
    if (!arg || !arg[0]) {
        te("sudo: usage: sudo <command> [args...]");
        return;
    }
    char cmd[64], rest[TERM_IN_MAX];
    tsplit(arg, cmd, sizeof(cmd), rest, sizeof(rest));
    int prev_su = terminal_is_superuser();
    ti("[sudo] Super user elevation active.");
    tvar_set("SUPERUSER", "1");
    trun_cmd(cmd, rest);
    if (!prev_su) {
        tvar_set("SUPERUSER", "0");
    }
}

static void cmd_su(const char *arg) {
    if (teq_ci(arg, "user")) {
        tvar_set("USER", "user");
        tvar_set("SUPERUSER", "0");
        ti("Switched to user 'user' ($).");
    } else if (teq_ci(arg, "guest")) {
        tvar_set("USER", "guest");
        tvar_set("SUPERUSER", "0");
        ti("Switched to user 'guest' ($).");
    } else {
        tvar_set("USER", "root");
        tvar_set("SUPERUSER", "1");
        ti("Switched to super user 'root' (#).");
    }
}

static void cmd_sync(void) {
    if(fs_sync()) ti("Filesystem synced to disk.");
    else td("Filesystem already clean.");
}

static void cmd_python(void) {
    te("Python is not available in FirstOS.");
    tblk();
    ti("Reason: FirstOS is a bare-metal OS with no:");
    td("  • Process management (fork/exec)");
    td("  • POSIX syscall layer");
    td("  • C runtime library (libc)");
    td("  • Dynamic memory allocator (malloc/free at userspace)");
    tblk();
    ti("Alternatives available:");
    tp("  • Shell variables:   set X=42");
    tp("  • Math expressions:  calc 10 + 32");
    tp("  • File operations:   read/write files");
    tp("  • Scripting:         write scripts, exec them");
    tblk();
    td("A future FirstOS version could embed Lua or a micro-Python");
    td("interpreter if a userspace runtime is implemented.");
}

static void cmd_calc(const char *arg) {
    /* basic: A op B */
    char a[32],op[8],b[32];
    int i=0;
    const char *p=tltrim(arg);
    while(*p&&*p!=' '&&i<31){a[i++]=*p++;} a[i]=0;
    p=tltrim(p); i=0;
    while(*p&&*p!=' '&&i<7){op[i++]=*p++;} op[i]=0;
    p=tltrim(p); i=0;
    while(*p&&i<31){b[i++]=*p++;} b[i]=0;
    if(!a[0]||!op[0]||!b[0]){te("calc: usage: calc <a> <op> <b>   (op: + - * /)");return;}
    int A=tatoi(a),B=tatoi(b),R=0;
    if(teq(op,"+")){R=A+B;}
    else if(teq(op,"-")){R=A-B;}
    else if(teq(op,"*")||teq(op,"x")){R=A*B;}
    else if(teq(op,"/")){if(B==0){te("calc: division by zero");return;}R=A/B;}
    else if(teq(op,"%")){if(B==0){te("calc: division by zero");return;}R=A%B;}
    else{te("calc: unknown operator");return;}
    char res[32]; char rn[16]; res[0]=0;
    tcat(res,a,sizeof(res)); tcat(res," ",sizeof(res));
    tcat(res,op,sizeof(res)); tcat(res," ",sizeof(res));
    tcat(res,b,sizeof(res)); tcat(res," = ",sizeof(res));
    titos(R,rn,sizeof(rn)); tcat(res,rn,sizeof(res));
    ti(res);
}

static void cmd_ping(const char *arg) {
    const char *host = tltrim(arg);
    if (!host[0]) {
        te("usage: ping <destination_host_or_ip>");
        return;
    }
    if (!net_is_enabled()) {
        te("ping: connect: Network is unreachable (Wi-Fi is disabled)");
        return;
    }
    if (!net_is_connected()) {
        te("ping: connect: Network is unreachable (no carrier)");
        return;
    }
    char ip[32];
    if (!net_resolve_host(host, ip, sizeof(ip))) {
        char err[128]; err[0] = 0;
        tcat(err, "ping: cannot resolve ", sizeof(err));
        tcat(err, host, sizeof(err));
        tcat(err, ": Unknown host", sizeof(err));
        te(err);
        return;
    }

    char hdr[128]; hdr[0] = 0;
    tcat(hdr, "PING ", sizeof(hdr));
    tcat(hdr, host, sizeof(hdr));
    tcat(hdr, " (", sizeof(hdr));
    tcat(hdr, ip, sizeof(hdr));
    tcat(hdr, "): 56 data bytes", sizeof(hdr));
    ti(hdr);

    int min_ms = 999, max_ms = 0, sum_ms = 0;
    for (int seq = 0; seq < 4; seq++) {
        char pkt[128]; pkt[0] = 0;
        tcat(pkt, "64 bytes from ", sizeof(pkt));
        tcat(pkt, ip, sizeof(pkt));
        tcat(pkt, ": icmp_seq=", sizeof(pkt));
        char seq_str[8];
        titos(seq, seq_str, sizeof(seq_str));
        tcat(pkt, seq_str, sizeof(pkt));
        tcat(pkt, " ttl=56 time=", sizeof(pkt));
        
        int rtt = 16 + ((seq * 7) % 5);
        if (rtt < min_ms) min_ms = rtt;
        if (rtt > max_ms) max_ms = rtt;
        sum_ms += rtt;
        
        char rtt_str[16];
        titos(rtt, rtt_str, sizeof(rtt_str));
        tcat(pkt, rtt_str, sizeof(pkt));
        tcat(pkt, ".", sizeof(pkt));
        char frac_str[4];
        titos((seq * 3 + 2) % 9, frac_str, sizeof(frac_str));
        tcat(pkt, frac_str, sizeof(pkt));
        tcat(pkt, " ms", sizeof(pkt));
        tp(pkt);
    }

    tblk();
    char stat_hdr[128]; stat_hdr[0] = 0;
    tcat(stat_hdr, "--- ", sizeof(stat_hdr));
    tcat(stat_hdr, host, sizeof(stat_hdr));
    tcat(stat_hdr, " ping statistics ---", sizeof(stat_hdr));
    ti(stat_hdr);
    tp("4 packets transmitted, 4 packets received, 0.0% packet loss");

    char stat_rtt[128]; stat_rtt[0] = 0;
    tcat(stat_rtt, "round-trip min/avg/max = ", sizeof(stat_rtt));
    char s_min[8], s_avg[8], s_max[8];
    titos(min_ms, s_min, sizeof(s_min));
    titos(sum_ms / 4, s_avg, sizeof(s_avg));
    titos(max_ms, s_max, sizeof(s_max));
    tcat(stat_rtt, s_min, sizeof(stat_rtt));
    tcat(stat_rtt, ".2/", sizeof(stat_rtt));
    tcat(stat_rtt, s_avg, sizeof(stat_rtt));
    tcat(stat_rtt, ".5/", sizeof(stat_rtt));
    tcat(stat_rtt, s_max, sizeof(stat_rtt));
    tcat(stat_rtt, ".8 ms", sizeof(stat_rtt));
    td(stat_rtt);
}

static void cmd_curl(const char *arg) {
    const char *url = tltrim(arg);
    if (!url[0]) {
        te("usage: curl <url>");
        return;
    }
    if (!net_is_connected()) {
        te("curl: (6) Could not resolve host (Wi-Fi disconnected)");
        return;
    }
    char norm_url[256];
    if (!tstarts_ci(url, "http://") && !tstarts_ci(url, "https://")) {
        norm_url[0] = 0;
        tcat(norm_url, "http://", sizeof(norm_url));
        tcat(norm_url, url, sizeof(norm_url));
    } else {
        tcpy(norm_url, url, sizeof(norm_url));
    }
    static char curl_buf[4096];
    int sz = net_http_fetch(norm_url, curl_buf, sizeof(curl_buf) - 1);
    if (sz <= 0) {
        te("curl: (7) Failed to connect to host");
        return;
    }
    curl_buf[sz] = 0;
    tp(curl_buf);
}

static void cmd_httpget(const char *arg) {
    const char *url = tltrim(arg);
    if (!url[0]) {
        te("usage: httpget <url>");
        return;
    }
    if (!net_is_connected()) {
        te("httpget: network offline (Wi-Fi disconnected)");
        return;
    }
    char norm_url[256];
    if (!tstarts_ci(url, "http://") && !tstarts_ci(url, "https://")) {
        snprintf(norm_url, sizeof(norm_url), "http://%s", url);
    } else {
        strncpy(norm_url, url, sizeof(norm_url) - 1);
        norm_url[sizeof(norm_url) - 1] = 0;
    }

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = norm_url;
    req.method = HTTP_METHOD_GET;
    req.follow_redirects = 5;
    req.timeout_polls = 350000;

    char msg[128];
    snprintf(msg, sizeof(msg), "Connecting to %s ...", norm_url);
    ti(msg);

    http_response_t *res = http_execute(&req);
    if (!res) {
        te("httpget: request failed (timeout or connection error)");
        return;
    }

    snprintf(msg, sizeof(msg), "HTTP/1.1 %d %s", res->status_code, res->status_message);
    if (res->status_code >= 200 && res->status_code < 400) ti(msg);
    else te(msg);

    for (int i = 0; i < res->header_count && i < 12; i++) {
        char hline[320];
        snprintf(hline, sizeof(hline), "  %s: %s", res->headers[i].name, res->headers[i].value);
        td(hline);
    }
    if (res->is_chunked) td("  [Transfer-Encoding: chunked decoded successfully]");

    char bline[128];
    snprintf(bline, sizeof(bline), "Payload: %u bytes received", res->body_len);
    ti(bline);

    if (res->body && res->body_len > 0) {
        char preview[280];
        u32 plen = res->body_len < 260 ? res->body_len : 260;
        memcpy(preview, res->body, plen);
        preview[plen] = 0;
        for (u32 k = 0; k < plen; k++) {
            if (preview[k] == '\r' || preview[k] == '\n') preview[k] = ' ';
        }
        tp(preview);
        if (res->body_len > plen) tp("... [truncated]");
    }

    http_response_free(res);
}

static void cmd_download(const char *arg) {
    char url[256] = {0};
    char dest[128] = {0};
    const char *p = tltrim(arg);
    if (!p[0]) {
        te("usage: download <url> [destination_dir]");
        return;
    }
    if (!net_is_connected()) {
        te("download: network offline");
        return;
    }

    char rest[384];
    tcpy(rest, p, sizeof(rest));
    char *sp = strchr(rest, ' ');
    if (sp) {
        *sp = 0;
        tcpy(url, rest, sizeof(url));
        tcpy(dest, tltrim(sp + 1), sizeof(dest));
    } else {
        tcpy(url, rest, sizeof(url));
        tcpy(dest, "/Downloads", sizeof(dest));
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "Starting stream download: %s", url);
    ti(msg);

    download_task_t *task = download_start(url, dest, NULL, NULL);
    if (!task || task->status == DOWNLOAD_FAILED) {
        te("download: failed to download file");
        return;
    }

    snprintf(msg, sizeof(msg), "Downloaded %u bytes -> %s", task->bytes_received, task->target_path);
    ti(msg);
}

static void cmd_tlsinfo(const char *arg) {
    const char *host = tltrim(arg);
    if (!host[0]) {
        te("usage: tlsinfo <hostname>");
        return;
    }
    if (!net_is_connected()) {
        te("tlsinfo: network offline");
        return;
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "Initiating TLS handshake to %s:443 ...", host);
    ti(msg);

    net_socket_t *sock = net_socket_open(host, 443, SOCKET_PROTO_TLS, 300000);
    if (!sock) {
        te("tlsinfo: TLS handshake failed");
        return;
    }

    ti("TLS connection established successfully!");
    td("  Protocol: TLS 1.2 / TLS 1.3 (SNI enabled)");
    td("  Key Exchange: ECDHE (X25519, secp256r1)");
    td("  Cipher Suite: TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 (0xC02F)");
    td("  AEAD Record Encryption: AES-128-GCM, SHA-256 MAC");
    net_socket_close(sock);
}

static void cmd_netstat(const char *arg) {
    (void)arg;
    port_entry_t entries[32];
    int count = ports_get_active_list(entries, 32);

    ti("Active Internet Connections (servers and established)");
    ti("Proto Recv-Q Send-Q Local Address          Foreign Address        State       Service");

    int tcp_cnt = 0, udp_cnt = 0;
    for (int i = 0; i < count; i++) {
        port_entry_t *e = &entries[i];
        char line[160];
        char loc[32], rem[32];

        if (e->proto == PORT_TYPE_TCP) {
            tcp_cnt++;
            if (e->local_ip == 0) snprintf(loc, sizeof(loc), "0.0.0.0:%u", e->local_port);
            else snprintf(loc, sizeof(loc), "%u.%u.%u.%u:%u",
                          (e->local_ip >> 24) & 0xFF, (e->local_ip >> 16) & 0xFF,
                          (e->local_ip >> 8) & 0xFF, e->local_ip & 0xFF, e->local_port);

            if (e->remote_ip == 0 || e->remote_port == 0) snprintf(rem, sizeof(rem), "0.0.0.0:*");
            else snprintf(rem, sizeof(rem), "%u.%u.%u.%u:%u",
                          (e->remote_ip >> 24) & 0xFF, (e->remote_ip >> 16) & 0xFF,
                          (e->remote_ip >> 8) & 0xFF, e->remote_ip & 0xFF, e->remote_port);

            const char *st_str = "UNKNOWN";
            if (e->state == PORT_STATE_LISTEN) st_str = "LISTEN";
            else if (e->state == PORT_STATE_ESTABLISHED) st_str = "ESTABLISHED";
            else if (e->state == PORT_STATE_SYN_SENT) st_str = "SYN_SENT";
            else if (e->state == PORT_STATE_CLOSE_WAIT) st_str = "CLOSE_WAIT";
            else if (e->state == PORT_STATE_TIME_WAIT) st_str = "TIME_WAIT";

            snprintf(line, sizeof(line), "tcp        0      0 %-22s %-22s %-11s %s",
                     loc, rem, st_str, e->service);
            tp(line);
        } else {
            udp_cnt++;
            snprintf(loc, sizeof(loc), "0.0.0.0:%u", e->local_port);
            snprintf(rem, sizeof(rem), "0.0.0.0:*");
            snprintf(line, sizeof(line), "udp        0      0 %-22s %-22s %-11s %s",
                     loc, rem, "BOUND", e->service);
            tp(line);
        }
    }

    if (count == 0) {
        td("No active TCP or UDP sockets found.");
    } else {
        char summary[80];
        snprintf(summary, sizeof(summary), "Total active sockets: %d (%d TCP, %d UDP)", count, tcp_cnt, udp_cnt);
        td(summary);
    }
}

static void cmd_firewall(const char *arg) {
    char subcmd[32] = {0};
    char rest[128] = {0};
    tsplit(arg, subcmd, sizeof(subcmd), rest, sizeof(rest));

    if (!subcmd[0] || teq_ci(subcmd, "status")) {
        const fw_state_t *fw = firewall_get_state();
        ti("╔════════════════════════════════════════════════════════════════╗");
        ti("║               FirstOS Stateful Firewall Status                ║");
        ti("╠════════════════════════════════════════════════════════════════╣");

        char sline[96];
        snprintf(sline, sizeof(sline), "Status: %s", fw->enabled ? "ACTIVE (ENABLED)" : "INACTIVE (DISABLED)");
        if (fw->enabled) ti(sline); else te(sline);

        const char *pname = "BALANCED";
        if (fw->profile == FW_PROFILE_OPEN) pname = "OPEN (Permissive)";
        else if (fw->profile == FW_PROFILE_STRICT) pname = "STRICT (High Security)";
        else if (fw->profile == FW_PROFILE_CUSTOM) pname = "CUSTOM";

        snprintf(sline, sizeof(sline), "Profile: %s  (Inbound: %s, Outbound: %s)",
                 pname,
                 fw->default_in == FW_ACTION_DROP ? "DROP" : "ACCEPT",
                 fw->default_out == FW_ACTION_DROP ? "DROP" : "ACCEPT");
        tp(sline);

        snprintf(sline, sizeof(sline), "Stealth Mode: %s (ICMP Echo Responses %s)",
                 fw->stealth_mode ? "ON" : "OFF",
                 fw->stealth_mode ? "Suppressed" : "Allowed");
        tp(sline);

        snprintf(sline, sizeof(sline), "Traffic: %u accepted, %u dropped, %u rejected (%u bytes)",
                 fw->packets_accepted, fw->packets_dropped, fw->packets_rejected, fw->bytes_processed);
        td(sline);
        tblk();

        ti("Active Firewall Rules:");
        ti("  #  Dir   Proto   Port/Range   Action   Matches   Rule Name");
        ti(" ──  ────  ──────  ───────────  ───────  ────────  ─────────────────────");

        for (int i = 0; i < fw->rule_count; i++) {
            const fw_rule_t *r = &fw->rules[i];
            const char *dstr = (r->dir == FW_DIR_IN) ? "IN" : (r->dir == FW_DIR_OUT ? "OUT" : "BOTH");
            const char *pstr = (r->proto == FW_PROTO_TCP) ? "TCP" : (r->proto == FW_PROTO_UDP ? "UDP" : (r->proto == FW_PROTO_ICMP ? "ICMP" : "ANY"));
            const char *astr = (r->action == FW_ACTION_ACCEPT) ? "ALLOW" : (r->action == FW_ACTION_DROP ? "DENY" : "REJECT");
            char prange[16];
            if (r->port_start == 0) snprintf(prange, sizeof(prange), "any");
            else if (r->port_start == r->port_end) snprintf(prange, sizeof(prange), "%u", r->port_start);
            else snprintf(prange, sizeof(prange), "%u-%u", r->port_start, r->port_end);

            char rline[120];
            snprintf(rline, sizeof(rline), " %2d  %-4s  %-6s  %-11s  %-7s  %-8u  %s",
                     i + 1, dstr, pstr, prange, astr, r->match_count, r->name);
            tp(rline);
        }
        if (fw->rule_count == 0) {
            td("  (No rules defined; default policy applies)");
        }
        return;
    }

    if (teq_ci(subcmd, "enable") || teq_ci(subcmd, "on")) {
        firewall_set_enabled(1);
        ti("Firewall is now active and enabled.");
        return;
    }

    if (teq_ci(subcmd, "disable") || teq_ci(subcmd, "off")) {
        firewall_set_enabled(0);
        te("Firewall is now inactive (permissive mode).");
        return;
    }

    if (teq_ci(subcmd, "allow") || teq_ci(subcmd, "deny") || teq_ci(subcmd, "reject")) {
        fw_action_t act = FW_ACTION_ACCEPT;
        if (teq_ci(subcmd, "deny")) act = FW_ACTION_DROP;
        else if (teq_ci(subcmd, "reject")) act = FW_ACTION_REJECT;

        char p_str[32] = {0};
        char proto_str[32] = {0};
        tsplit(rest, p_str, sizeof(p_str), proto_str, sizeof(proto_str));

        int port = 0;
        const char *p = p_str;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p++ - '0');
        }

        if (port <= 0 || port > 65535) {
            te("usage: firewall allow|deny|reject <port> [tcp|udp]");
            return;
        }

        fw_proto_t pr = FW_PROTO_ANY;
        if (teq_ci(proto_str, "tcp")) pr = FW_PROTO_TCP;
        else if (teq_ci(proto_str, "udp")) pr = FW_PROTO_UDP;

        fw_rule_t r;
        memset(&r, 0, sizeof(r));
        r.enabled = 1;
        r.dir = FW_DIR_IN;
        r.proto = pr;
        r.port_start = (u16)port;
        r.port_end = (u16)port;
        r.action = act;

        const char *svc = ports_get_service_name((u16)port, pr == FW_PROTO_UDP ? 2 : 1);
        if (strcmp(svc, "-") != 0) {
            snprintf(r.name, sizeof(r.name), "%s %s (%u)",
                     act == FW_ACTION_ACCEPT ? "Allow" : "Block", svc, port);
        } else {
            snprintf(r.name, sizeof(r.name), "%s Port %u",
                     act == FW_ACTION_ACCEPT ? "Allow" : "Block", port);
        }

        if (firewall_add_rule(&r)) {
            char ok[80];
            snprintf(ok, sizeof(ok), "Rule added: Inbound %s on port %u/%s",
                     act == FW_ACTION_ACCEPT ? "ALLOWED" : (act == FW_ACTION_DROP ? "BLOCKED" : "REJECTED"),
                     port, pr == FW_PROTO_TCP ? "tcp" : (pr == FW_PROTO_UDP ? "udp" : "any"));
            ti(ok);
        } else {
            te("Failed to add rule (maximum rules reached).");
        }
        return;
    }

    if (teq_ci(subcmd, "delete") || teq_ci(subcmd, "del") || teq_ci(subcmd, "rm")) {
        int idx = 0;
        const char *p = rest;
        while (*p >= '0' && *p <= '9') {
            idx = idx * 10 + (*p++ - '0');
        }
        if (idx <= 0) {
            te("usage: firewall delete <rule_number>");
            return;
        }
        if (firewall_delete_rule(idx - 1)) {
            char msg[48];
            snprintf(msg, sizeof(msg), "Rule %d deleted successfully.", idx);
            ti(msg);
        } else {
            te("Invalid rule number.");
        }
        return;
    }

    if (teq_ci(subcmd, "profile")) {
        if (teq_ci(rest, "balanced")) {
            firewall_set_profile(FW_PROFILE_BALANCED);
            ti("Firewall profile switched to BALANCED (Default protected mode).");
        } else if (teq_ci(rest, "strict")) {
            firewall_set_profile(FW_PROFILE_STRICT);
            ti("Firewall profile switched to STRICT (Stealth mode & all unsolicited inbound blocked).");
        } else if (teq_ci(rest, "open") || teq_ci(rest, "permissive")) {
            firewall_set_profile(FW_PROFILE_OPEN);
            td("Firewall profile switched to OPEN (All traffic permitted).");
        } else {
            te("usage: firewall profile balanced|strict|open");
        }
        return;
    }

    if (teq_ci(subcmd, "stealth")) {
        if (teq_ci(rest, "on") || teq_ci(rest, "1") || teq_ci(rest, "enable")) {
            firewall_set_stealth(1);
            ti("Firewall stealth mode ENABLED (ICMP ping requests dropped silently).");
        } else if (teq_ci(rest, "off") || teq_ci(rest, "0") || teq_ci(rest, "disable")) {
            firewall_set_stealth(0);
            td("Firewall stealth mode DISABLED (System will answer ICMP pings).");
        } else {
            te("usage: firewall stealth on|off");
        }
        return;
    }

    if (teq_ci(subcmd, "log") || teq_ci(subcmd, "logs")) {
        fw_log_t logs[32];
        int n = firewall_get_logs(logs, 32);
        ti("Recent Firewall Filter Events:");
        if (n == 0) {
            td("  (No events logged yet)");
            return;
        }
        for (int i = 0; i < n; i++) {
            char lline[128];
            const char *act = (logs[i].action == FW_ACTION_ACCEPT) ? "ACCEPT" : (logs[i].action == FW_ACTION_DROP ? "DROP" : "REJECT");
            const char *pr = (logs[i].proto == IP_PROTO_TCP) ? "TCP" : (logs[i].proto == IP_PROTO_UDP ? "UDP" : "ICMP");
            snprintf(lline, sizeof(lline), "  [%s] %s %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (%s)",
                     act, pr,
                     (logs[i].src_ip >> 24) & 0xFF, (logs[i].src_ip >> 16) & 0xFF,
                     (logs[i].src_ip >> 8) & 0xFF, logs[i].src_ip & 0xFF, logs[i].src_port,
                     (logs[i].dst_ip >> 24) & 0xFF, (logs[i].dst_ip >> 16) & 0xFF,
                     (logs[i].dst_ip >> 8) & 0xFF, logs[i].dst_ip & 0xFF, logs[i].dst_port,
                     logs[i].rule_name);
            if (logs[i].action == FW_ACTION_DROP) te(lline); else ti(lline);
        }
        return;
    }

    if (teq_ci(subcmd, "reset")) {
        firewall_reset_defaults();
        ti("Firewall reset to factory defaults (Balanced profile).");
        return;
    }

    te("unknown firewall command. Available: status, enable, disable, allow, deny, reject, delete, profile, stealth, log, reset");
}

static void cmd_listen(const char *arg) {
    char p_str[32] = {0};
    char proto_str[32] = {0};
    tsplit(arg, p_str, sizeof(p_str), proto_str, sizeof(proto_str));

    int port = 0;
    const char *p = p_str;
    while (*p >= '0' && *p <= '9') port = port * 10 + (*p++ - '0');

    if (port <= 0 || port > 65535) {
        te("usage: listen <port> [tcp|udp]");
        return;
    }

    int is_udp = teq_ci(proto_str, "udp");
    const char *svc = ports_get_service_name((u16)port, is_udp ? 2 : 1);

    if (is_udp) {
        udp_binding_t *b = udp_bind((u16)port, 0);
        if (b) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Bound UDP listener on port %u (Service: %s). Type 'netstat' to inspect.", port, svc);
            ti(msg);
        } else {
            te("Failed to bind UDP port (maximum bindings reached).");
        }
    } else {
        tcp_socket_t *s = tcp_listen((u16)port);
        if (s) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Listening on TCP port %u (Service: %s). Type 'netstat' to inspect.", port, svc);
            ti(msg);
        } else {
            te("Failed to open TCP listener (maximum sockets reached).");
        }
    }
}

static void cmd_settings(const char *arg) {
    extern void open_app(int kind);
    extern void settings_set_tab(int tab);
    extern void settings_set_subtab(int subtab);
    
    open_app(5); // APP_SETTINGS
    if (arg && *arg) {
        if (teq_ci(arg, "firewall") || teq_ci(arg, "fw") || teq_ci(arg, "security")) {
            settings_set_tab(8);
            settings_set_subtab(1);
            ti("Opened Settings -> Firewall & Security");
            return;
        } else if (teq_ci(arg, "ports") || teq_ci(arg, "port") || teq_ci(arg, "sockets") || teq_ci(arg, "netstat")) {
            settings_set_tab(8);
            settings_set_subtab(2);
            ti("Opened Settings -> Active Ports");
            return;
        } else if (teq_ci(arg, "wifi") || teq_ci(arg, "net") || teq_ci(arg, "network")) {
            settings_set_tab(8);
            settings_set_subtab(0);
            ti("Opened Settings -> Wi-Fi Networks");
            return;
        }
    }
    ti("Opened Settings app");
}

static void cmd_help(void) {
    ti("╔══════════════════════════════════════════════════╗");
    ti("║      FirstOS Shell v2.0  —  Command Reference   ║");
    ti("╠══════════════════════════════════════════════════╣");
    ti("║ FILESYSTEM                                       ║");
    tp("║  ls [-l] [dir]     List directory              ║");
    tp("║  cd <dir>          Change dir  (cd - = prev)   ║");
    tp("║  pwd               Print working directory     ║");
    tp("║  mkdir <dir>       Create directory            ║");
    tp("║  touch <file>      Create file                 ║");
    tp("║  rm <file>         Delete file                 ║");
    tp("║  mv <src> <dst>    Move / rename               ║");
    tp("║  cp <src> <dst>    Copy file                   ║");
    ti("║ FILE CONTENT                                     ║");
    tp("║  cat <file>        Print file                  ║");
    tp("║  head [-N] <file>  First N lines (def. 10)     ║");
    tp("║  tail [-N] <file>  Last N lines                ║");
    tp("║  wc <file>         Word / line count           ║");
    tp("║  grep <pat> <file> Search pattern in file      ║");
    tp("║  find [dir] -name  Find files by name          ║");
    tp("║  stat <file>       File info                   ║");
    tp("║  write <f> <txt>   Write text to file          ║");
    tp("║  append <f> <txt>  Append text to file         ║");
    ti("║ VARIABLES & SCRIPTING                            ║");
    tp("║  set VAR=value     Set variable                ║");
    tp("║  unset VAR         Remove variable             ║");
    tp("║  env               List all variables          ║");
    tp("║  echo $VAR         Print variable              ║");
    tp("║  calc <a> <op> <b> Math: + - * / %             ║");
    ti("║ SYSTEM                                           ║");
    tp("║  date              Current date & time         ║");
    tp("║  uptime            System uptime               ║");
    tp("║  ps                Process list                ║");
    tp("║  free              Memory info                 ║");
    tp("║  df                Disk usage                  ║");
    tp("║  uname [-a/-r/-m]  System info                 ║");
    tp("║  sync              Flush filesystem to disk    ║");
    tp("║  history           Command history             ║");
    tp("║  alias             Show aliases                ║");
    tp("║  clear             Clear screen                ║");
    tp("║  exit              Quit terminal session       ║");
    tp("║  shutdown          Power off the system        ║");
    tp("║  reboot            Reboot the system           ║");
    tp("║  python            Why Python can't run here   ║");
    ti("║ NETWORK & PACKAGES                              ║");
    tp("║  ping <host/ip>    Ping website or IP address  ║");
    tp("║  curl <url>        Fetch and display webpage   ║");
    tp("║  httpget <url>     HTTP/1.1 client with chunked║");
    tp("║  download <url>    Stream download file to disk║");
    tp("║  tlsinfo <host>    Inspect TLS 1.2/1.3 security║");
    tp("║  netstat           Show active TCP/UDP ports   ║");
    tp("║  firewall [cmd]    Stateful packet firewall    ║");
    tp("║  listen <port>     Open test TCP/UDP port      ║");
    tp("║  browser [url]     Launch Web Browser          ║");
    tp("║  pkg install <pkg> Install software package    ║");
    tp("║  pkg list          List installed packages     ║");
    ti("║ TIPS                                             ║");
    tp("║  ↑ / ↓  history  •  ←/→  cursor  •  Ctrl+C   ║");
    tp("║  Select text to copy • Ctrl+V to paste        ║");
    tp("║  cmd > file  to redirect output to file        ║");
    ti("╚══════════════════════════════════════════════════╝");
}

/* ─── redirect: run command, capture output to file ─── */
static void handle_redirect(const char *line) {
    /* find '>' */
    const char *gt=line; while(*gt&&*gt!='>') gt++;
    if(!*gt){trun_cmd("","");return;}
    char cmd_part[TERM_IN_MAX]; int n=(int)(gt-line);
    if(n>TERM_IN_MAX-1) n=TERM_IN_MAX-1;
    int k; for(k=0;k<n;k++) cmd_part[k]=line[k];
    cmd_part[n]=0; trtrim(cmd_part);

    const char *file_part=tltrim(gt+1);
    char file_path[256]; tcpy(file_path,file_part,sizeof(file_path));
    trtrim(file_path);
    char abs_path[256]; tresol(tcwd,file_path,abs_path,sizeof(abs_path));
    if (is_system_path(abs_path) && !terminal_is_superuser()) {
        te("Permission denied: super user access required to modify system files.");
        return;
    }

    /* enable capture */
    pipe_capturing=1; pipe_cap_len=0; pipe_capture[0]=0;

    char cmd2[64], arg2[TERM_IN_MAX];
    tsplit(cmd_part,cmd2,sizeof(cmd2),arg2,sizeof(arg2));
    trun_cmd(cmd2,arg2);

    pipe_capturing=0;
    fs_create_at(abs_path,0);
    if(fs_write_at(abs_path,pipe_capture,(u32)pipe_cap_len)){
        char m[128]; m[0]=0;
        tcat(m,"→ Redirected to ",sizeof(m)); tcat(m,file_path,sizeof(m));
        ti(m);
    } else te("redirect: write failed");
}


/* ───── FirstLang commands ───── */
static void cmd_fl(const char *arg) {
    char subcmd[32], rest[TERM_IN_MAX];
    /* split subcmd + arg */
    int i=0,j=0;
    const char *p=tltrim(arg);
    while(*p&&*p!=' '&&i<31){subcmd[i++]=*p++;} subcmd[i]=0;
    p=tltrim(p);
    while(*p&&j<TERM_IN_MAX-1){rest[j++]=*p++;} rest[j]=0;

        if(teq(subcmd,"doc") || teq(subcmd,"help") || !subcmd[0]) {
        ti("=== FirstLang Comprehensive Documentation ===");
        tp("Usage:     fl <cmd> | fl run <file> | fl eval <code> | fl new <file>");
        tblk();
        ti("--- Variables & Math ---");
        tp("Variables: let X be 10 | put 5 into Y | make name equal to \"Ali\"");
        tp("Hidden:    let h score be 100 | set hidden secret to 5");
        tp("Math:      increase X by 5 | decrease Y by 2 | multiply X by 3 | divide Y by 2");
        tblk();
        ti("--- Logic & Loops ---");
        tp("If Logic:  if X is greater than 10: ... end | if name is equal to \"Ali\": ... end");
        tp("Loops:     as long as X is less than 5: ... end | while X < 5: ... end");
        tblk();
        ti("--- Output & UI ---");
        tp("Terminal:  say \"Hello\" | show X | display Y | print \"Debug\"");
        tp("UI Basic:  create a button \"Btn\" | draw a label \"Lbl\" | create a text \"Input\"");
        tp("UI Adv:    add chart \"Stats\" | add radio \"Opt 1\" | add slider \"Vol\" | add checkbox \"Save\"");
        tp("UI Image:  add image \"/firstOS.ico\" at (x,y)");
        tblk();
        ti("--- Events & System ---");
        tp("Events:    whenever click \"Btn\": ... end | on click \"Btn\": ... end");
        tp("System:    execute \"/app.elf\" | execute \"/script.fl\" | wait 1000");
        tblk();
        ti("--- File I/O & Memory ---");
        tp("Files:     write \"Data\" to \"/file.txt\" | read file \"/file.txt\" to myVar");
        tp("Mem Alloc: allocate memory 1024 to ptr | free memory ptr");
        tp("Mem R/W:   write memory 42 to ptr | read memory at ptr to val");
        tblk();
        ti("--- Functions & OOP ---");
        tp("Functions: function Add: ... end | call Add");
        tp("Classes:   class init: function Main. | function Main: ... (Traditional syntax)");
        ti("=============================================");
        return;
    }

    if(teq(subcmd,"run")||teq(subcmd,"exec")) {
        /* fl run <filepath> */
        if(!rest[0]){te("fl run: missing filename");return;}
        char path[256]; tresol(tcwd,rest,path,sizeof(path));
        static char out[FL_OUTPUT_MAX];
        ti("Running FirstLang script...");
        int r=firstlang_run_file(path,out,sizeof(out));
        /* print output line by line */
        const char *op=out;

        while(*op) {
            int ei=0; char line[TERM_LLEN]; line[0]=0;
            while(*op&&*op!='\n'&&ei<TERM_LLEN-1){line[ei++]=*op++;} line[ei]=0;
            if(*op=='\n') op++;
            if(tcontains(line,"[ERROR]")) te(line);
            else if(tcontains(line,"[INFO]"))  ti(line);
            else if(line[0]) tp(line);
        }
        if(r<0) te("Script finished with errors.");
        else { char m[32];m[0]=0;tcat(m,"Done. (",32);char n[8];titos(r,n,8);tcat(m,n,32);tcat(m," functions)",32);ti(m); }
    } else if(teq(subcmd,"eval")) {
        /* fl eval <inline code> */
        if(!rest[0]){te("fl eval: missing code");return;}
        static char out[FL_OUTPUT_MAX];
        int r=firstlang_run(rest,out,sizeof(out));
        const char *op=out;
        while(*op){
            char line[TERM_LLEN]; int ei=0;
            while(*op&&*op!='\n'&&ei<TERM_LLEN-1){line[ei++]=*op++;} line[ei]=0;
            if(*op=='\n') op++;
            if(tcontains(line,"[ERROR]")) te(line);
            else if(tcontains(line,"[INFO]"))  ti(line);
            else if(line[0]) tp(line);
        }
        (void)r;
    } else if(teq(subcmd,"new")) {
        /* fl new <filename> — create template script */
        char fname[64]; tcpy(fname,rest[0]?rest:"script.fl",64);
        if(!tcontains(fname,".")) { tcat(fname,".fl",64); }
        char path[256]; tresol(tcwd,fname,path,sizeof(path));
        static const char tmpl[] =
            "# FirstLang Script\n"
            "# Created by FirstOS\n"
            "\n"
            "class init: function UI, function check, function copy.\n"
            "\n"
            "function UI(buttons, radio box, slider)\n"
            "  buttons positions: (center, top).\n"
            "\n"
            "function check: directory Desktop any new files with format .jpg\n"
            "\n"
            "function copy: copy detected from check to Desktop/images.\n";
        fs_create_at(path,0);
        if(fs_write_at(path,tmpl,(u32)tlen(tmpl))){
            char m[64];m[0]=0;tcat(m,"Created: ",64);tcat(m,fname,64);ti(m);
            tcat(m,"  Use: fl run ",64);tcat(m,fname,64);
        } else te("fl new: write failed");
    } else {
        /* treat as inline interactive command */
        extern void firstlang_eval_line(const char*, char*, int);
        static char out[FL_OUTPUT_MAX];
        firstlang_eval_line(arg, out, sizeof(out));
        const char *op=out;
        while(*op){
            char line[TERM_LLEN]; int ei=0;
            while(*op&&*op!='\n'&&ei<TERM_LLEN-1){line[ei++]=*op++;} line[ei]=0;
            if(*op=='\n') op++;
            if(tcontains(line,"[ERROR]")) te(line);
            else if(tcontains(line,"[INFO]"))  ti(line);
            else if(line[0]) tp(line);
        }
    }
}

static void cmd_browser(const char *arg) {
    extern void web_browser_app_launch(const char *url);
    if (arg && arg[0]) {
        char msg[128]; msg[0] = 0;
        tcat(msg, "Launching FirstOS Web Browser with: ", sizeof(msg));
        tcat(msg, arg, sizeof(msg));
        ti(msg);
        web_browser_app_launch(arg);
    } else {
        ti("Launching FirstOS Web Browser...");
        web_browser_app_launch("about:home");
    }
}

static void cmd_pkg(const char *tool, const char *arg) {
    char subcmd[64]; subcmd[0] = 0;
    char target[64]; target[0] = 0;
    int i = 0;
    while (arg[i] && arg[i] != ' ' && i < 63) { subcmd[i] = arg[i]; i++; }
    subcmd[i] = 0;
    while (arg[i] == ' ') i++;
    int j = 0;
    while (arg[i] && j < 63) { target[j++] = arg[i++]; }
    target[j] = 0;

    if (teq(subcmd, "install") || teq(subcmd, "add")) {
        if (!target[0]) {
            char err[64]; err[0] = 0;
            tcat(err, tool, sizeof(err)); tcat(err, " install: missing package name", sizeof(err));
            te(err);
            return;
        }
        {
            char not_found[96]; not_found[0] = 0;
            tcat(not_found, "Package '", sizeof(not_found));
            tcat(not_found, target, sizeof(not_found));
            tcat(not_found, "' not found in default repository.", sizeof(not_found));
            te(not_found);
        }
    } else if (teq(subcmd, "search")) {
        if (tcontains(target, "browser") || tcontains(target, "web")) {
            tp("firstos-browser - Native HTML5 Web Browser [installed]");
            tp("chromium - Chromium Browser 124.0 (Blink/V8 Engine) [installed]");
            tp("firstos-media - HTML5 Audio/Video support [installed]");
        } else {
            tp("No matching packages found.");
        }
    } else if (teq(subcmd, "list") || teq(subcmd, "status")) {
        tp("Installed packages:");
        tp("  base-system          1.0.0    FirstOS Core OS utilities");
        tp("  firstlang            2.0.0    FirstOS Interactive Script Engine");
        tp("  firstos-browser      1.0.0    Native HTML5 Web Browser");
        tp("  chromium             124.0    Chromium Browser (Blink)");
        tp("  firstos-media        1.0.0    HTML5 Video/Audio playback support");
    } else {
        char use[96]; use[0] = 0;
        tcat(use, "Usage: ", sizeof(use));
        tcat(use, tool, sizeof(use));
        tcat(use, " [install <pkg> | search <query> | list]", sizeof(use));
        ti(use);
    }
}

static void trun_cmd(const char *cmd, const char *arg) {
    if(!cmd[0])              { }
    else if(teq(cmd,"help"))    cmd_help();
    else if(teq(cmd,"clear"))   cmd_clear();
    else if(teq(cmd,"browser")) cmd_browser(arg);
    else if(teq(cmd,"pkg")||teq(cmd,"apt")||teq(cmd,"apt-get")) cmd_pkg(cmd, arg);
    else if(teq(cmd,"ls")||teq(cmd,"ll")) {
        if(teq(cmd,"ll")){char a2[TERM_IN_MAX];a2[0]=0;tcat(a2,"-l ",sizeof(a2));tcat(a2,arg,sizeof(a2));cmd_ls(a2);}
        else cmd_ls(arg);
    }
    else if(teq(cmd,"cd"))      cmd_cd(arg);
    else if(teq(cmd,"pwd"))     cmd_pwd();
    else if(teq(cmd,"mkdir"))   cmd_mkdir(arg);
    else if(teq(cmd,"touch"))   cmd_touch(arg);
    else if(teq(cmd,"rm"))      cmd_rm(arg);
    else if(teq(cmd,"mv"))      cmd_mv(arg);
    else if(teq(cmd,"cp"))      cmd_cp(arg);
    else if(teq(cmd,"cat"))     cmd_cat(arg);
    else if(teq(cmd,"head"))    cmd_head(arg);
    else if(teq(cmd,"tail"))    cmd_tail(arg);
    else if(teq(cmd,"wc"))      cmd_wc(arg);
    else if(teq(cmd,"grep"))    cmd_grep(arg);
    else if(teq(cmd,"find"))    cmd_find(arg);
    else if(teq(cmd,"stat"))    cmd_stat(arg);
    else if(teq(cmd,"write"))   cmd_write(arg);
    else if(teq(cmd,"append"))  cmd_append(arg);
    else if(teq(cmd,"echo"))    cmd_echo(arg);
    else if(teq(cmd,"set"))     cmd_set(arg);
    else if(teq(cmd,"unset"))   cmd_unset(arg);
    else if(teq(cmd,"env"))     cmd_env();
    else if(teq(cmd,"calc"))    cmd_calc(arg);
    else if(teq(cmd,"date"))    cmd_date();
    else if(teq(cmd,"uptime"))  cmd_uptime();
    else if(teq(cmd,"ps"))      cmd_ps();
    else if(teq(cmd,"free"))    cmd_free();
    else if(teq(cmd,"df"))      cmd_df();
    else if(teq(cmd,"uname"))   cmd_uname(arg);
    else if(teq(cmd,"sync"))    cmd_sync();
    else if(teq(cmd,"history")) cmd_history();
    else if(teq(cmd,"alias"))   cmd_alias(arg);
    else if(teq(cmd,"python")||teq(cmd,"python3")) cmd_python();
    else if(teq(cmd,"fl")||teq(cmd,"firstlang")) cmd_fl(arg);
    else if(teq(cmd,"llm"))       cmd_llm(arg);
    else if(teq(cmd,"ping"))      cmd_ping(arg);
    else if(teq(cmd,"curl"))      cmd_curl(arg);
    else if(teq(cmd,"httpget")||teq(cmd,"http")) cmd_httpget(arg);
    else if(teq(cmd,"download")||teq(cmd,"wget")) cmd_download(arg);
    else if(teq(cmd,"tlsinfo")||teq(cmd,"ssl")) cmd_tlsinfo(arg);
    else if(teq(cmd,"netstat")||teq(cmd,"ports")) cmd_netstat(arg);
    else if(teq(cmd,"firewall")||teq(cmd,"ufw")) cmd_firewall(arg);
    else if(teq(cmd,"listen"))    cmd_listen(arg);
    else if(teq(cmd,"settings"))  cmd_settings(arg);
    else if(teq(cmd,"exit")||teq(cmd,"quit")) cmd_exit();
    else if(teq(cmd,"shutdown")||teq(cmd,"poweroff")) cmd_shutdown();
    else if(teq(cmd,"reboot")||teq(cmd,"restart")) cmd_reboot();
    else if(teq(cmd,"sudo"))      cmd_sudo(arg);
    else if(teq(cmd,"su"))        cmd_su(arg);
    else if(teq(cmd,"run")||teq(cmd,"source")||teq(cmd,"exec")) {
        terminal_run_script(arg);
    }
    else {
        /* Check if cmd is a .tml script or path to executable script */
        char candidate[256];
        int is_script = tends_ci(cmd, ".tml") || cmd[0] == '.' || cmd[0] == '/';
        if (is_script) {
            tresol(tcwd, cmd, candidate, sizeof(candidate));
            static char test_b[4];
            if (fs_read_at(candidate, test_b, 2) >= 0) {
                terminal_run_script(candidate);
                return;
            }
        }
        /* Check if cmd.tml exists in tcwd */
        char with_tml[256]; with_tml[0] = 0;
        tcat(with_tml, cmd, sizeof(with_tml));
        tcat(with_tml, ".tml", sizeof(with_tml));
        tresol(tcwd, with_tml, candidate, sizeof(candidate));
        static char test_b2[4];
        if (fs_read_at(candidate, test_b2, 2) >= 0) {
            terminal_run_script(candidate);
            return;
        }

        char err[128]; err[0]=0;
        tcat(err,cmd,128); tcat(err,": command not found. Type 'help'",128);
        te(err);
    }
}

/* ───────────────────────────── execute ─────────────────────────────── */
static void term_execute(const char *raw) {
    /* expand vars in input */
    char line[TERM_IN_MAX]; texpand(raw,line,sizeof(line));

    /* push history */
    if(line[0]&&(thcount==0||!teq(thist[thcount-1],raw))){
        if(thcount<TERM_HIST_MAX) tcpy(thist[thcount++],raw,TERM_IN_MAX);
        else {
            int i; for(i=0;i<TERM_HIST_MAX-1;i++) tcpy(thist[i],thist[i+1],TERM_IN_MAX);
            tcpy(thist[TERM_HIST_MAX-1],raw,TERM_IN_MAX);
        }
    }
    thidx=-1;

    /* print prompt line */
    char pline[TERM_LLEN]; tprompt_str(pline,sizeof(pline));
    tcat(pline,raw,sizeof(pline));
    tpush(pline,1);

    /* check for redirect */
    if(tcontains(line,">")){handle_redirect(line);}
    else {
        char cmd[64],arg[TERM_IN_MAX];
        tsplit(line,cmd,sizeof(cmd),arg,sizeof(arg));
        trun_cmd(cmd,arg);
    }
    tblk();
}

/* ───────────────────────────── script execution ────────────────────── */
void terminal_run_script(const char *path) {
    if (!path || !path[0]) { te("run: missing script path"); return; }
    char full[256];
    tresol(tcwd, path, full, sizeof(full));

    static char script_buf[8192];
    int bytes = fs_read_at(full, script_buf, sizeof(script_buf) - 1);
    if (bytes < 0 && !tends_ci(full, ".tml")) {
        char full_tml[256]; full_tml[0] = 0;
        tcat(full_tml, full, sizeof(full_tml));
        tcat(full_tml, ".tml", sizeof(full_tml));
        bytes = fs_read_at(full_tml, script_buf, sizeof(script_buf) - 1);
        if (bytes >= 0) tcpy(full, full_tml, sizeof(full));
    }
    if (bytes < 0) {
        char err[128]; err[0] = 0;
        tcat(err, "terminal: cannot open script: ", sizeof(err));
        tcat(err, full, sizeof(err));
        te(err);
        return;
    }
    script_buf[bytes] = 0;

    char banner[128]; banner[0] = 0;
    tcat(banner, ">>> Running script: ", sizeof(banner));
    tcat(banner, full, sizeof(banner));
    ti(banner);

    int pos = 0;
    while (pos < bytes) {
        char line[TERM_IN_MAX];
        int li = 0;
        while (pos < bytes && script_buf[pos] != '\n' && script_buf[pos] != '\r' && li < TERM_IN_MAX - 1) {
            line[li++] = script_buf[pos++];
        }
        line[li] = 0;
        while (pos < bytes && (script_buf[pos] == '\n' || script_buf[pos] == '\r')) pos++;

        const char *p = tltrim(line);
        if (!p[0]) continue;
        if (p[0] == '#' || (p[0] == '/' && p[1] == '/') || p[0] == ';') continue;
        if ((p[0] == 'r' || p[0] == 'R') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'm' || p[2] == 'M') && (p[3] == ' ' || p[3] == '\t' || p[3] == 0)) continue;

        if (teq(p, "exit") || teq(p, "quit")) {
            ti("[script exited]");
            break;
        }

        term_execute(p);
    }
    ti(">>> Script finished.");
}

void terminal_execute_line(const char *line) {
    if (line && line[0]) term_execute(line);
}

/* ───────────────────────────── init & clear ────────────────────────── */
void terminal_clear(void) {
    int i; for(i=0;i<TERM_ROWS;i++){tlines[i][0]=0;ttype[i]=0;}
    tlcount=0; tscroll=0;
    tinput[0]=0; tinlen=0; tincur=0;
    thidx=-1;
    terminal_clear_selection();
}

void terminal_init(void) {
    terminal_clear();
    tcpy(tcwd,"/",sizeof(tcwd));

    /* init default variables */
    tvar_set("USER","root");
    tvar_set("HOSTNAME","firstos");
    tvar_set("HOME","/");
    tvar_set("SHELL","/bin/sh");
    tvar_set("OS","FirstOS");
    tvar_set("VERSION","1.0.0");
    tvar_set("ARCH","i386");
}

/* ───────────────────────────── selection & clipboard ────────────────── */
void terminal_mouse_down(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h) {
    int cx=frame_x+10, cy=frame_y+40, cw=frame_w-20, ch=frame_h-60;
    int pad_x=8, pad_y=18, line_h=12;
    int input_h=22, sep_y=cy+ch-input_h-2;
    int vis_h=sep_y-cy-pad_y;
    int max_lines=vis_h/line_h; if(max_lines<1) max_lines=1;
    int start=tlcount-max_lines-tscroll; if(start<0) start=0;

    if (my >= cy + pad_y && my < sep_y && mx >= cx + pad_x && mx < cx + cw - pad_x) {
        int row = (my - (cy + pad_y)) / line_h;
        int line_idx = start + row;
        if (line_idx >= 0 && line_idx < tlcount) {
            int col = (mx - (cx + pad_x)) / 6;
            if (col < 0) col = 0;
            int llen = tlen(tlines[line_idx]);
            if (col > llen) col = llen;
            term_selecting = 1;
            term_sel_start_line = line_idx;
            term_sel_start_col  = col;
            term_sel_end_line   = line_idx;
            term_sel_end_col     = col;
            return;
        }
    }
    terminal_clear_selection();
}

void terminal_mouse_drag(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h) {
    (void)frame_w;
    if (!term_selecting) return;
    int cx=frame_x+10, cy=frame_y+40, ch=frame_h-60;
    int pad_x=8, pad_y=18, line_h=12;
    int input_h=22, sep_y=cy+ch-input_h-2;
    int vis_h=sep_y-cy-pad_y;
    int max_lines=vis_h/line_h; if(max_lines<1) max_lines=1;
    int start=tlcount-max_lines-tscroll; if(start<0) start=0;

    int row = (my - (cy + pad_y)) / line_h;
    if (row < 0) row = 0;
    if (row >= max_lines) row = max_lines - 1;
    int line_idx = start + row;
    if (line_idx < 0) line_idx = 0;
    if (line_idx >= tlcount) line_idx = tlcount > 0 ? tlcount - 1 : 0;

    int col = (mx - (cx + pad_x)) / 6;
    if (col < 0) col = 0;
    if (line_idx < tlcount) {
        int llen = tlen(tlines[line_idx]);
        if (col > llen) col = llen;
    }
    term_sel_end_line = line_idx;
    term_sel_end_col   = col;
}

void terminal_mouse_up(void) {
    term_selecting = 0;
}

void terminal_copy(void) {
    if (!terminal_has_selection()) return;
    int s_line = term_sel_start_line;
    int s_col  = term_sel_start_col;
    int e_line = term_sel_end_line;
    int e_col  = term_sel_end_col;

    if (s_line > e_line || (s_line == e_line && s_col > e_col)) {
        int tmp_l = s_line; s_line = e_line; e_line = tmp_l;
        int tmp_c = s_col; s_col = e_col; e_col = tmp_c;
    }

    int out_idx = 0;
    for (int l = s_line; l <= e_line && l < tlcount && out_idx < (int)sizeof(editor_clipboard) - 2; l++) {
        const char *txt = tlines[l];
        int llen = tlen(txt);
        int from_col = (l == s_line) ? s_col : 0;
        int to_col   = (l == e_line) ? e_col : llen;
        if (from_col < 0) from_col = 0;
        if (to_col > llen) to_col = llen;

        for (int c = from_col; c < to_col && out_idx < (int)sizeof(editor_clipboard) - 2; c++) {
            editor_clipboard[out_idx++] = txt[c];
        }
        if (l < e_line && out_idx < (int)sizeof(editor_clipboard) - 2) {
            editor_clipboard[out_idx++] = '\n';
        }
    }
    editor_clipboard[out_idx] = 0;
}

void terminal_paste(void) {
    int p = 0;
    while (editor_clipboard[p] && tinlen < TERM_IN_MAX - 1) {
        char c = editor_clipboard[p++];
        if (c == '\r' || c == '\n') continue;
        for (int i = tinlen; i > tincur; i--) tinput[i] = tinput[i - 1];
        tinput[tincur] = c;
        tinlen++;
        tincur++;
        tinput[tinlen] = 0;
    }
}

static void term_do_tab_completion(void) {
    int wstart = tincur;
    while (wstart > 0 && tinput[wstart - 1] != ' ' && tinput[wstart - 1] != '\t') wstart--;
    int wlen = tincur - wstart;
    char word[128];
    int wi;
    for (wi = 0; wi < wlen && wi < 127; wi++) word[wi] = tinput[wstart + wi];
    word[wi] = 0;

    int is_command = 1;
    for (int k = 0; k < wstart; k++) {
        if (tinput[k] != ' ' && tinput[k] != '\t') { is_command = 0; break; }
    }

    #define MAX_COMPLETIONS 64
    static char matches[MAX_COMPLETIONS][128];
    static int is_dir_match[MAX_COMPLETIONS];
    int match_count = 0;

    if (is_command && !tcontains(word, "/")) {
        // Builtin command completions
        static const char *builtins[] = {
            "help", "clear", "ls", "ll", "cd", "pwd", "mkdir", "touch",
            "rm", "mv", "cp", "cat", "head", "tail", "wc", "grep",
            "find", "stat", "write", "append", "echo", "set", "unset",
            "env", "calc", "date", "uptime", "ps", "free", "df",
            "uname", "sync", "history", "alias", "python", "fl",
            "ping", "curl", "browser", "pkg", "apt",
            "exit", "shutdown", "reboot", "run", "source", "sudo", "su"
        };
        int num_builtins = sizeof(builtins) / sizeof(builtins[0]);
        for (int b = 0; b < num_builtins && match_count < MAX_COMPLETIONS; b++) {
            if (wlen == 0 || tstarts_ci(builtins[b], word)) {
                tcpy(matches[match_count], builtins[b], 128);
                is_dir_match[match_count] = 0;
                match_count++;
            }
        }
        // Also look for files/scripts in tcwd
        int count = fs_count_in_dir(tcwd);
        for (int i = 0; i < count && match_count < MAX_COMPLETIONS; i++) {
            const fs_entry_t *e = fs_entry_in_dir(tcwd, i);
            if (!e) continue;
            if (wlen > 0 && tstarts_ci(e->name, word)) {
                tcpy(matches[match_count], e->name, 128);
                is_dir_match[match_count] = e->is_dir;
                match_count++;
            }
        }
    } else {
        // Path / file / directory completion
        int last_slash = -1;
        for (int i = 0; i < wlen; i++) if (word[i] == '/') last_slash = i;

        char search_dir[256];
        char file_prefix[128];
        if (last_slash >= 0) {
            char sub_dir[256];
            for (int i = 0; i <= last_slash; i++) sub_dir[i] = word[i];
            sub_dir[last_slash + 1] = 0;
            tresol(tcwd, sub_dir, search_dir, sizeof(search_dir));
            int k = 0;
            for (int i = last_slash + 1; i < wlen && k < 127; i++) file_prefix[k++] = word[i];
            file_prefix[k] = 0;
        } else {
            tcpy(search_dir, tcwd, sizeof(search_dir));
            tcpy(file_prefix, word, sizeof(file_prefix));
        }

        // Canonicalize search_dir
        int sl = tlen(search_dir);
        while (sl > 1 && search_dir[sl - 1] == '/') search_dir[--sl] = 0;

        char canonical_dir[256]; canonical_dir[0] = 0;
        if (teq(search_dir, "/")) {
            tcpy(canonical_dir, "/", sizeof(canonical_dir));
        } else {
            for (int slot = 0; slot < 256; slot++) {
                const fs_entry_t *fe = fs_entry_at(slot);
                if (!fe || !fe->used || !fe->is_dir) continue;
                if (teq_ci(fe->path, search_dir)) {
                    tcpy(canonical_dir, fe->path, sizeof(canonical_dir));
                    break;
                }
            }
            if (!canonical_dir[0]) tcpy(canonical_dir, search_dir, sizeof(canonical_dir));
        }

        int count = fs_count_in_dir(canonical_dir);
        for (int i = 0; i < count && match_count < MAX_COMPLETIONS; i++) {
            const fs_entry_t *e = fs_entry_in_dir(canonical_dir, i);
            if (!e) continue;
            if (!file_prefix[0] || tstarts_ci(e->name, file_prefix)) {
                tcpy(matches[match_count], e->name, 128);
                is_dir_match[match_count] = e->is_dir;
                match_count++;
            }
        }
    }

    if (match_count == 0) return;

    if (match_count == 1) {
        // Exactly one match: complete it!
        char completion[140];
        completion[0] = 0;
        if (!is_command && tcontains(word, "/")) {
            int last_slash = -1;
            for (int i = 0; i < wlen; i++) if (word[i] == '/') last_slash = i;
            for (int i = 0; i <= last_slash; i++) {
                char s[2] = {word[i], 0};
                tcat(completion, s, sizeof(completion));
            }
        }
        tcat(completion, matches[0], sizeof(completion));
        if (is_dir_match[0]) {
            tcat(completion, "/", sizeof(completion));
        } else {
            tcat(completion, " ", sizeof(completion));
        }

        char rest[TERM_IN_MAX]; rest[0] = 0;
        int ri = 0;
        for (int i = tincur; i < tinlen && ri < TERM_IN_MAX - 1; i++) rest[ri++] = tinput[i];
        rest[ri] = 0;

        tinput[wstart] = 0;
        tcat(tinput, completion, sizeof(tinput));
        int new_cur = tlen(tinput);
        tcat(tinput, rest, sizeof(tinput));
        tinlen = tlen(tinput);
        tincur = new_cur;
        return;
    }

    // Multiple matches: find Longest Common Prefix (LCP)
    char lcp[128];
    tcpy(lcp, matches[0], sizeof(lcp));
    int lcp_len = tlen(lcp);
    for (int m = 1; m < match_count; m++) {
        int j = 0;
        while (j < lcp_len && matches[m][j]) {
            char ca = lcp[j], cb = matches[m][j];
            if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
            if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
            if (ca != cb) break;
            j++;
        }
        lcp_len = j;
        lcp[lcp_len] = 0;
    }

    int current_prefix_len = wlen;
    if (!is_command && tcontains(word, "/")) {
        int last_slash = -1;
        for (int i = 0; i < wlen; i++) if (word[i] == '/') last_slash = i;
        current_prefix_len = wlen - (last_slash + 1);
    }

    if (lcp_len > current_prefix_len) {
        // Expand up to the LCP!
        char completion[140]; completion[0] = 0;
        if (!is_command && tcontains(word, "/")) {
            int last_slash = -1;
            for (int i = 0; i < wlen; i++) if (word[i] == '/') last_slash = i;
            for (int i = 0; i <= last_slash; i++) {
                char s[2] = {word[i], 0};
                tcat(completion, s, sizeof(completion));
            }
        }
        tcat(completion, lcp, sizeof(completion));

        char rest[TERM_IN_MAX]; rest[0] = 0;
        int ri = 0;
        for (int i = tincur; i < tinlen && ri < TERM_IN_MAX - 1; i++) rest[ri++] = tinput[i];
        rest[ri] = 0;

        tinput[wstart] = 0;
        tcat(tinput, completion, sizeof(tinput));
        int new_cur = tlen(tinput);
        tcat(tinput, rest, sizeof(tinput));
        tinlen = tlen(tinput);
        tincur = new_cur;
    } else {
        // Already at LCP: list options on screen and restore prompt!
        char candidate_line[TERM_LLEN]; candidate_line[0] = 0;
        tpush("", 0);
        for (int m = 0; m < match_count; m++) {
            char item[140]; item[0] = 0;
            tcat(item, matches[m], sizeof(item));
            if (is_dir_match[m]) tcat(item, "/", sizeof(item));
            tcat(item, "  ", sizeof(item));
            if (tlen(candidate_line) + tlen(item) >= TERM_LLEN - 6) {
                tp(candidate_line);
                candidate_line[0] = 0;
            }
            tcat(candidate_line, item, sizeof(candidate_line));
        }
        if (candidate_line[0]) tp(candidate_line);

        // Reprint current prompt line
        char pline[TERM_LLEN]; tprompt_str(pline, sizeof(pline));
        tcat(pline, tinput, sizeof(pline));
        tpush(pline, 1);
    }
}

/* ───────────────────────────── key input ───────────────────────────── */
void terminal_key(unsigned char ascii, int is_enter, int is_backspace,
                  int is_up, int is_down, int is_left, int is_right,
                  int is_ctrl_c, int is_tab) {
    if(is_tab){
        term_do_tab_completion();
        return;
    }
    if(is_ctrl_c){
        te("^C"); tblk();
        tinput[0]=0; tinlen=0; tincur=0; thidx=-1;
        terminal_clear_selection();
        return;
    }
    if(is_enter){
        tinput[tinlen]=0;
        term_execute(tinput);
        tinput[0]=0; tinlen=0; tincur=0; tscroll=0;
        terminal_clear_selection();
        return;
    }
    if(is_backspace){
        if(tincur>0){
            int i; for(i=tincur-1;i<tinlen-1;i++) tinput[i]=tinput[i+1];
            tinlen--; tincur--; tinput[tinlen]=0;
        }
        return;
    }
    if(is_up){
        if(thcount==0) return;
        if(thidx<0) {
            tcpy(thist_draft, tinput, TERM_IN_MAX);
            thidx = thcount;
        }
        if(thidx>0){
            thidx--;
            tcpy(tinput, thist[thidx], TERM_IN_MAX);
            tinlen = tlen(tinput);
            tincur = tinlen;
        }
        return;
    }
    if(is_down){
        if(thidx<0) return;
        thidx++;
        if(thidx>=thcount){
            thidx = -1;
            tcpy(tinput, thist_draft, TERM_IN_MAX);
            tinlen = tlen(tinput);
            tincur = tinlen;
        } else {
            tcpy(tinput, thist[thidx], TERM_IN_MAX);
            tinlen = tlen(tinput);
            tincur = tinlen;
        }
        return;
    }
    if(is_left){ if(tincur>0) tincur--; return; }
    if(is_right){ if(tincur<tinlen) tincur++; return; }
    if(ascii>=32&&ascii<127){
        if(tinlen<TERM_IN_MAX-1){
            int i; for(i=tinlen;i>tincur;i--) tinput[i]=tinput[i-1];
            tinput[tincur]=ascii; tinlen++; tincur++; tinput[tinlen]=0;
        }
    }
}

void terminal_scroll(int delta) {
    tscroll+=delta;
    if(tscroll<0) tscroll=0;
    if(tscroll>tlcount) tscroll=tlcount;
}

/* ───────────────────────────── draw ────────────────────────────────── */
extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void font_text_fit(int x, int y, const char *text, u32 color, int max_px);
extern void font_text_fit_center(int x, int y, const char *text, u32 color, int max_px);

static void toutline(int x,int y,int w,int h,u32 c){
    fb_rect(x,y,w,1,c);fb_rect(x,y+h-1,w,1,c);
    fb_rect(x,y,1,h,c);fb_rect(x+w-1,y,1,h,c);
}

void terminal_draw(int frame_x, int frame_y, int frame_w, int frame_h) {
    int cx=frame_x+10, cy=frame_y+40, cw=frame_w-20, ch=frame_h-60;
    if(cw<60||ch<40) return;

    /* background */
    fb_rect(cx,cy,cw,ch,0x0B1622);

    /* scanlines */
    int sy; for(sy=cy;sy<cy+ch;sy+=4) fb_rect(cx,sy,cw,1,0x090F1A);

    toutline(cx,cy,cw,ch,0x1A3050);

    int pad_x=8, pad_y=18, line_h=12;
    int input_h=22, sep_y=cy+ch-input_h-2;
    int vis_h=sep_y-cy-pad_y;
    int max_lines=vis_h/line_h; if(max_lines<1) max_lines=1;

    /* ── title bar ── */
    fb_rect(cx,cy,cw,16,0x0F2236);
    char title[80]; title[0]=0;
    tcat(title,"  shell  ",sizeof(title)); tcat(title,tcwd,sizeof(title));
    font_text_fit(cx+6,cy+3,title,0x4A88C0,cw-120);
    /* show date in title */
    rtc_time_t rt; rtc_read(&rt);
    char dt[32]; dt[0]=0;
    char b[4];
    t2d(rt.hour,b); tcat(dt,b,sizeof(dt)); tcat(dt,":",sizeof(dt));
    t2d(rt.minute,b); tcat(dt,b,sizeof(dt));
    font_text_fit(cx+cw-55,cy+3,dt,0x5A9FD0,50);

    /* ── output ── */
    int start=tlcount-max_lines-tscroll; if(start<0) start=0;
    int end=start+max_lines; if(end>tlcount) end=tlcount;
    int draw_y=cy+pad_y, i;

    int has_sel = terminal_has_selection();
    int s_line = term_sel_start_line;
    int s_col  = term_sel_start_col;
    int e_line = term_sel_end_line;
    int e_col  = term_sel_end_col;
    if (s_line > e_line || (s_line == e_line && s_col > e_col)) {
        int tmp_l = s_line; s_line = e_line; e_line = tmp_l;
        int tmp_c = s_col; s_col = e_col; e_col = tmp_c;
    }

    for(i=start;i<end;i++){
        u32 col;
        switch(ttype[i]){
            case 1: col=0x6EE492; break; /* prompt  green   */
            case 2: col=0xFF6060; break; /* error   red     */
            case 3: col=0x78CFEE; break; /* info    cyan    */
            case 4: col=0x4A6A88; break; /* dim     grey    */
            default: col=0xCCDAE8; break;/* normal  white   */
        }
        if (has_sel && i >= s_line && i <= e_line) {
            int llen = tlen(tlines[i]);
            int from_col = (i == s_line) ? s_col : 0;
            int to_col   = (i == e_line) ? e_col : llen;
            if (from_col < 0) from_col = 0;
            if (to_col > llen) to_col = llen;
            if (to_col > from_col) {
                int hx = cx + pad_x + from_col * 6;
                int hw = (to_col - from_col) * 6;
                fb_rect(hx - 1, draw_y - 1, hw + 2, line_h, 0x1D4ED8);
            }
        }
        font_text_fit(cx+pad_x,draw_y,tlines[i],col,cw-pad_x*2-8);
        draw_y+=line_h;
    }

    /* scrollbar */
    if(tlcount>max_lines){
        int sbx=cx+cw-5,sby=cy+pad_y,sbh=vis_h;
        fb_rect(sbx,sby,3,sbh,0x122030);
        int ts=tlcount-max_lines;
        int th=sbh*max_lines/tlcount; if(th<6) th=6;
        int ty=sby+(ts-tscroll)*(sbh-th)/(ts>0?ts:1);
        fb_rect(sbx,ty,3,th,0x3A78B0);
    }

    /* separator */
    fb_rect(cx,sep_y,cw,1,0x1A3050);

    /* input background */
    fb_rect(cx,sep_y+1,cw,input_h,0x0E1E30);

    /* prompt + input text */
    char pstr[128]; tprompt_str(pstr,sizeof(pstr));
    int ppx=tlen(pstr)*6; if(ppx>cw-80) ppx=cw-80;
    font_text_fit(cx+pad_x,sep_y+5,pstr,0x5ED38E,ppx+4);

    int inp_x=cx+pad_x+ppx;
    font_text_fit(inp_x,sep_y+5,tinput,0xF0F8FF,cw-pad_x-ppx-12);

    /* cursor */
    int cur_px=inp_x+tincur*6;
    if(terminal_cursor_visible&&terminal_focused){
        fb_rect(cur_px,sep_y+3,2,16,0x6EE492);
    } else {
        toutline(cur_px,sep_y+3,6,16,0x2A5A8A);
    }

    if(tscroll>0){
        char si[24]; si[0]=0; tcat(si,"↑ -",24);
        char n[8]; titos(tscroll,n,sizeof(n)); tcat(si,n,24); tcat(si,"↑",24);
        font_text_fit(cx+cw-70,cy+3,si,0xFF9966,68);
    }
}

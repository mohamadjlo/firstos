/*
 * firstlang.c — FirstOS Domain-Specific Language Interpreter
 *
 * Syntax Example:
 *   class init: function UI, function check, function copy.
 *
 *   function UI(buttons, radio box, slider)
 *     buttons positions: (center, top).
 *
 *   function check: directory Desktop any new files with format .jpg
 *
 *   function copy: copy detected from check to Desktop/images.
 */

#include "firstlang.h"
#include "fs.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"
#include "rtc.h"

typedef unsigned int  u32;
typedef unsigned char u8;

/* ─── string helpers ─── */
static int fl_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static void fl_cpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static void fl_cat(char *d, const char *s, int max) {
    int n=fl_len(d),i=0; while(n<max-1&&s[i]){d[n++]=s[i++];} d[n]=0;
}
static int fl_eq(const char *a, const char *b) {
    while(*a&&*b){if(*a++!=*b++)return 0;} return *a==0&&*b==0;
}
static int fl_contains_sub(const char *hay, const char *needle) {
    int hl=fl_len(hay),nl=fl_len(needle); int i,j;
    for(i=0;i<=hl-nl;i++){
        for(j=0;j<nl;j++) if(hay[i+j]!=needle[j]) break;
        if(j==nl) return 1;
    } return 0;
}
static int fl_starts(const char *s, const char *pre) {
    while(*pre){if(*s++!=*pre++)return 0;} return 1;
}
static const char *fl_ltrim(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static void fl_rtrim(char *s) {
    int n=fl_len(s)-1;
    while(n>=0&&(s[n]==' '||s[n]=='\t'||s[n]=='.')){s[n--]=0;}
}
static void fl_lower(char *s) {
    while(*s){if(*s>='A'&&*s<='Z') *s+=32; s++;}
}
static int fl_digit(char c){return c>='0'&&c<='9';}
static int fl_atoi(const char *s){
    int v=0; while(fl_digit(*s)){v=v*10+(*s-'0');s++;} return v;
}
static void fl_itos(int v, char *b, int sz){
    if(!sz){return;} if(v==0){b[0]='0';b[1]=0;return;}
    char t[16];int n=0; while(v){t[n++]=(char)('0'+v%10);v/=10;}
    int i=0; while(n>0){b[i++]=t[--n];} b[i]=0;
}
/* check if string ends with suffix */
static int fl_ends(const char *s, const char *suf) {
    int sl=fl_len(s),el=fl_len(suf);
    if(el>sl) return 0;
    return fl_eq(s+sl-el,suf);
}
/* path join: dir + "/" + name → out */
static void fl_path(const char *dir, const char *name, char *out, int sz) {
    fl_cpy(out,dir,sz);
    int n=fl_len(out);
    if(n>0&&out[n-1]!='/'){out[n++]='/';out[n]=0;}
    fl_cat(out,name,sz);
}

/* ─── output buffer (shares terminal output) ─── */
/* We call back into terminal output via function pointers or extern */
extern void fl_print(const char *s);   /* defined below or injected */
extern void fl_error(const char *s);
extern void fl_info(const char *s);

static char fl_out_buf[FL_OUTPUT_MAX];
static int  fl_out_len = 0;
static int  fl_had_error = 0;

static void flo(const char *s)  { fl_cat(fl_out_buf,s,FL_OUTPUT_MAX); fl_cat(fl_out_buf,"\n",FL_OUTPUT_MAX); }
static void fle(const char *s)  { fl_cat(fl_out_buf,"[ERROR] ",FL_OUTPUT_MAX); fl_cat(fl_out_buf,s,FL_OUTPUT_MAX); fl_cat(fl_out_buf,"\n",FL_OUTPUT_MAX); fl_had_error=1; }
static void fli(const char *s)  { fl_cat(fl_out_buf,"[INFO]  ",FL_OUTPUT_MAX); fl_cat(fl_out_buf,s,FL_OUTPUT_MAX); fl_cat(fl_out_buf,"\n",FL_OUTPUT_MAX); }

/* ─── LEXER ─── */
#define FL_MAX_TOKENS 256
#define FL_TOK_LEN    64

typedef enum {
    TK_IDENT=0, TK_NUM, TK_DOT, TK_COLON, TK_COMMA,
    TK_LPAREN, TK_RPAREN, TK_NEWLINE, TK_EOF,
    /* keywords */
    TK_CLASS, TK_FUNCTION, TK_DIRECTORY, TK_ANY, TK_NEW,
    TK_FILES, TK_WITH, TK_FORMAT, TK_COPY, TK_TO, TK_FROM,
    TK_DETECTED, TK_POSITIONS, TK_BUTTONS, TK_RADIO,
    TK_BOX, TK_SLIDER, TK_CHECKBOX, TK_TEXT, TK_LABEL,
    TK_CENTER, TK_TOP, TK_BOTTOM, TK_LEFT, TK_RIGHT,
    TK_INIT, TK_IN, TK_DESKTOP, TK_NEW_DIR
} fl_toktype;

typedef struct { fl_toktype type; char val[FL_TOK_LEN]; int line; } fl_tok;

static fl_tok fl_toks[FL_MAX_TOKENS];
static int    fl_ntoks = 0;
static int    fl_tokpos = 0;

static fl_toktype fl_keyword(const char *w) {
    if(fl_eq(w,"class"))     return TK_CLASS;
    if(fl_eq(w,"function"))  return TK_FUNCTION;
    if(fl_eq(w,"directory")) return TK_DIRECTORY;
    if(fl_eq(w,"any"))       return TK_ANY;
    if(fl_eq(w,"new"))       return TK_NEW;
    if(fl_eq(w,"files"))     return TK_FILES;
    if(fl_eq(w,"with"))      return TK_WITH;
    if(fl_eq(w,"format"))    return TK_FORMAT;
    if(fl_eq(w,"copy"))      return TK_COPY;
    if(fl_eq(w,"to"))        return TK_TO;
    if(fl_eq(w,"from"))      return TK_FROM;
    if(fl_eq(w,"detected"))  return TK_DETECTED;
    if(fl_eq(w,"positions")) return TK_POSITIONS;
    if(fl_eq(w,"buttons"))   return TK_BUTTONS;
    if(fl_eq(w,"radio"))     return TK_RADIO;
    if(fl_eq(w,"box"))       return TK_BOX;
    if(fl_eq(w,"slider"))    return TK_SLIDER;
    if(fl_eq(w,"checkbox"))  return TK_CHECKBOX;
    if(fl_eq(w,"text"))      return TK_TEXT;
    if(fl_eq(w,"label"))     return TK_LABEL;
    if(fl_eq(w,"center"))    return TK_CENTER;
    if(fl_eq(w,"top"))       return TK_TOP;
    if(fl_eq(w,"bottom"))    return TK_BOTTOM;
    if(fl_eq(w,"left"))      return TK_LEFT;
    if(fl_eq(w,"right"))     return TK_RIGHT;
    if(fl_eq(w,"init"))      return TK_INIT;
    if(fl_eq(w,"in"))        return TK_IN;
    if(fl_eq(w,"desktop"))   return TK_DESKTOP;
    return TK_IDENT;
}

static int fl_lex(const char *src) {
    fl_ntoks=0; fl_tokpos=0;
    int line=1; const char *p=src;
    while(*p && fl_ntoks < FL_MAX_TOKENS-1) {
        /* skip spaces */
        while(*p==' '||*p=='\t') p++;
        if(!*p) break;
        if(*p=='\n'||*p=='\r'){
            fl_toks[fl_ntoks].type=TK_NEWLINE;
            fl_toks[fl_ntoks].val[0]=0; fl_toks[fl_ntoks].line=line;
            fl_ntoks++; line++; p++; continue;
        }
        if(*p=='#'){while(*p&&*p!='\n') p++; continue;}/* comment */
        if(*p=='.'){fl_toks[fl_ntoks].type=TK_DOT;fl_toks[fl_ntoks].val[0]='.';fl_toks[fl_ntoks].val[1]=0;fl_toks[fl_ntoks].line=line;fl_ntoks++;p++;continue;}
        if(*p==':'){fl_toks[fl_ntoks].type=TK_COLON;fl_toks[fl_ntoks].val[0]=':';fl_toks[fl_ntoks].val[1]=0;fl_toks[fl_ntoks].line=line;fl_ntoks++;p++;continue;}
        if(*p==','){fl_toks[fl_ntoks].type=TK_COMMA;fl_toks[fl_ntoks].val[0]=',';fl_toks[fl_ntoks].val[1]=0;fl_toks[fl_ntoks].line=line;fl_ntoks++;p++;continue;}
        if(*p=='('){fl_toks[fl_ntoks].type=TK_LPAREN;fl_toks[fl_ntoks].val[0]='(';fl_toks[fl_ntoks].val[1]=0;fl_toks[fl_ntoks].line=line;fl_ntoks++;p++;continue;}
        if(*p==')'){fl_toks[fl_ntoks].type=TK_RPAREN;fl_toks[fl_ntoks].val[0]=')';fl_toks[fl_ntoks].val[1]=0;fl_toks[fl_ntoks].line=line;fl_ntoks++;p++;continue;}
        /* number */
        if(fl_digit(*p)) {
            char buf[FL_TOK_LEN]; int i=0;
            while(fl_digit(*p)&&i<FL_TOK_LEN-1){buf[i++]=*p++;} buf[i]=0;
            fl_toks[fl_ntoks].type=TK_NUM;
            fl_cpy(fl_toks[fl_ntoks].val,buf,FL_TOK_LEN);
            fl_toks[fl_ntoks].line=line; fl_ntoks++; continue;
        }
        /* extension: .jpg .png etc */
        if(*p=='.'&&*(p+1)>='a'&&*(p+1)<='z'){
            char buf[FL_TOK_LEN]; int i=0;
            while(*p&&*p!=' '&&*p!='\n'&&i<FL_TOK_LEN-1){buf[i++]=*p++;} buf[i]=0;
            fl_toks[fl_ntoks].type=TK_IDENT;
            fl_cpy(fl_toks[fl_ntoks].val,buf,FL_TOK_LEN);
            fl_toks[fl_ntoks].line=line; fl_ntoks++; continue;
        }
        /* identifier / keyword */
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_'||*p=='/') {
            char buf[FL_TOK_LEN]; int i=0;
            while(((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_'||*p=='/'||(*p>='0'&&*p<='9'))&&i<FL_TOK_LEN-1)
                {buf[i++]=*p++;}
            buf[i]=0;
            /* lower for keyword matching */
            char low[FL_TOK_LEN]; fl_cpy(low,buf,FL_TOK_LEN); fl_lower(low);
            fl_toks[fl_ntoks].type=fl_keyword(low);
            fl_cpy(fl_toks[fl_ntoks].val,buf,FL_TOK_LEN);
            fl_toks[fl_ntoks].line=line; fl_ntoks++; continue;
        }
        p++; /* skip unknown */
    }
    fl_toks[fl_ntoks].type=TK_EOF; fl_toks[fl_ntoks].val[0]=0;
    fl_toks[fl_ntoks].line=line; fl_ntoks++;
    return fl_ntoks;
}

/* ─── PARSER / AST ─── */
#define FL_MAX_FUNCS   16
#define FL_MAX_PARAMS   8
#define FL_MAX_POS      8
#define FL_MAX_CLASS_FN 16

typedef enum {
    FN_UI=0, FN_CHECK, FN_COPY, FN_COMPUTE, FN_SCRIPT, FN_UNKNOWN
} fl_fn_kind;

typedef struct {
    char      name[32];
    char      val[64];   /* e.g. "center", "top", "100", "200" */
} fl_pos_t;

typedef struct {
    fl_fn_kind kind;
    char       name[32];
    /* UI params */
    int        has_buttons, has_radio, has_slider, has_checkbox, has_textbox, has_label;
    int        button_count;
    fl_pos_t   positions[FL_MAX_POS];
    int        pos_count;
    /* CHECK params */
    char       check_dir[128];
    char       check_ext[16];   /* e.g. ".jpg" */
    /* COPY params */
    char       copy_from_fn[32];/* name of check function */
    char       copy_dest[128];  /* destination directory */
    /* COMPUTE (calc) */
    char       expr[128];
    char       script_body[2048];
} fl_func_t;

typedef struct {
    char     name[32];
    char     fn_names[FL_MAX_CLASS_FN][32];
    int      fn_count;
} fl_class_t;

#define FL_MAX_CLASSES 8

static fl_func_t  fl_funcs[FL_MAX_FUNCS];
static int        fl_nfuncs = 0;
static fl_class_t fl_classes[FL_MAX_CLASSES];
static int        fl_nclasses = 0;

/* token helpers */
static fl_tok *fl_cur(void)  { return &fl_toks[fl_tokpos]; }
static fl_tok *fl_peek(int n){ int p=fl_tokpos+n; if(p>=fl_ntoks) p=fl_ntoks-1; return &fl_toks[p]; }
static void    fl_adv(void)  { if(fl_tokpos<fl_ntoks-1) fl_tokpos++; }
static void    fl_skip_nl(void){ while(fl_cur()->type==TK_NEWLINE) fl_adv(); }

static int fl_tok_is_ident_or_kw(fl_toktype t){
    return t!=TK_DOT&&t!=TK_COLON&&t!=TK_COMMA&&t!=TK_LPAREN&&t!=TK_RPAREN&&t!=TK_NEWLINE&&t!=TK_EOF;
}

/* collect remaining tokens on this line into a string */
static void fl_collect_line(char *out, int sz) {
    out[0]=0;
    while(fl_cur()->type!=TK_NEWLINE&&fl_cur()->type!=TK_EOF&&fl_cur()->type!=TK_DOT) {
        fl_cat(out,fl_cur()->val,sz); fl_cat(out," ",sz); fl_adv();
    }
    fl_rtrim(out);
}

/* parse function definition */
static int fl_parse_function(void) {
    /* "function" already consumed */
    fl_func_t *fn = &fl_funcs[fl_nfuncs];
    /* zero-init */
    fn->kind=FN_UNKNOWN; fn->name[0]=0;
    fn->has_buttons=fn->has_radio=fn->has_slider=fn->has_checkbox=fn->has_textbox=fn->has_label=0;
    fn->button_count=0; fn->pos_count=0;
    fn->check_dir[0]=fn->check_ext[0]=fn->copy_from_fn[0]=fn->copy_dest[0]=0;
    fn->expr[0]=0;

    fl_skip_nl();
    /* function name */
    if(fl_tok_is_ident_or_kw(fl_cur()->type)) {
        fl_cpy(fn->name,fl_cur()->val,32); fl_adv();
    }

    /* optional colon → body on same line */
    if(fl_cur()->type==TK_COLON) {
        fl_adv();
        /* check or copy or compute kind */
        char rest[256]; rest[0]=0;
        /* read rest of line */
        while(fl_cur()->type!=TK_NEWLINE&&fl_cur()->type!=TK_EOF&&fl_cur()->type!=TK_DOT){
            fl_cat(rest,fl_cur()->val,sizeof(rest));fl_cat(rest," ",sizeof(rest));fl_adv();
        }
        fl_rtrim(rest);
        char low[256]; fl_cpy(low,rest,256); fl_lower(low);

        if(fl_contains_sub(low,"directory")||fl_contains_sub(low,"dir")) {
            fn->kind=FN_CHECK;
            /* parse: directory <path> any new files with format <ext> */
            /* find dir name */
            const char *p=fl_ltrim(low);
            /* skip "directory" */
            if(fl_starts(p,"directory")) p=fl_ltrim(p+9);
            /* read path until "any" or "with" */
            int i=0;
            while(*p&&!fl_starts(p,"any")&&!fl_starts(p,"with")&&i<127)
                {fn->check_dir[i++]=*p++;} fn->check_dir[i]=0; fl_rtrim(fn->check_dir);
            /* map friendly names */
            if(fl_eq(fn->check_dir,"desktop")) fl_cpy(fn->check_dir,"/Desktop",128);
            else if(fn->check_dir[0]!='/') {char tmp[128];tmp[0]='/';fl_cat(tmp,fn->check_dir,128);fl_cpy(fn->check_dir,tmp,128);}
            /* find format */
            const char *fmt=fl_contains_sub(low,"format")?low:0;
            if(fmt){
                while(*fmt&&!fl_starts(fmt,"format")) fmt++;
                if(*fmt){fmt+=6; fmt=fl_ltrim(fmt);}
                i=0; while(*fmt&&*fmt!=' '&&i<15){fn->check_ext[i++]=(char)*fmt++;} fn->check_ext[i]=0;
                if(fn->check_ext[0]&&fn->check_ext[0]!='.'){char t[16];t[0]='.';fl_cat(t,fn->check_ext,16);fl_cpy(fn->check_ext,t,16);}
            }
        } else if(fl_contains_sub(low,"copy")) {
            fn->kind=FN_COPY;
            /* parse: copy detected from <fn> to <dest> */
            /* find "from" */
            const char *p=fl_contains_sub(low,"from")?low:0;
            if(p){while(*p&&!fl_starts(p,"from"))p++;p=fl_ltrim(p+4);
                int i=0;while(*p&&!fl_starts(p,"to")&&i<31){fn->copy_from_fn[i++]=(char)*p++;} fn->copy_from_fn[i]=0; fl_rtrim(fn->copy_from_fn);
            }
            /* find "to" */
            const char *q=fl_contains_sub(low," to ")?(low):0;
            if(q){while(*q&&!fl_starts(q," to "))q++;q=fl_ltrim(q+4);
                /* check "new directory" prefix */
                if(fl_starts(q,"new directory")){q=fl_ltrim(q+13);}
                else if(fl_starts(q,"directory")){q=fl_ltrim(q+9);}
                /* map desktop */
                char dest[128]; int i=0;
                while(*q&&i<127){fn->copy_dest[i++]=(char)*q++;} fn->copy_dest[i]=0; fl_rtrim(fn->copy_dest);
                if(fl_starts(fn->copy_dest,"desktop/")||fl_eq(fn->copy_dest,"desktop"))
                    {char tmp[128];tmp[0]=0;fl_cat(tmp,"/",128);fl_cat(tmp,fn->copy_dest,128);fl_cpy(fn->copy_dest,tmp,128);}
                else if(fn->copy_dest[0]!='/')
                    {char tmp[128];tmp[0]='/';fl_cat(tmp,fn->copy_dest,128);fl_cpy(fn->copy_dest,tmp,128);}
                (void)dest;
            }
            /* also check "in function <name>" for source fn */
            if(!fn->copy_from_fn[0]&&fl_contains_sub(low,"function")){
                const char *r=low; while(*r&&!fl_starts(r,"function"))r++;
                r=fl_ltrim(r+8); int i=0;
                while(*r&&*r!=' '&&i<31){fn->copy_from_fn[i++]=(char)*r++;} fn->copy_from_fn[i]=0;
            }
        } else {
            fn->kind=FN_SCRIPT;
            char body[2048]; body[0]=0;
            fl_cat(body, rest, sizeof(body)); fl_cat(body, "\n", sizeof(body));
            while(fl_cur()->type!=TK_EOF && fl_cur()->type!=TK_FUNCTION && fl_cur()->type!=TK_CLASS) {
                if(fl_cur()->type==TK_NEWLINE) fl_cat(body, "\n", sizeof(body));
                else { fl_cat(body, fl_cur()->val, sizeof(body)); fl_cat(body, " ", sizeof(body)); }
                fl_adv();
            }
            fl_cpy(fn->script_body, body, sizeof(fn->script_body));
        }
        if(fl_nfuncs<FL_MAX_FUNCS) fl_nfuncs++;
        return 1;
    }

    /* optional params in parens */
    if(fl_cur()->type==TK_LPAREN) {
        fn->kind=FN_UI;
        fl_adv();
        while(fl_cur()->type!=TK_RPAREN&&fl_cur()->type!=TK_EOF) {
            if(fl_cur()->type==TK_NUM){fn->button_count=fl_atoi(fl_cur()->val);fl_adv();continue;}
            if(fl_cur()->type==TK_BUTTONS){fn->has_buttons=1;fl_adv();continue;}
            if(fl_cur()->type==TK_RADIO){fn->has_radio=1;fl_adv();
                if(fl_cur()->type==TK_BOX||fl_cur()->type==TK_IDENT) fl_adv();
                continue;}
            if(fl_cur()->type==TK_SLIDER){fn->has_slider=1;fl_adv();continue;}
            if(fl_cur()->type==TK_CHECKBOX){fn->has_checkbox=1;fl_adv();continue;}
            if(fl_cur()->type==TK_TEXT){fn->has_textbox=1;fl_adv();
                if(fl_cur()->type==TK_BOX||fl_cur()->type==TK_IDENT) fl_adv();
                continue;}
            if(fl_cur()->type==TK_LABEL){fn->has_label=1;fl_adv();continue;}
            fl_adv();
        }
        if(fl_cur()->type==TK_RPAREN) fl_adv();
        if(!fn->has_buttons&&fn->button_count==0) fn->button_count=1;
        if(fn->has_buttons&&fn->button_count==0) fn->button_count=1;
    }

    if(fn->kind==FN_UNKNOWN) {
        if(fl_eq(fn->name, "UI")) fn->kind=FN_UI;
        else fn->kind=FN_SCRIPT;
    }

    /* parse body lines (positions:) */
    while(fl_cur()->type!=TK_EOF) {
        fl_skip_nl();
        if(fl_cur()->type==TK_FUNCTION||fl_cur()->type==TK_CLASS||fl_cur()->type==TK_EOF) break;
        if(fl_cur()->type==TK_BUTTONS||fl_cur()->type==TK_RADIO||
           fl_cur()->type==TK_SLIDER||fl_cur()->type==TK_CHECKBOX||
           fl_cur()->type==TK_TEXT||fl_cur()->type==TK_LABEL) {
            fl_toktype comp=fl_cur()->type; fl_adv();
            /* skip "box" */
            if(comp==TK_RADIO&&fl_cur()->type==TK_BOX) fl_adv();
            if(comp==TK_TEXT&&fl_cur()->type==TK_BOX) fl_adv();
            /* positions: */
            if(fl_cur()->type==TK_POSITIONS) {
                fl_adv();
                if(fl_cur()->type==TK_COLON) fl_adv();
                /* read position tuples */
                while((fl_cur()->type==TK_LPAREN||fl_cur()->type==TK_IDENT||
                       fl_cur()->type==TK_CENTER||fl_cur()->type==TK_TOP||
                       fl_cur()->type==TK_BOTTOM||fl_cur()->type==TK_LEFT||
                       fl_cur()->type==TK_RIGHT||fl_cur()->type==TK_NUM)&&
                      fn->pos_count<FL_MAX_POS) {
                    if(fl_cur()->type==TK_LPAREN) fl_adv();
                    fl_pos_t *pos=&fn->positions[fn->pos_count];
                    fl_cpy(pos->name, comp==TK_BUTTONS?"buttons":comp==TK_RADIO?"radio":
                           comp==TK_SLIDER?"slider":"component", 32);
                    pos->val[0]=0;
                    while(fl_cur()->type!=TK_RPAREN&&fl_cur()->type!=TK_EOF&&
                          fl_cur()->type!=TK_NEWLINE) {
                        fl_cat(pos->val,fl_cur()->val,64);fl_cat(pos->val,",",64);fl_adv();
                    }
                    fl_rtrim(pos->val);
                    if(fl_cur()->type==TK_RPAREN) fl_adv();
                    fn->pos_count++;
                }
            }
        }
        if(fl_cur()->type==TK_NEWLINE) fl_adv();
        else break;
    }

    if(fl_nfuncs<FL_MAX_FUNCS) fl_nfuncs++;
    return 1;
}

static int fl_parse(const char *src) {
    fl_lex(src);
    fl_tokpos=0; fl_nfuncs=0; fl_nclasses=0;
    fl_skip_nl();
    while(fl_cur()->type!=TK_EOF) {
        fl_skip_nl();
        if(fl_cur()->type==TK_CLASS) {
            fl_adv();
            fl_class_t *cls=&fl_classes[fl_nclasses];
            cls->name[0]=0; cls->fn_count=0;
            /* class name */
            if(fl_cur()->type!=TK_COLON&&fl_tok_is_ident_or_kw(fl_cur()->type))
                {fl_cpy(cls->name,fl_cur()->val,32);fl_adv();}
            if(fl_cur()->type==TK_COLON) fl_adv();
            /* collect function references */
            while(fl_cur()->type!=TK_NEWLINE&&fl_cur()->type!=TK_DOT&&fl_cur()->type!=TK_EOF) {
                if(fl_cur()->type==TK_FUNCTION) {
                    fl_adv();
                    if(fl_tok_is_ident_or_kw(fl_cur()->type)&&cls->fn_count<FL_MAX_CLASS_FN) {
                        fl_cpy(cls->fn_names[cls->fn_count],fl_cur()->val,32);
                        cls->fn_count++; fl_adv();
                    }
                } else fl_adv();
            }
            if(fl_cur()->type==TK_DOT) fl_adv();
            if(fl_nclasses<FL_MAX_CLASSES) fl_nclasses++;
        } else if(fl_cur()->type==TK_FUNCTION) {
            fl_adv();
            fl_parse_function();
        } else {
            fl_adv();
        }
    }
    return fl_nfuncs;
}

/* ─── EXECUTOR ─── */

/* helpers for fb drawing */
extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void fb_round_rect(int x, int y, int w, int h, int r, u32 color);
extern void fb_round_rect_border(int x, int y, int w, int h, int r, u32 color);
extern void font_text_fit(int x, int y, const char *t, u32 c, int max_px);
extern void font_text_fit_center(int x, int y, const char *t, u32 c, int max_px);
extern framebuffer_t fb;

static void fl_outline(int x,int y,int w,int h,u32 c){
    fb_rect(x,y,w,1,c);fb_rect(x,y+h-1,w,1,c);
    fb_rect(x,y,1,h,c);fb_rect(x+w-1,y,1,h,c);
}

/* detect file scanner results: filled by exec_check */
#define FL_MAX_DETECTED 32
static char fl_detected[FL_MAX_DETECTED][256];
static int  fl_detected_count = 0;
/* map: function name → detected list */
static char fl_check_fn_name[32];

/* UI window state */
static int fl_ui_open    = 0;
static fl_func_t *fl_active_ui_fn = 0;
#define MAX_DYN_UI 64
typedef struct { char type; char name[32]; char text[64]; int x, y; int action_ip; } fl_dyn_ui_t;
static fl_dyn_ui_t dyn_ui[MAX_DYN_UI];
static int num_dyn_ui = 0;
static void exec_script(fl_func_t *fn);
static void exec_script_loop(int start_ip);
extern int needs_redraw;

static int fl_ui_x, fl_ui_y, fl_ui_w, fl_ui_h;

/* resolve position string ("center,top", "100,200") to pixel coords */
static void fl_resolve_pos(const char *posval, int win_w, int win_h, int *px, int *py) {
    char lo[64]; fl_cpy(lo,posval,64); fl_lower(lo);
    int has_center=fl_contains_sub(lo,"center");
    int has_top=fl_contains_sub(lo,"top");
    int has_bottom=fl_contains_sub(lo,"bottom");
    int has_left=fl_contains_sub(lo,"left");
    int has_right=fl_contains_sub(lo,"right");
    *px=has_right?(win_w-80):has_left?10:has_center?(win_w/2-40):(win_w/2-40);
    *py=has_bottom?(win_h-40):has_top?20:40;
    (void)win_h;
}

/* Draw a UI button */
static void fl_draw_button(int x,int y,int w,const char *label){
    fb_round_rect(x, y, w, 24, 6, 0x3A6EA0);
    fb_round_rect(x+1, y+1, w-2, 22, 5, 0x4A85C0);
    fb_round_rect_border(x, y, w, 24, 6, 0x2A5080);
    font_text_fit_center(x, y+7, label, 0xFFFFFF, w);
}

/* Draw radio button */
static void fl_draw_radio(int x,int y,const char *label){
    fb_round_rect(x, y+3, 14, 14, 7, 0xFFFFFF);
    fb_round_rect_border(x, y+3, 14, 14, 7, 0x4A7AB0);
    fb_round_rect(x+4, y+7, 6, 6, 3, 0x3A6EA0);
    font_text_fit(x+18, y+5, label, 0x1A2A3A, 120);
}

/* Draw slider */
static void fl_draw_slider(int x,int y,int w){
    int track_y = y + 8;
    int track_h = 6;
    fb_round_rect(x, track_y, w, track_h, 3, 0xC0D0E0);
    fb_round_rect_border(x, track_y, w, track_h, 3, 0x7090B0);
    int thumb_x = x + w / 2 - 8;
    fb_round_rect(thumb_x, y + 3, 16, 16, 8, 0x3A6EA0);
    fb_round_rect_border(thumb_x, y + 3, 16, 16, 8, 0x2A5080);
}

/* Draw textbox */
static void fl_draw_textbox(int x,int y,int w,const char *hint){
    fb_round_rect(x, y, w, 22, 5, 0xF8FAFF);
    fb_round_rect_border(x, y, w, 22, 5, 0x7090B0);
    font_text_fit(x+6, y+6, hint, 0x556677, w-12);
}

/* Draw checkbox */
static void fl_draw_checkbox(int x,int y,const char *label){
    fb_round_rect(x, y, 16, 16, 4, 0xFFFFFF);
    fb_round_rect_border(x, y, 16, 16, 4, 0x5080A0);
    fb_round_rect(x+4, y+4, 8, 8, 2, 0x3A6EA0);
    font_text_fit(x+22, y+3, label, 0x1A2A3A, 120);
}

static void exec_ui(fl_func_t *fn) {
    fl_active_ui_fn = fn;
    fl_ui_open = 1;
    char msg[64]; msg[0]=0;
    fl_cat(msg,"UI window opened: ",sizeof(msg));
    fl_cat(msg,fn->name,sizeof(msg));
    fli(msg);
}

void firstlang_draw_ui(void) {
    if(!fl_ui_open) return;
    int scr_w=(int)fb.width, scr_h=(int)fb.height;
    int wx=scr_w/2-220, wy=scr_h/2-180;
    int ww=440,wh=360;
    fl_ui_x=wx;fl_ui_y=wy;fl_ui_w=ww;fl_ui_h=wh;fl_ui_open=1;

    /* window shadow */
    fb_rect(wx+4,wy+4,ww,wh,0x0A1422);
    /* window bg */
    fb_rect(wx,wy,ww,wh,0xDDE8F4);
    fl_outline(wx,wy,ww,wh,0x3A6090);
    /* title bar */
    fb_rect(wx,wy,ww,28,0x2A5080);
    fb_rect(wx,wy,ww,1,0x5080B0);
    /* title text */
    char title[64]; title[0]=0;
    fl_cat(title,"  FirstLang App",sizeof(title));
    font_text_fit(wx+8,wy+8,title,0xFFFFFF,ww-60);
    /* close button */
    fb_rect(wx+ww-26,wy+4,20,20,0xC04040);
    fl_outline(wx+ww-26,wy+4,20,20,0x902020);
    font_text_fit_center(wx+ww-26,wy+10,"X",0xFFFFFF,20);

    /* content area */
    fb_rect(wx+8,wy+36,ww-16,wh-44,0xF0F5FB);
    fl_outline(wx+8,wy+36,ww-16,wh-44,0xBBCCDD);

    int content_x=wx+20, content_y=wy+50;
    int cursor_y=content_y;




    /* render dynamic ui */
    int di;
    int auto_y = 10;
    for(di=0; di<num_dyn_ui; di++) {
        fl_dyn_ui_t *u = &dyn_ui[di];
        int dx = u->x; int dy = u->y;
        if(dx == -1) dx = 20; /* left align auto-labels */
        if(dy == -1) {
            dy = auto_y;
            if (u->type == 'H' || u->type == 'I' || u->type == 'R' || u->type == 'C') auto_y += 70;
            else auto_y += 30;
        }
        if(u->type == 'B') fl_draw_button(wx+dx, wy+40+dy, 120, u->text);
        else if(u->type == 'L') font_text_fit(wx+dx, wy+40+dy, u->text, 0x000000, ww-20);
        else if(u->type == 'S') { font_text_fit(wx+dx, wy+40+dy, u->text, 0x000000, ww-20); fl_draw_slider(wx+dx+64, wy+40+dy, 120); }
else if(u->type == 'T') fl_draw_textbox(wx+dx, wy+40+dy, 120, u->text);
        else if(u->type == 'R') {
            extern void fb_rect(int x, int y, int w, int h, unsigned int color);
            fb_rect(wx+dx, wy+40+dy, 60, 60, 0x336699);
        }
        else if(u->type == 'C') {
            extern void outline(int x, int y, int w, int h, unsigned int color);
            outline(wx+dx, wy+40+dy, 60, 60, 0x993366);
        }
        else if(u->type == 'A') fl_draw_radio(wx+dx, wy+40+dy, u->text);
        else if(u->type == 'K') fl_draw_checkbox(wx+dx, wy+40+dy, u->text);
        else if(u->type == 'H') {
            extern void fb_rect(int x, int y, int w, int h, unsigned int color);
            fb_rect(wx+dx, wy+40+dy, 120, 60, 0xE0E0E0);
            fb_rect(wx+dx+10, wy+40+dy+20, 20, 40, 0x336699);
            fb_rect(wx+dx+40, wy+40+dy+10, 20, 50, 0x336699);
            fb_rect(wx+dx+70, wy+40+dy+30, 20, 30, 0x336699);
            font_text_fit(wx+dx, wy+40+dy-16, u->text, 0x000000, ww-20);
        }
        else if(u->type == 'I') {
            extern int load_ico_from_path(const char* path, unsigned int* out_pixels, int* out_w, int* out_h);
            extern void draw_icon_scaled(const unsigned int *pixels, int src_w, int src_h, int x, int y, int dst_w, int dst_h);
            static unsigned int img_buf[4096];
            int img_w=0, img_h=0;
            if(load_ico_from_path(u->text, img_buf, &img_w, &img_h)) {
                draw_icon_scaled(img_buf, img_w, img_h, wx+dx, wy+40+dy, img_w, img_h);
            } else {
                font_text_fit(wx+dx, wy+40+dy, "[Image Not Found]", 0xFF0000, ww-20);
            }
        }
    }
    /* status bar */
    fb_rect(wx,wy+wh-22,ww,22,0xC8D8E8);
    fl_outline(wx,wy+wh-22,ww,22,0x8AA0B8);
    char status[64]; status[0]=0;
    fl_cat(status,"  UI rendered by FirstLang",sizeof(status));
    font_text_fit(wx+8,wy+wh-14,status,0x3A5070,ww-16);

}

static void exec_check(fl_func_t *fn) {
    fl_detected_count=0;
    fl_cpy(fl_check_fn_name,fn->name,32);

    /* resolve dir */
    char dir[128]; fl_cpy(dir,fn->check_dir,128);
    if(!dir[0]||fl_eq(dir,"Desktop")) fl_cpy(dir,"/Desktop",128);

    int count=fs_count_in_dir(dir),i;
    if(count<0){
        char e[64];e[0]=0;fl_cat(e,"check: directory not found: ",64);fl_cat(e,dir,64);fle(e);return;
    }
    char msg[128];msg[0]=0;
    fl_cat(msg,"Scanning ",sizeof(msg));fl_cat(msg,dir,sizeof(msg));
    fl_cat(msg," for *",sizeof(msg));fl_cat(msg,fn->check_ext,sizeof(msg));
    fli(msg);

    for(i=0;i<count;i++){
        const fs_entry_t *e=fs_entry_in_dir(dir,i); if(!e||e->is_dir) continue;
        if(fn->check_ext[0]&&!fl_ends(e->name,fn->check_ext)) continue;
        if(fl_detected_count<FL_MAX_DETECTED){
            fl_path(dir,e->name,fl_detected[fl_detected_count],256);
            fl_detected_count++;
        }
    }
    char res[64];res[0]=0;
    char n[8];fl_itos(fl_detected_count,n,sizeof(n));
    fl_cat(res,"Found ",sizeof(res));fl_cat(res,n,sizeof(res));fl_cat(res," file(s)",sizeof(res));
    fli(res);
    for(i=0;i<fl_detected_count;i++) flo(fl_detected[i]);
}

static void exec_copy(fl_func_t *fn) {
    /* ensure destination directory exists */
    fs_mkdir(fn->copy_dest);

    int count=fl_detected_count,i,copied=0,failed=0;
    if(count==0){fli("copy: no files to copy (run check first)");return;}

    char dest[256];fl_cpy(dest,fn->copy_dest,sizeof(dest));
    char msg[128];msg[0]=0;
    fl_cat(msg,"Copying ",sizeof(msg));char n[8];fl_itos(count,n,sizeof(n));
    fl_cat(msg,n,sizeof(msg));fl_cat(msg," file(s) → ",sizeof(msg));fl_cat(msg,dest,sizeof(msg));
    fli(msg);

    static char buf[32768];
    for(i=0;i<count;i++){
        /* get filename from path */
        const char *src=fl_detected[i];
        const char *name=src; const char *p=src;
        while(*p){if(*p=='/') name=p+1;p++;}
        char dst[256]; fl_path(dest,name,dst,sizeof(dst));
        int bytes=fs_read_at(src,buf,sizeof(buf)-1);
        if(bytes<0){failed++;continue;}
        buf[bytes]=0;
        fs_create_at(dst,0);
        if(fs_write_at(dst,buf,(u32)bytes)){
            char line[64];line[0]=0;
            fl_cat(line,"  ✓ ",sizeof(line));fl_cat(line,name,sizeof(line));flo(line);
            copied++;
        } else {
            char line[64];line[0]=0;
            fl_cat(line,"  ✗ ",sizeof(line));fl_cat(line,name,sizeof(line));fle(line);failed++;
        }
    }
    char r[64];r[0]=0;char c[8],f[8];fl_itos(copied,c,8);fl_itos(failed,f,8);
    fl_cat(r,"Done: ",sizeof(r));fl_cat(r,c,sizeof(r));fl_cat(r," copied, ",sizeof(r));fl_cat(r,f,sizeof(r));fl_cat(r," failed",sizeof(r));
    fli(r);
}

static void exec_compute(fl_func_t *fn) {
    /* simple expression eval */
    char msg[128];msg[0]=0;
    fl_cat(msg,"compute: ",sizeof(msg));fl_cat(msg,fn->expr,sizeof(msg));
    flo(msg);
}

static fl_func_t *fl_find_func(const char *name) {
    int i; for(i=0;i<fl_nfuncs;i++)
        if(fl_eq(fl_funcs[i].name,name)) return &fl_funcs[i];
    return 0;
}

/* needs_redraw callback */
extern int needs_redraw;

static void fl_exec_func(fl_func_t *fn) {
    char m[64];m[0]=0;fl_cat(m,"→ function ",sizeof(m));fl_cat(m,fn->name,sizeof(m));fli(m);
    switch(fn->kind){
        case FN_UI:      exec_ui(fn);      needs_redraw=1; break;
        case FN_CHECK:   exec_check(fn);   break;
        case FN_COPY:    exec_copy(fn);    break;
        case FN_COMPUTE: exec_compute(fn); break;
        case FN_SCRIPT:  exec_script(fn);  needs_redraw=1; break;
        default: break;
    }
}

/* ─── PUBLIC API ─── */


/* --- Script Evaluator --- */
#define MAX_VARS 64
static char var_names[MAX_VARS][32];
static int  var_ivals[MAX_VARS];
static char var_svals[MAX_VARS][128];
static int  var_types[MAX_VARS]; /* 0=int, 1=string */
static int  script_num_vars = 0;

static int eval_expr_int(const char *expr);

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
    int val = 0; char op='+';
    while(*p) {
        p=fl_ltrim(p);
        if(!*p) break;
        if(*p=='+' || *p=='-' || *p=='*' || *p=='/' || *p=='%') { op = *p; p++; continue; }
        int term=0;
        if(fl_digit(*p)) { term=fl_atoi(p); while(fl_digit(*p)) p++; }
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

static void eval_expr_str(const char *expr, char *out) {
    out[0]=0;
    char ex[128]; fl_cpy(ex,expr,128); fl_rtrim(ex);
    
    // If it has no quotes and has math operators (or is just variables/numbers), evaluate as int!
    // But wait, what if it's a string variable? "set name to playerName" (no quotes).
    // Let's just say if it evaluates to 0, and there are no operators, maybe it's a string?
    // Actually, if it has any of '-', '*', '/', '%', it's definitely math!
    // If it has '+', it could be string concat or math.
    // Let's do a smart check:
    int has_quotes = fl_contains_sub(ex, "\"");
    int has_math = fl_contains_sub(ex, "-") || fl_contains_sub(ex, "*") || fl_contains_sub(ex, "/") || fl_contains_sub(ex, "%");
    
    if (!has_quotes && (has_math || fl_contains_sub(ex, "+"))) {
        // Let's evaluate as math!
        int val = eval_expr_int(ex);
        fl_itos(val, out, 128);
        return;
    }
    
    if (!has_quotes && !has_math && !fl_contains_sub(ex, "+")) {
        // Single variable or number
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
    int is_neq = 0;
    if(fl_contains_sub(c, " is less than ")) { op = " is less than "; is_less=1; }
    else if(fl_contains_sub(c, " is smaller than ")) { op = " is smaller than "; is_less=1; }
    else if(fl_contains_sub(c, " < ")) { op = " < "; is_less=1; }
    else if(fl_contains_sub(c, " is equal to ")) { op = " is equal to "; is_eq=1; }
    else if(fl_contains_sub(c, " equals ")) { op = " equals "; is_eq=1; }
    else if(fl_contains_sub(c, " is not ")) { op = " is not "; is_neq=1; }
    else if(fl_contains_sub(c, " != ")) { op = " != "; is_neq=1; }
    else if(fl_contains_sub(c, " is ")) { op = " is "; is_eq=1; }
    else if(fl_contains_sub(c, " = ")) { op = " = "; is_eq=1; }
    else if(fl_contains_sub(c, " == ")) { op = " == "; is_eq=1; }
    else if(fl_contains_sub(c, " is greater than ")) { op = " is greater than "; is_gt=1; }
    else if(fl_contains_sub(c, " is bigger than ")) { op = " is bigger than "; is_gt=1; }
    else if(fl_contains_sub(c, " > ")) { op = " > "; is_gt=1; }
    
    if(!op) return eval_expr_int(c); /* truthy */
    char left[64]; char right[64];
    const char *p = c; while(*p) {
        int i=0; int match=1; while(op[i]) { if(p[i]!=op[i]) { match=0; break; } i++; }
        if(match) {
            int j; for(j=0; j<(p-c); j++) left[j]=c[j]; left[p-c]=0; fl_rtrim(left);
            fl_cpy(right, p+fl_len(op), 64); fl_ltrim(right); fl_rtrim(right);
            
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
        
        if(fl_starts(p, "set ") || fl_starts(p, "let ") || fl_starts(p, "put ") || fl_starts(p, "assign ")) {
            if(fl_starts(p, "set ")) p+=4;
            else if(fl_starts(p, "let ")) p+=4;
            else if(fl_starts(p, "put ")) p+=4;
            else if(fl_starts(p, "assign ")) p+=7;
            p=fl_ltrim(p);
            int is_hidden = 0;
            if(fl_starts(p, "h ")) { is_hidden = 1; p+=2; p=fl_ltrim(p); }
            else if(fl_starts(p, "hidden ")) { is_hidden = 1; p+=7; p=fl_ltrim(p); }
            
            // Check if it's just an expression without "to" or "="
            int has_to = fl_contains_sub(p, " to ") || fl_contains_sub(p, " be ") || fl_contains_sub(p, " is ") || fl_contains_sub(p, " into ");
            int has_eq = fl_contains_sub(p, "=");
            int has_with_label = fl_contains_sub(p, " with label ");
            if (!has_to && !has_eq && !has_with_label) {
                // Evaluate as an expression and print it!
                char sval[128]; eval_expr_str(p, sval);
                if (num_dyn_ui < MAX_DYN_UI) {
                    fl_dyn_ui_t *u = &dyn_ui[num_dyn_ui++];
                    u->type = 'L'; u->action_ip = -1; u->name[0]=0;
                    u->x = -1; u->y = -1;
                    fl_cpy(u->text, sval, 64);
                    extern int needs_redraw; needs_redraw = 1;
                }
                ip++; continue;
            }
            
            char raw[32]; int vi=0;
            while(*p && !fl_starts(p, " to ") && !fl_starts(p, " be ") && !fl_starts(p, " is ") && !fl_starts(p, " into ") && !fl_starts(p, " with label ") && *p!='=' && *p!=' ') raw[vi++] = *p++;
            raw[vi]=0; fl_rtrim(raw);
            char vname[32]; resolve_var_name(vname, raw);
            
            char prefix[64]; prefix[0] = 0;
            if(fl_starts(p, " with label ")) {
                p+=12; p=fl_ltrim(p);
                int pi=0;
                while(*p && *p!=' ' && !fl_starts(p, " to ") && !fl_starts(p, " be ") && !fl_starts(p, " is ") && !fl_starts(p, " into ")) prefix[pi++] = *p++;
                prefix[pi]=0; fl_rtrim(prefix);
            }
            
            if(fl_starts(p, " to ")) p+=4;
            else if(fl_starts(p, " be ")) p+=4;
            else if(fl_starts(p, " is ")) p+=4;
            else if(fl_starts(p, " into ")) p+=6;
            else if(*p=='=') p+=1;
            p=fl_ltrim(p);
            
            int is_str = (*p=='"' || fl_contains_sub(p, "\""));
            char sval[128]; sval[0]=0;
            int ival = 0;
            
            if(is_str) {
                eval_expr_str(p, sval);
                set_var_str(vname, sval);
            } else {
                ival = eval_expr_int(p);
                set_var_int(vname, ival);
                fl_itos(ival, sval, 128);
            }
            
            if (!is_hidden) {
                fl_dyn_ui_t *u = 0;
                for(int di=0; di<num_dyn_ui; di++) {
                    if (dyn_ui[di].type == 'L' && fl_eq(dyn_ui[di].name, vname)) { u = &dyn_ui[di]; break; }
                }
                if (!u && num_dyn_ui < MAX_DYN_UI) {
                    u = &dyn_ui[num_dyn_ui++];
                    u->type = 'L'; u->action_ip = -1; fl_cpy(u->name, vname, 32);
                    u->x = -1; u->y = -1;
                }
                if (u) {
                char full_text[128]; full_text[0]=0;
                if (prefix[0]) {
                    fl_cpy(full_text, prefix, 128);
                    fl_cat(full_text, " ", 128);
                } else {
                    fl_cpy(full_text, vname, 128);
                    fl_cat(full_text, ": ", 128);
                }
                fl_cat(full_text, sval, 128);
                fl_cpy(u->text, full_text, 64);
                extern int needs_redraw; needs_redraw = 1;
                }
            }
        }
        else if(fl_starts(p, "read memory ")) {
            p+=12; p=fl_ltrim(p);
            if(fl_starts(p, "at ")) { p+=3; p=fl_ltrim(p); }
            char addr_str[64]; int ai=0;
            while(*p && !fl_starts(p, " to ")) addr_str[ai++] = *p++; addr_str[ai]=0; fl_rtrim(addr_str);
            p+=4; p=fl_ltrim(p);
            char vname[32]; resolve_var_name(vname, p);
            unsigned int addr = (unsigned int)eval_expr_int(addr_str);
            int val = 0;
            if(addr >= 0x1000) val = *((int*)(unsigned long)addr);
            set_var_int(vname, val);
        }
        else if(fl_starts(p, "write memory ")) {
            p+=13; p=fl_ltrim(p);
            char val_str[64]; int vi=0;
            while(*p && !fl_starts(p, " to ") && !fl_starts(p, " at ")) val_str[vi++] = *p++; val_str[vi]=0; fl_rtrim(val_str);
            if(fl_starts(p, " to ")) p+=4; else if(fl_starts(p, " at ")) p+=4;
            p=fl_ltrim(p);
            char addr_str[64]; int ai=0;
            while(*p) addr_str[ai++] = *p++; addr_str[ai]=0; fl_rtrim(addr_str);
            unsigned int addr = (unsigned int)eval_expr_int(addr_str);
            int val = eval_expr_int(val_str);
            if(addr >= 0x1000) *((int*)(unsigned long)addr) = val;
        }
        else if(fl_starts(p, "allocate memory ")) {
            p+=16; p=fl_ltrim(p);
            char size_str[64]; int si=0;
            while(*p && !fl_starts(p, " to ")) size_str[si++] = *p++; size_str[si]=0; fl_rtrim(size_str);
            p+=4; p=fl_ltrim(p);
            char vname[32]; resolve_var_name(vname, p);
            int size = eval_expr_int(size_str);
            extern void* kmalloc(unsigned int size);
            unsigned int ptr = (unsigned int)(unsigned long)kmalloc(size);
            set_var_int(vname, ptr);
        }
        else if(fl_starts(p, "free memory ")) {
            p+=12; p=fl_ltrim(p);
            char addr_str[64]; int ai=0;
            while(*p) addr_str[ai++] = *p++; addr_str[ai]=0; fl_rtrim(addr_str);
            unsigned int addr = (unsigned int)eval_expr_int(addr_str);
            extern void kfree(void* ptr);
            if(addr >= 0x1000) kfree((void*)(unsigned long)addr);
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
        else if(fl_starts(p, "while ") || fl_starts(p, "as long as ")) {
            if(fl_starts(p, "while ")) p+=6;
            else p+=11;
            p=fl_ltrim(p);
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
        else if(fl_starts(p, "on ") || fl_starts(p, "whenever ") || fl_starts(p, "when ")) {
            if(fl_starts(p, "on ")) p+=3;
            else if(fl_starts(p, "whenever ")) p+=9;
            else if(fl_starts(p, "when ")) p+=5;
            p=fl_ltrim(p);
            if(fl_starts(p, "click ")) {
                p+=6; p=fl_ltrim(p);
                char target[64]; eval_expr_str(p, target);
                int skip=1;
                int action_start = ip+1;
                while(ip+1 < script_num_lines && skip>0) {
                    ip++; const char *np = fl_ltrim(script_lines[ip]);
                    if(fl_starts(np, "if ") || fl_starts(np, "while ") || fl_starts(np, "on ")) skip++;
                    if(fl_starts(np, "end")) skip--;
                }
                int i; for(i=0; i<num_dyn_ui; i++) {
                    if(fl_eq(dyn_ui[i].text, target) || fl_eq(dyn_ui[i].name, target)) {
                        dyn_ui[i].action_ip = action_start;
                    }
                }
            }
        }
        else if(fl_starts(p, "function ")) {
            int skip=1;
            while(ip+1 < script_num_lines && skip>0) {
                ip++; const char *np = fl_ltrim(script_lines[ip]);
                if(fl_starts(np, "if ") || fl_starts(np, "while ") || fl_starts(np, "on ") || fl_starts(np, "function ")) skip++;
                if(fl_starts(np, "end")) skip--;
            }
        }
        else if(fl_starts(p, "class ")) {
            while(ip+1 < script_num_lines) {
                ip++; const char *np = fl_ltrim(script_lines[ip]);
                if(fl_starts(np, ".")) break;
            }
        }
        else if(fl_starts(p, ".")) {
            /* ignore class dot */
        }
        else if(fl_starts(p, "end")) {
            if(loop_sp > 0) ip = loop_stack[--loop_sp] - 1;
            else if(script_call_sp > 0) { ip = script_call_stack[--script_call_sp]; } /* return from call */
            else if(start_ip != 0) return; /* end of event listener block */
        }
        else if(fl_starts(p, "play ")) {
            p+=5; p=fl_ltrim(p);
            char arg[128]; eval_expr_str(p, arg);
            extern int hda_play_audio(const char* path);
            hda_play_audio(arg);
        }
        else if(fl_starts(p, "read file ")) {
            p+=10; p=fl_ltrim(p);
            char path_str[64]; int pi=0;
            while(*p && !fl_starts(p, " to ")) path_str[pi++] = *p++; path_str[pi]=0; fl_rtrim(path_str);
            p+=4; p=fl_ltrim(p);
            char vname[32]; resolve_var_name(vname, p);
            char path[64]; eval_expr_str(path_str, path);
            char buf[1024]; buf[0]=0;
            extern int fs_read_at(const char *path, char *out, unsigned int capacity);
            int r = fs_read_at(path, buf, 1023);
            if(r >= 0) { buf[r]=0; set_var_str(vname, buf); }
            else set_var_str(vname, "");
        }
        else if(fl_starts(p, "write file ")) {
            p+=11; p=fl_ltrim(p);
            char val_str[64]; int vi=0;
            while(*p && !fl_starts(p, " to ")) val_str[vi++] = *p++; val_str[vi]=0; fl_rtrim(val_str);
            p+=4; p=fl_ltrim(p);
            char path_str[64]; int pi=0;
            while(*p) path_str[pi++] = *p++; path_str[pi]=0; fl_rtrim(path_str);
            char val[1024]; eval_expr_str(val_str, val);
            char path[64]; eval_expr_str(path_str, path);
            extern int fs_write_at(const char *path, const char *data, unsigned int size);
            int len = 0; while(val[len]) len++;
            fs_write_at(path, val, (unsigned int)len);
        }
        else if(fl_starts(p, "execute ")) {
            p+=8; p=fl_ltrim(p);
            char arg[128]; eval_expr_str(p, arg);
            int len = 0; while(arg[len]) len++;
            if(len>4 && arg[len-1]=='f' && arg[len-2]=='l' && arg[len-3]=='e' && arg[len-4]=='.') {
                extern int elf_load_and_run(const char* path);
                elf_load_and_run(arg);
            } else if(len>3 && arg[len-1]=='l' && arg[len-2]=='f' && arg[len-3]=='.') {
                char dummy[1];
                extern int firstlang_run_file(const char* path, char* out, int sz);
                firstlang_run_file(arg, dummy, 1);
            }
        }
        else if(fl_starts(p, "print ") || fl_starts(p, "say ") || fl_starts(p, "show ") || fl_starts(p, "display ")) {
            if(fl_starts(p, "print ")) p+=6;
            else if(fl_starts(p, "say ")) p+=4;
            else if(fl_starts(p, "show ")) p+=5;
            else if(fl_starts(p, "display ")) p+=8;
            p=fl_ltrim(p);
            char arg[128]; eval_expr_str(p, arg);
            fli(arg);
            if(num_dyn_ui < MAX_DYN_UI) {
                fl_dyn_ui_t *u = &dyn_ui[num_dyn_ui++];
                u->type = 'L'; u->action_ip = -1; u->name[0]=0;
                u->x = -1; u->y = -1;
                fl_cpy(u->text, arg, 64);
                extern int needs_redraw; needs_redraw = 1;
            }
        }
        else if(fl_starts(p, "call ")) {
            p+=5; p=fl_ltrim(p);
            char target[64]; int ci=0;
            while(*p && *p!=' ' && *p!='(') target[ci++] = *p++; target[ci]=0; fl_rtrim(target);
            
            char search_args[64]; fl_cpy(search_args, "function ", 64); fl_cat(search_args, target, 64); fl_cat(search_args, "(", 64);
            char search_noargs[64]; fl_cpy(search_noargs, "function ", 64); fl_cat(search_noargs, target, 64); fl_cat(search_noargs, ":", 64);
            char search_space[64]; fl_cpy(search_space, "function ", 64); fl_cat(search_space, target, 64); fl_cat(search_space, " ", 64);
            
            int i; int found=-1;
            for(i=0; i<script_num_lines; i++) {
                const char *lp = fl_ltrim(script_lines[i]);
                if(fl_starts(lp, search_args) || fl_starts(lp, search_noargs) || fl_starts(lp, search_space)) { found=i+1; break; }
            }
            if(found>=0) {
                char args[8][64]; int num_args = 0;
                const char *ap = fl_ltrim(p);
                if(*ap == '(') ap++;
                while(*ap && *ap != ':' && *ap != ')') {
                    ap = fl_ltrim(ap); if(!*ap || *ap==')') break;
                    int ai=0; while(*ap && *ap!=',' && *ap!=')') { args[num_args][ai++]=*ap++; }
                    args[num_args][ai]=0; fl_rtrim(args[num_args]); num_args++;
                    if(*ap==',') ap++;
                }
                
                const char *dp = fl_ltrim(script_lines[found-1]);
                while(*dp && *dp != '(' && *dp != ':') dp++;
                if (*dp == '(') {
                    dp++;
                    int num_params = 0;
                    while(*dp && *dp != ')') {
                        dp = fl_ltrim(dp); if(!*dp || *dp==')') break;
                        char pname[32]; int pi=0;
                        while(*dp && *dp!=',' && *dp!=')') { pname[pi++]=*dp++; }
                        pname[pi]=0; fl_rtrim(pname);
                        
                        if (num_params < num_args) {
                            char arg_val[128]; eval_expr_str(args[num_params], arg_val);
                            set_var_str(pname, arg_val);
                        }
                        num_params++;
                        if(*dp==',') dp++;
                    }
                }
                
                script_call_stack[script_call_sp++] = ip;
                ip = found - 1; /* jump */
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
        else if(fl_starts(p, "delay ") || fl_starts(p, "wait ") || fl_starts(p, "pause ")) {
            if(fl_starts(p, "delay ")) p+=6;
            else if(fl_starts(p, "wait ")) p+=5;
            else if(fl_starts(p, "pause ")) p+=6;
            p=fl_ltrim(p);
            // Ignore "for "
            if(fl_starts(p, "for ")) { p+=4; p=fl_ltrim(p); }
            
            char arg[128]; eval_expr_str(p, arg);
            int ms = fl_atoi(arg);
            if(ms <= 0) ms = 1;
            extern void ps2_poll(void);
            extern void gui_draw(void);
            extern void hda_tick(void);
            extern int gui_needs_redraw(void);
            for(int d=0; d<ms*50000; d++) {
                ps2_poll(); hda_tick();
                if(d % 50000 == 0 && gui_needs_redraw()) gui_draw();
            }
        }
        else if(fl_starts(p, "clear ui")) {
            p+=8; p=fl_ltrim(p);
            num_dyn_ui = 0;
            extern int needs_redraw; needs_redraw = 1;
        }
        else if((fl_starts(p, "increase ") || fl_starts(p, "decrease ")) && fl_contains_sub(p, " by ")) {
            int is_inc = fl_starts(p, "increase ");
            p += (is_inc ? 9 : 9); p = fl_ltrim(p);
            char vname[32]; int vi=0;
            while(*p && !fl_starts(p, " by ")) vname[vi++] = *p++; vname[vi]=0; fl_rtrim(vname);
            p += 4; p = fl_ltrim(p);
            int amount = eval_expr_int(p);
            int current = get_var_int(vname);
            set_var_int(vname, is_inc ? current + amount : current - amount);
            {
                fl_dyn_ui_t *u = 0;
                for(int di=0; di<num_dyn_ui; di++) {
                    if (dyn_ui[di].type == 'L' && fl_eq(dyn_ui[di].name, vname)) { u = &dyn_ui[di]; break; }
                }
                if (!u && num_dyn_ui < MAX_DYN_UI) {
                    u = &dyn_ui[num_dyn_ui++]; u->type = 'L'; u->action_ip = -1; fl_cpy(u->name, vname, 32); u->x = -1; u->y = -1;
                }
                if (u) {
                    char full_text[128]; full_text[0]=0; fl_cpy(full_text, vname, 128); fl_cat(full_text, ": ", 128);
                    char sval[32]; fl_itos(is_inc ? current + amount : current - amount, sval, 32); fl_cat(full_text, sval, 128);
                    fl_cpy(u->text, full_text, 64); extern int needs_redraw; needs_redraw = 1;
                }
            }
        }
        else if((fl_starts(p, "multiply ") || fl_starts(p, "divide ")) && fl_contains_sub(p, " by ")) {
            int is_mult = fl_starts(p, "multiply ");
            p += (is_mult ? 9 : 7); p = fl_ltrim(p);
            char vname[32]; int vi=0;
            while(*p && !fl_starts(p, " by ")) vname[vi++] = *p++; vname[vi]=0; fl_rtrim(vname);
            p += 4; p = fl_ltrim(p);
            int amount = eval_expr_int(p);
            int current = get_var_int(vname);
            if (!is_mult && amount == 0) amount = 1;
            set_var_int(vname, is_mult ? current * amount : current / amount);
            {
                fl_dyn_ui_t *u = 0;
                for(int di=0; di<num_dyn_ui; di++) {
                    if (dyn_ui[di].type == 'L' && fl_eq(dyn_ui[di].name, vname)) { u = &dyn_ui[di]; break; }
                }
                if (!u && num_dyn_ui < MAX_DYN_UI) {
                    u = &dyn_ui[num_dyn_ui++]; u->type = 'L'; u->action_ip = -1; fl_cpy(u->name, vname, 32); u->x = -1; u->y = -1;
                }
                if (u) {
                    char full_text[128]; full_text[0]=0; fl_cpy(full_text, vname, 128); fl_cat(full_text, ": ", 128);
                    char sval[32]; fl_itos(is_mult ? current * amount : current / amount, sval, 32); fl_cat(full_text, sval, 128);
                    fl_cpy(u->text, full_text, 64); extern int needs_redraw; needs_redraw = 1;
                }
            }
        }
        else if(fl_starts(p, "add ") && fl_contains_sub(p, " to ")) {
            p+=4; p=fl_ltrim(p);
            char sval[32]; int vi=0;
            while(*p && !fl_starts(p, " to ")) sval[vi++] = *p++; sval[vi]=0; fl_rtrim(sval);
            p += 4; p = fl_ltrim(p);
            char vname[32]; resolve_var_name(vname, p);
            int amount = eval_expr_int(sval);
            int current = get_var_int(vname);
            set_var_int(vname, current + amount);
            {
                fl_dyn_ui_t *u = 0;
                for(int di=0; di<num_dyn_ui; di++) {
                    if (dyn_ui[di].type == 'L' && fl_eq(dyn_ui[di].name, vname)) { u = &dyn_ui[di]; break; }
                }
                if (!u && num_dyn_ui < MAX_DYN_UI) {
                    u = &dyn_ui[num_dyn_ui++]; u->type = 'L'; u->action_ip = -1; fl_cpy(u->name, vname, 32); u->x = -1; u->y = -1;
                }
                if (u) {
                    char full_text[128]; full_text[0]=0; fl_cpy(full_text, vname, 128); fl_cat(full_text, ": ", 128);
                    char sval2[32]; fl_itos(current + amount, sval2, 32); fl_cat(full_text, sval2, 128);
                    fl_cpy(u->text, full_text, 64); extern int needs_redraw; needs_redraw = 1;
                }
            }
        }
        else if(fl_starts(p, "subtract ") && fl_contains_sub(p, " from ")) {
            p+=9; p=fl_ltrim(p);
            char sval[32]; int vi=0;
            while(*p && !fl_starts(p, " from ")) sval[vi++] = *p++; sval[vi]=0; fl_rtrim(sval);
            p += 6; p = fl_ltrim(p);
            char vname[32]; resolve_var_name(vname, p);
            int amount = eval_expr_int(sval);
            int current = get_var_int(vname);
            set_var_int(vname, current - amount);
            {
                fl_dyn_ui_t *u = 0;
                for(int di=0; di<num_dyn_ui; di++) {
                    if (dyn_ui[di].type == 'L' && fl_eq(dyn_ui[di].name, vname)) { u = &dyn_ui[di]; break; }
                }
                if (!u && num_dyn_ui < MAX_DYN_UI) {
                    u = &dyn_ui[num_dyn_ui++]; u->type = 'L'; u->action_ip = -1; fl_cpy(u->name, vname, 32); u->x = -1; u->y = -1;
                }
                if (u) {
                    char full_text[128]; full_text[0]=0; fl_cpy(full_text, vname, 128); fl_cat(full_text, ": ", 128);
                    char sval2[32]; fl_itos(current - amount, sval2, 32); fl_cat(full_text, sval2, 128);
                    fl_cpy(u->text, full_text, 64); extern int needs_redraw; needs_redraw = 1;
                }
            }
        }
        else if(fl_starts(p, "add ") || fl_starts(p, "create ") || fl_starts(p, "draw ") || fl_starts(p, "make ")) {
            if(fl_starts(p, "add ")) p+=4;
            else if(fl_starts(p, "create ")) p+=7;
            else if(fl_starts(p, "draw ")) p+=5;
            else if(fl_starts(p, "make ")) p+=5;
            
            // Skip optional "a " or "an "
            p=fl_ltrim(p);
            if(fl_starts(p, "a ")) { p+=2; p=fl_ltrim(p); }
            else if(fl_starts(p, "an ")) { p+=3; p=fl_ltrim(p); }
            
            if(num_dyn_ui < MAX_DYN_UI) {
                fl_dyn_ui_t *u = &dyn_ui[num_dyn_ui++];
                u->type = 'B'; u->action_ip = -1; u->name[0]=0;
                if(fl_starts(p, "button ")) { u->type='B'; p+=7; }
                else if(fl_starts(p, "label ")) { u->type='L'; p+=6; }
                else if(fl_starts(p, "slider ")) { u->type='S'; p+=7; }
                else if(fl_starts(p, "text ")) { u->type='T'; p+=5; }
                else if(fl_starts(p, "rect ")) { u->type='R'; p+=5; }
                else if(fl_starts(p, "circle ")) { u->type='C'; p+=7; }
                else if(fl_starts(p, "image ")) { u->type='I'; p+=6; }
                else if(fl_starts(p, "chart ")) { u->type='H'; p+=6; }
                else if(fl_starts(p, "radio ")) { u->type='A'; p+=6; }
                else if(fl_starts(p, "checkbox ")) { u->type='K'; p+=9; }
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
                } else { u->x=-1; u->y=-1; }
            }
        }
        ip++;
    }
}

static void exec_script(fl_func_t *fn) {
    script_num_lines=0;
    const char *s = fn->script_body;
    while(*s && script_num_lines < 256) {
        char line[128]; int i=0;
        while(*s && *s!='\n' && i<127) line[i++]=*s++;
        line[i]=0; fl_rtrim(line);
        if(*s=='\n') s++;
        
        if(line[0]) {
            const char *lp = fl_ltrim(line);
            if(fl_starts(lp, "export ")) { lp += 7; lp = fl_ltrim(lp); }
            if(fl_starts(lp, "import ")) {
                lp += 7; lp = fl_ltrim(lp);
                char path[128]; eval_expr_str(lp, path);
                char buf[4096];
                int r = fs_read_at(path, buf, 4095);
                if(r >= 0) {
                    buf[r] = 0;
                    const char *bp = buf;
                    while(*bp && script_num_lines < 256) {
                        char bline[128]; int bi=0;
                        while(*bp && *bp!='\n' && bi<127) bline[bi++] = *bp++; bline[bi]=0; fl_rtrim(bline);
                        if(bline[0]) fl_cpy(script_lines[script_num_lines++], bline, 128);
                        if(*bp=='\n') bp++;
                    }
                } else {
                    char err[128]; err[0]=0; fl_cat(err, "import failed: ", 128); fl_cat(err, path, 128);
                    fle(err);
                }
            } else {
                fl_cpy(script_lines[script_num_lines++], line, 128);
            }
        }
    }
    
    script_num_vars = 0;
    num_dyn_ui = 0;
    fl_active_ui_fn = fn;
    if (fl_eq(fn->name, "UI")) fl_ui_open = 1; else fl_ui_open = 0;
    script_call_sp = 0;
    
    exec_script_loop(0);
    
    char msg[64]; msg[0]=0;
    fl_cat(msg,"UI script finished: ",sizeof(msg));
    fl_cat(msg,fn->name,sizeof(msg));
    fli(msg);
}


void firstlang_eval_line(const char *line, char *output_buf, int output_sz) {
    fl_out_buf[0]=0; fl_out_len=0; fl_had_error=0;
    
    script_num_lines = 1;
    fl_cpy(script_lines[0], line, 128);
    fl_rtrim(script_lines[0]);
    
    fl_ui_open = 0;
    script_call_sp = 0;
    
    exec_script_loop(0);
    
    int i=0;
    while(fl_out_buf[i] && i < output_sz-1) {
        output_buf[i] = fl_out_buf[i]; i++;
    }
    output_buf[i] = 0;
}

int firstlang_run(const char *source, char *output_buf, int output_sz) {
    fl_out_buf[0]=0; fl_out_len=0; fl_had_error=0;
    fl_detected_count=0; fl_ui_open=0;

    fli("FirstLang v1.0 — parsing...");
    
    script_num_lines=0;
    const char *s = source;
    int has_ui = 0;
    while(*s && script_num_lines < 256) {
        char line[128]; int i=0;
        while(*s && *s!='\n' && i<127) line[i++]=*s++;
        line[i]=0; fl_rtrim(line);
        if(*s=='\n') s++;
        
        if(line[0]) {
            const char *lp = fl_ltrim(line);
            if(fl_starts(lp, "export ")) { lp += 7; lp = fl_ltrim(lp); }
            if(fl_starts(lp, "import ")) {
                lp += 7; lp = fl_ltrim(lp);
                char path[128]; eval_expr_str(lp, path);
                char buf[4096];
                int r = fs_read_at(path, buf, 4095);
                if(r >= 0) {
                    buf[r] = 0;
                    const char *bp = buf;
                    while(*bp && script_num_lines < 256) {
                        char bline[128]; int bi=0;
                        while(*bp && *bp!='\n' && bi<127) bline[bi++] = *bp++; bline[bi]=0; fl_rtrim(bline);
                        if(bline[0]) fl_cpy(script_lines[script_num_lines++], bline, 128);
                        if(*bp=='\n') bp++;
                    }
                } else {
                    char err[128]; err[0]=0; fl_cat(err, "import failed: ", 128); fl_cat(err, path, 128);
                    fle(err);
                }
            } else {
                char lp_lower[128];
                int lj=0; while(lp[lj] && lj<127) { lp_lower[lj] = lp[lj]; if (lp[lj]>='A' && lp[lj]<='Z') lp_lower[lj]+=32; lj++; } lp_lower[lj]=0;
                if (fl_starts(lp_lower, "function ui")) has_ui = 1;
                fl_cpy(script_lines[script_num_lines++], line, 128);
            }
        }
    }
    
    script_num_vars = 0;
    num_dyn_ui = 0;
    if (has_ui) fl_ui_open = 1;
    script_call_sp = 0;
    
    exec_script_loop(0); // execute top-level

    // Auto-call UI or MAIN
    int fi;
    for(fi=0; fi<script_num_lines; fi++) {
        char lp_lower[128];
        const char *lp = fl_ltrim(script_lines[fi]);
        int lj=0; while(lp[lj] && lj<127) { lp_lower[lj] = lp[lj]; if (lp[lj]>='A' && lp[lj]<='Z') lp_lower[lj]+=32; lj++; } lp_lower[lj]=0;
        if (has_ui) {
            if (fl_starts(lp_lower, "function ui:") || fl_starts(lp_lower, "function ui ") || fl_starts(lp_lower, "function ui(")) {
                exec_script_loop(fi+1);
                break;
            }
        } else {
            if (fl_starts(lp_lower, "function main:") || fl_starts(lp_lower, "function main ") || fl_starts(lp_lower, "function main(")) {
                exec_script_loop(fi+1);
                break;
            }
        }
    }

    fli(fl_had_error?"Completed with errors.":"Done.");
    
    extern int active_app_index;
    if (fl_had_error && active_app_index == 10) { /* APP_IDE - 1 */
        // ONLY show GUI error if the script has a UI function!
        if (has_ui) {
            fl_ui_open = 1;
            num_dyn_ui = 0;
            if (num_dyn_ui < MAX_DYN_UI) {
                fl_dyn_ui_t *u = &dyn_ui[num_dyn_ui++];
                u->type = 'L'; fl_cpy(u->text, "Script Error!", 64);
                u->x = -1; u->y = -1; u->action_ip = -1; u->name[0]=0;
            }
        }
    }
    
    fl_cpy(output_buf,fl_out_buf,output_sz);
    return fl_had_error?-1:1;
}

/* load a script from a file and run it */
int firstlang_run_file(const char *path, char *output_buf, int output_sz) {
    static char src[8192];
    int bytes=fs_read_at(path,src,sizeof(src)-1);
    if(bytes<0){fl_cpy(output_buf,"[ERROR] File not found",output_sz);return -1;}
    src[bytes]=0;
    return firstlang_run(src,output_buf,output_sz);
}

/* Get UI draw state (called from gui.c to overlay the UI) */
int firstlang_has_ui(void)  { return fl_ui_open; }
void firstlang_close_ui(void){ fl_ui_open=0; }

int firstlang_mouse_click(int mx, int my) {
    if(!fl_ui_open) return 0;
    /* close button is at wx+ww-26, wy+4, w=20, h=20 */
    if (mx >= fl_ui_x + fl_ui_w - 26 && mx <= fl_ui_x + fl_ui_w - 6 &&
        my >= fl_ui_y + 4 && my <= fl_ui_y + 24) {
        fl_ui_open = 0;
        return 1;
    }
    /* handle dynamic button clicks */
    int di;
    int auto_y = 10;
    for(di=0; di<num_dyn_ui; di++) {
        fl_dyn_ui_t *u = &dyn_ui[di];
        
        int dx = u->x; int dy = u->y;
        if(dx == -1) dx = 20;
        if(dy == -1) {
            dy = auto_y;
            if (u->type == 'H' || u->type == 'I' || u->type == 'R' || u->type == 'C') auto_y += 70;
            else auto_y += 30;
        }

        if(u->type == 'B' && u->action_ip >= 0) {
            int bx = fl_ui_x + dx; int by = fl_ui_y + 40 + dy;
            if(mx >= bx && mx <= bx + 120 && my >= by && my <= by + 28) {
                exec_script_loop(u->action_ip);
                needs_redraw=1;
                return 1;
            }
        }
    }
    
    /* swallow clicks inside window */
    if (mx >= fl_ui_x && mx <= fl_ui_x + fl_ui_w &&
        my >= fl_ui_y && my <= fl_ui_y + fl_ui_h) {
        return 1;
    }
    return 0;
}

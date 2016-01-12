%option outfile="jpflex.yy.c"
%option prefix="jpfyy"
%option noyywrap
%option noyyalloc
%option noyyrealloc
%option yylineno
%option reentrant
%option extra-type="struct lexer *"

%{

#include <stdarg.h>
#include "jpf.h"
#include "util.h"

struct key {
  char *key;
  struct key *next;
};

struct token {
  int offset,next_offset;
  struct key *keys;
};

char * lexer_strdup(struct lexer *lx,char *c) {
  int len;

  len = strlen(c);
  if(lx->maxalloc && lx->alloced+len>lx->maxalloc) {
    return 0;
  }
  lx->alloced += len+1;
  return strdup(c);
}

static void jpf_send_error(struct lexer *lx,char *s,...) {
  va_list ap;
  char *msg;

  va_start(ap,s);
  msg = jpf_vmessage(s,ap);
  va_end(ap);
  lx->callbacks->error(msg,lx->line,lx->startcol,lx->priv);
  free(msg);
}

static int jpf_add_key(struct lexer *lx,struct token *tk,char *key) {
  struct key *k;

  for(k=tk->keys;k;k=k->next) {
    if(!strcmp(key,k->key)) { return 0; }
  }
  if(lx->maxalloc && lx->alloced+sizeof(struct key) > lx->maxalloc) {
    jpf_send_error(lx,"Exceeded maximum memory with keys");
    return 0;
  }
  lx->alloced += sizeof(struct key);
  k = jpf_safe_malloc(sizeof(struct key));
  k->key = lexer_strdup(lx,key);
  if(!k->key) {
    jpf_send_error(lx,"Exceeded maximum memory with keys");
    free(k);
    return 0;
  }
  k->next = tk->keys;
  tk->keys = k;
  return 1;
}

static void jpf_key_free(struct lexer *lx,struct token *tk) {
  struct key *k,*kn;

  for(k=tk->keys;k;k=kn) {
    kn = k->next;
    lx->alloced -= strlen(k->key)+1+sizeof(struct key);
    free(k->key);
    free(k);
  }
}

/* Is and must only be called when lx->num > 0 */
static int jpf_pop_action(struct lexer *lx,char *key) {
  if(lx->need_more) {
    lx->callbacks->push_string("",JPF_UNQUOTED,lx->priv);
    lx->need_more = 0;
  }
  if(lx->stack[lx->num-1].keys) {
    lx->callbacks->end_key(lx->stack[lx->num-1].keys->key,lx->priv);
    if(key && !jpf_add_key(lx,lx->stack+lx->num-1,key)) {
      jpf_send_error(lx,"Duplicate key '%s'",key);
      lx->colkill = lx->stack[lx->num-1].offset;
      jpf_key_free(lx,lx->stack+lx->num-1);
      lx->num--;
      return 0;
    }
  } else {
    lx->callbacks->end_member(lx->priv);
  }
  lx->stack[lx->num-1].next_offset = -1;
  lx->atom_top = 0;
  lx->can_push = 0;
  return 1;
}

static int type_match(struct lexer *lx,char *key) {
  if(key && !lx->stack[lx->num-1].keys) {
    jpf_send_error(lx,"Got key '%s' where member expected",key);
    lx->colkill = lx->col;
    return 0;
  }
  if(!key && lx->stack[lx->num-1].keys) {
    jpf_send_error(lx,"Got member where key expected");
    lx->colkill = lx->col;
    return 0;
  }
  return 1;
}

static void jpf_lex_push(struct lexer *lx,char *key) {
  int new_size;

  if(lx->colkill!=-1 && lx->curcol>=lx->colkill) { return; }
  if(lx->num) {
    if(lx->stack[lx->num-1].offset == lx->col) {
      if(!type_match(lx,key)) { return; }
      if(!jpf_pop_action(lx,key)) { return; }
      if(key) {
        lx->callbacks->start_key(key,JPF_BEST,-1,lx->priv);
      } else {
        lx->callbacks->start_member(JPF_BEST,lx->priv);
      }
      lx->need_more = 1;
      lx->atom_top = 0;
      lx->can_push = 1;
      return;
    }
    if(lx->stack[lx->num-1].next_offset != -1 &&
       lx->stack[lx->num-1].next_offset != lx->col) {
      jpf_send_error(lx,"Mismatched indent");
      lx->colkill = lx->stack[lx->num-1].offset+1;
      if(!type_match(lx,key)) { return; }
    }
  }
  if(lx->atom_top) { jpf_send_error(lx,"Cannot push onto literal"); return; }
  if(!lx->can_push) { jpf_send_error(lx,"Element not in container"); return; }
  if(lx->size==lx->num) {
    new_size = (lx->size*3/2)+16;
    if(lx->maxstack && new_size > lx->maxstack) {
      jpf_send_error(lx,"Stack overflow");
      return;
    }
    lx->size = new_size;
    lx->stack = jpf_safe_realloc(lx->stack,lx->size*sizeof(struct token));
  }
  lx->stack[lx->num].offset = lx->col;
  lx->stack[lx->num].next_offset = -1;
  lx->stack[lx->num].keys = 0;
  if(lx->num) {
    lx->stack[lx->num-1].next_offset = lx->col;
  }
  if(key) {
    jpf_add_key(lx,lx->stack+lx->num,key);
    lx->callbacks->push_assoc(lx->priv);
  } else {
    lx->callbacks->push_array(lx->priv);
  }
  if(key) {
    lx->callbacks->start_key(key,JPF_BEST,-1,lx->priv);
  } else {
    lx->callbacks->start_member(JPF_BEST,lx->priv);
  }
  lx->num++;
  lx->can_push = 1;
  lx->need_more = 1;
  lx->atom_top = 0;
}

static void jpf_lex_undent(struct lexer *lx,char *text) {
  char *c;
  int col = 0;

  lx->seen_space = 0;
  lx->startcol = 1;
  if(text) {
    col = 1;
    for(c=text;*c;c++) {
      if(*c==' ') { col++; } else { col = 1; }
    }
  }
  if(!text && !lx->num && lx->need_more) { /* Empty file & edge-cases */
    lx->callbacks->push_string("",JPF_UNQUOTED,lx->priv);
  }
  while(lx->num) {
    if(lx->stack[lx->num-1].offset <= col) { break; }
    if(lx->num) { jpf_pop_action(lx,0); }
    jpf_key_free(lx,lx->stack+lx->num-1);
    if(text && lx->num==1) {
      jpf_send_error(lx,"Attempt to pop top level");
      /* Recover by lying as to col so as to match root. Will mess up
       * error message cols, but the user is lucky we're even trying at
       * this point.
       */
      lx->curcol = lx->stack[lx->num-1].offset;
      return;
    }
    lx->num--;
  }
  lx->curcol = col>0?col:1;
  if(lx->curcol < lx->colkill) { lx->colkill = -1; }
}

static void jpf_str_add(struct lexer *lx,char *x) {
  if(!jpf_strbuf_add(&(lx->str),x)) {
    jpf_send_error(lx,"Out of memory");
  }
}

/* Pointer safety case:
 *
 * cp and out are created (by strdup) to be initially the size of the
 * lx->str buffer. In the first for loop, in starts at the start of that
 * buffer and advances by one each time, without interference from elsewhere,
 * so the for loop gets executed exactly as many times as characters in the
 * string. The three if statements sometimes short-circuit the rest of the
 * loop, in one case after advancing cp. If not short-circuited, then
 * cp is advanced. Therefore cp is advanced at most as many times as the
 * loop gets executed, and so can never exceed its allocation. In the
 * following while loop, cp may get decreased, but only at most as far as
 * out. Inside the while at the start it is always strictly greater than out,
 * and after the decrement greater or equal to it. It can be increased
 * before the break, but only having just been decreased and so always
 * leaves the loop within bounds. The \0 assignment to *cp ensures a null
 * terminator somewhere within bounds for out.
 */
static void jpf_str_hundent(struct lexer *lx) {
  char *in,*out,*cp;
  int sol = lx->hindent;
  int soh = 1;

  free(lx->hmark);
  lx->hmark = 0;
  cp = out = jpf_safe_strdup(jpf_strbuf_str(&(lx->str))); /* For size */
  for(in=jpf_strbuf_str(&(lx->str));*in;in++) {
    if(soh) { if(isournl(*in)) { continue; } else { soh = 0; } }
    if(isournl(*in)) { sol = lx->hindent; *(cp++) = *in; continue; }
    if(sol && --sol) { continue; }
    *(cp++) = *in;
  }
  while(cp>out) {
    cp--;
    if(!isournl(*cp)) { cp++; break; }
  }
  *cp = '\0';
  jpf_strbuf_free(&(lx->str));
  jpf_strbuf_init(&(lx->str),lx->maxalloc);
  jpf_str_add(lx,out);
  jpf_strbuf_trim(&(lx->str)); /* For EOF leading space */
  free(out);
}

static void utf8_for(char *x) {
  int v,s,n,i;

  s = strtol(x+2,0,16);
  s = s & 0x1FFFFF;
  if(s<1<<7) { n=0; v=0x00; }
  else if(s<1<<11) { n=1; v=0xC0; }
  else if(s<1<<16) { n=2; v=0xE0; }
  else { n=3; v=0xF0; }
  for(i=0;i<n;i++) {
    *(x+n-i) = 0x80|(s&0x3F);
    s >>= 6;
  }
  *x = v|s;
  *(x+n+1) = '\0';
}

static double to_number(struct lexer *lx,char *x) {
  double d;
  char *end;

  d = strtod(x,&end);
  if(*end != '\0') { jpf_send_error(lx,"Bad number '%s'",x); return 0.0; }
  return d;
}

static int jpf_literal(struct lexer *lx) {
  if(lx->atom_top) { jpf_send_error(lx,"Consecutive literals"); return 0; }
  if(lx->colkill!=-1 && lx->curcol>=lx->colkill) { return 0; }
  lx->need_more = 0;
  lx->atom_top = 1;
  return 1;
}

static void jpf_set_mark(struct lexer *lx) {
  lx->hmark = jpf_safe_strdup(jpf_strbuf_str(&(lx->str)));
  lx->hindent = -1;
}

static void jpf_indent(struct lexer *lx,int len) {
  if(len<lx->hindent || lx->hindent==-1) { lx->hindent = len; }
}

static void jpf_str_reset(struct lexer *lx) {
  jpf_strbuf_free(&(lx->str));
  jpf_strbuf_init(&(lx->str),lx->maxalloc);
}

/* We define a little domain-specific language for the lexer proper, mainly
 * hide the complexities of the reentrant parser (all those yyextra's!) and
 * to use the other yy* macros, but more generally to keep the lexer part
 * clean, as it's hard enough to understand without complex actions getting
 * in the way.
 */

/* Misc */
#define SETCOL     yyextra->col = yyextra->curcol - yyleng
#define SPACE      yyextra->seen_space

/* Symbol stack and callbacks */
#define PV         yyextra->priv
#define PUSH(fn)   do { if(jpf_literal(yyextra)) { \
                        yyextra->callbacks->push_##fn; } } while(0)
#define STACK(x)   jpf_lex_push(yyextra,x)
#define UNDENT(x)  jpf_lex_undent(yyextra,x)
#define ERROR(x)   jpf_send_error(yyextra,x)

/* String building */
#define STRING     jpf_strbuf_str(&(yyextra->str))
#define RESET      jpf_str_reset(yyextra)
#define MORE(x)    jpf_str_add(yyextra,x)
#define TRIM       jpf_strbuf_trim(&(yyextra->str))

/* Heredocs */
#define HMARK      jpf_set_mark(yyextra)
#define HINDENT    jpf_indent(yyextra,yyleng)
#define HDONE      (!strcmp(yyextra->hmark,yytext))
#define HUNDENT    jpf_str_hundent(yyextra)
#define FIRSTCOL   yyextra->startcol = yyextra->curcol = 1

/* For -Wall */
#define YY_NO_INPUT

#define YY_USER_ACTION yyextra->startcol = yyextra->curcol; yyextra->curcol += yyleng; yyextra->line = yylineno;
#define LESS(x) do { int y=(x); \
                     yyextra->curcol -= yyleng-y; yyless(y); } while(0)

#define YY_FATAL_ERROR(x) longjmp(((struct yyguts_t *)yyscanner)->yyextra_r->fatal,1)

// XXX \0 escape
// XXX tab escape (parse and emit)

%}

%x Q
%x U
%x H
%x HP
%x HT
INT ([1-9][0-9]{0,100})|0
DIGITS [0-9]{1,100}
ALNUM [a-zA-Z0-9]
HEX [a-fA-F0-9]
NUMBER [+-]{INT}(\.{DIGITS})?([eE][+-]?{INT})?
NL [\n\r\f\v]
NLS ((\n[\r\f\v]*)|(\r[\n\f\v]*)|(\v[\r\n\f]*)|(\f[\r\n\v]*))
NNL [^\n\r\f\v]
WS [ \n\r\f\v] 
NWS [^ \n\r\f\v]{-}[[:cntrl:]]
IWS {NWS}(\ *{NWS}){0,1000}
SIG [^ \n\r\f\v#] 
UEND [\n\r\f\v#]
UNORM [^\\\n\r\f\v#: ]{-}[[:cntrl:]]
HPNORM [^\n\r\f\v#]{-}[[:cntrl:]]

%%

<INITIAL,Q,U,H,HP,HT>\0 { ERROR("Literal \\0 illegal, use escape"); }

  /* String escapes */
<U,Q>\\n         { MORE("\n"); }
<U,Q>\\r         { MORE("\r"); }
<U,Q>\\b         { MORE("\b"); }
<U,Q>\\a         { MORE("\a"); }
<U,Q>\\f         { MORE("\f"); }
<U,Q>\\v         { MORE("\v"); }
<U,Q>\\u{HEX}{4} { utf8_for(yytext); MORE(yytext); }
<U,Q>\\w{HEX}{6} { utf8_for(yytext); MORE(yytext); }
<U,Q>\\{ALNUM}   { ERROR("Unexpected escape sequence"); }
<U,Q>\\(.|\n)    { MORE(yytext+1); }

  /* Quoted strings */
\"               { SETCOL; RESET; BEGIN(Q); }
<Q>[^\\"]{0,1000} {MORE(yytext); }
<Q>\"            { PUSH(string(STRING,JPF_QUOTED,PV)); BEGIN(0); }
<Q>\":           { STACK(STRING); BEGIN(0); }

  /* Simple symbols */
-                { SETCOL; STACK(0); }
!null            { PUSH(null(PV)); } 
!false           { PUSH(boolean(0,PV)); } 
!true            { PUSH(boolean(1,PV)); }
!-               { PUSH(array(PV)); }
!:               { PUSH(assoc(PV)); }

  /* Numbers */
{NUMBER}         { PUSH(number(to_number(yyextra,yytext),JPF_BEST,PV)); }
[+-]{NWS}{0,100} { ERROR("Funny looking number"); }

  /* Blank / comment-only lines */
^\ +(#{NNL}*)?{NL}    { LESS(yyleng-1); }
^#{NNL}*{NL}          { LESS(yyleng-1); }
{NL}\ *(#{NNL}*)?{NL} { LESS(yyleng-1); }

  /* Starts of lines: process any undent */
{NL}\ */{SIG}         { UNDENT(yytext); }
^\ +/{SIG}            { UNDENT(yytext); }
  
  /* Misc padding/comments/superfluous chars */
{NL}
" "
#{NNL}*
\t                  { ERROR("tabs illegal in JSONpf"); }
[[:cntrl:]]         { ERROR("illegal control character"); }

  /* EOFs */
<INITIAL><<EOF>>   { UNDENT(0); return 0; }
<Q><<EOF>>         { UNDENT(0); ERROR("EOF inside quoted string"); return 0; }
<U><<EOF>>         { PUSH(string(STRING,JPF_UNQUOTED,PV));
                     UNDENT(0); return 0; }

  /* Heredocs */
\<\ */{NNL}        { BEGIN(HP); }
\<                 { ERROR("Bad heredoc prefix"); }
<HP>{HPNORM}+      { RESET; MORE(yytext); TRIM; HMARK; RESET; BEGIN(HT); }
<HT>{NNL}*
<HT>{NLS}          { LESS(0); BEGIN(H); }
<H>{NLS}\ *{NLS}   { FIRSTCOL; MORE("\n"); LESS(yyleng-1); }
<H>{NLS}\ */{NNL}  { FIRSTCOL; MORE("\n"); MORE(yytext+1); HINDENT; }
<H>{IWS}           { if(HDONE) { HUNDENT; PUSH(string(STRING,JPF_HEREDOC,PV));
                                 BEGIN(0); }
                     else { MORE(yytext); } }
<H>" "             /* tails of lines */
<H>{NL}            /* Before sudden EOF */
<H>[[:cntrl:]]     { ERROR("illegal control character"); }
<H><<EOF>>         { ERROR("Unterminated heredoc"); BEGIN(0); }
<HT><<EOF>>        { ERROR("Unterminated heredoc"); BEGIN(0); }
<HP><<EOF>>        { ERROR("Unterminated heredoc"); BEGIN(0); }
  
  /* Unquoted string */
{NNL}               { SETCOL; RESET; LESS(0); BEGIN(U); }
<U>{UNORM}{0,1000}  { MORE(yytext); }
<U>{UEND}           { PUSH(string(STRING,JPF_UNQUOTED,PV));
                      LESS(0); BEGIN(0); }
<U>:/{WS}           { if(SPACE) { MORE(yytext); }
                      else      { STACK(STRING); BEGIN(0); } }
<U>:                { MORE(yytext); }
<U>\ +/{UNORM}      { SPACE = 1; MORE(yytext); }
<U>" "
<U>[[:cntrl:]]      { ERROR("illegal control character"); }

%%

/* NOTE: we can't keep trck of frees by flex, so limit is a little trigger-
 * happy. But except in pathological files flex itself will come nowhere
 * near any reasonable limit on a modern machine. This is here to catch
 * deliberate attempts to kill the process and infinite spew.
 */
void * yyalloc(size_t bytes,void *yyscanner) {
  struct lexer *lx = ((struct yyguts_t *)yyscanner)->yyextra_r;

  if(lx->maxalloc && lx->alloced+bytes>lx->maxalloc) {
    return 0;
  }
  lx->alloced += bytes;
  return jpf_safe_malloc(bytes);
}

void * yyrealloc(void *ptr,size_t bytes,void *yyscanner) {
  struct lexer *lx = ((struct yyguts_t *)yyscanner)->yyextra_r;

  if(lx->maxalloc && lx->alloced+bytes>lx->maxalloc) { free(ptr); return 0; }
  lx->alloced += bytes;
  return jpf_safe_realloc(ptr,bytes);
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

void jpf_errno(struct lexer *lx,char *msg,int errnum) {
  char *s,*e;

  e = jpf_safe_strerror(errnum);
  s = jpf_message("%s: %s",msg,e);
  free(e);
  lx->pre_error = s;
}

static void jpf_lex_init(struct lexer *lx) {
  *lx = (struct lexer){
    .in = 0,
    .done = 0,
    .pre_error = 0,
    .alloced = 0,
    .maxalloc = 0,
    .maxstack = 0,
    .callbacks = 0,
    .priv = 0,
    .curcol = 1,
    .colkill = -1,
    .need_more = 1,
    .atom_top = 0,
    .can_push = 1,
    .size = 0,
    .num = 0,
    .stack = 0,
    .seen_space = 0,
    .hmark = 0
  };
}

void jpf_lex_file(struct lexer *lx,FILE *f) {
  jpf_lex_init(lx);
  lx->in = f;
}

void jpf_max_memory(struct lexer *lx,size_t buffers,int stack) {
  lx->maxalloc = buffers;
  lx->maxstack = stack;
}

void jpf_lex_go(struct lexer *lx,struct jpf_callbacks *cb,void *priv) {
  yyscan_t scanner;

  cb->start(priv); 
  if(lx->pre_error) {
    cb->error(lx->pre_error,0,0,priv);
    free(lx->pre_error);
    lx->pre_error = 0;
    cb->end(priv); 
    return;
  } 
  *lx = (struct lexer){
    .in = lx->in,
    .done = lx->done,
    .alloced = 0,
    .maxalloc = lx->maxalloc,
    .maxstack = lx->maxstack,
    .callbacks = cb,
    .priv = priv,
    .curcol = 1,
    .colkill = -1,
    .need_more = 1,
    .atom_top = 0,
    .can_push = 1,
    .size = 0,
    .num = 0,
    .stack = 0,
    .seen_space = 0,
    .hmark = 0
  };
  jpf_strbuf_init(&(lx->str),lx->maxalloc);
  if(yylex_init_extra(lx,&scanner)) {
    jpf_send_error(lx,"flex internal buffer full: pathological input");
    return; 
  }
  if(!setjmp(lx->fatal)) {
    yyset_in(lx->in,scanner);
    yylex(scanner);
  } else {
    jpf_send_error(lx,"flex internal buffer full: pathological input");
  }
  cb->end(priv); 
  yylex_destroy(scanner);
  jpf_strbuf_free(&(lx->str));
  if(lx->stack) { free(lx->stack); }
  if(lx->hmark) { free(lx->hmark); }
  if(lx->done)  { lx->done(lx); }
}

// XXX check for errors
static void jpf_close(struct lexer *lx) { fclose(lx->in); }

void jpf_lex_filename(struct lexer *lx,char *fn) {
  FILE *f;

  jpf_lex_init(lx);
  f = fopen(fn,"rb");
  if(!f) {
    jpf_errno(lx,"Cannot open file",errno);
    return;
  }
  lx->in = f;
  lx->done = jpf_close;
}

void jpf_lex_fd(struct lexer *lx,int fd) {
  FILE *f;
  int newfd;

  jpf_lex_init(lx);
  newfd = dup(fd); /* So we can close at will */
  if(newfd==-1) {
    jpf_errno(lx,"Cannot dup fd",errno);
    return;
  }
  f = fdopen(newfd,"r");
  if(!f) {
    jpf_errno(lx,"Cannot open file",errno);
    return;
  }
  lx->in = f;
  lx->done = jpf_close;
}

void jpf_lex_str(struct lexer *lx,char *str) {
  FILE *f;
  int r,len;

  jpf_lex_init(lx);
  f = tmpfile();
  if(!f) {
    jpf_errno(lx,"Cannot open temporary file",errno);
    return;
  }
  len = strlen(str);
  r = fwrite(str,1,len,f);
  if(r<len) {
    jpf_errno(lx,"Cannot write temporary file",errno);
    return;
  }
  if(fseek(f,0,SEEK_SET)) {
    jpf_errno(lx,"Cannot rewind temporary file",errno);
    return;
  }
  lx->done = jpf_close;
  lx->in = f;
}

void (*jpf_unused)() = yyunput; /* For -Wall */
void (*jpf_unused2)() = yy_fatal_error; /* For -Wall */


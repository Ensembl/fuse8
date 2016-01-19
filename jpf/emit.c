#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "jpf.h"
#include "util.h"

static int curcol(struct jpf_emitter *em) {
  int i,len,col=1;
  char *str;

  str = jpf_strbuf_str(&(em->buf));
  len = jpf_strbuf_len(&(em->buf));
  for(i=0;i<len;i++) {
    if(isournl(str[len-i-1])) { break; }
    col++;
  }
  return col; 
}

static void indent_push(struct jpf_emitter *em,int v) {
  if(em->size==em->len) {
    em->size = (em->size*3/2)+8;
    em->stack = jpf_safe_realloc(em->stack,em->size*sizeof(int));
  }
  em->stack[em->len++] = v;
}

static void indent_pop(struct jpf_emitter *em) {
  if(em->empty==1) {
    jpf_strbuf_add(&(em->buf),"!-");
  } else if(em->empty==2) {
    jpf_strbuf_add(&(em->buf),"!:");
  }
  em->len--;
  em->nl=1;
  em->empty = 0;
}

static char *spaces(int n) {
  char *c;
  int i;

  c = jpf_safe_malloc(n+1);
  for(i=0;i<n;i++)
    c[i] = ' ';
  c[n] = '\0';
  return c;
}

// XXX support style systematically
// TEST error writing
/* Only ever call at EOF or immediately after NL */
static void send(struct jpf_emitter *em) {
  int len;
  char *error;

  if(!em->outf || em->failed) { return; }
  len = jpf_strbuf_len(&(em->buf));
  if(fwrite(jpf_strbuf_str(&(em->buf)),1,len,em->outf) < len) {
    error = jpf_safe_strerror(errno);
    em->failed = jpf_message("fwrite to file failed: %s",error);
    return;
  }
  jpf_strbuf_free(&(em->buf));
  jpf_strbuf_init(&(em->buf),0);
}

// TEST (more) initial escape unquoted
static void undent(struct jpf_emitter *em,enum style style) {
  char *spp;

  if(style == JPF_VIMPORTANT) {
    jpf_strbuf_add(&(em->buf),"\n\n# ---\n\n");
  }
  if(!em->nl) { return; }
  em->nl = 0;
  jpf_strbuf_add(&(em->buf),"\n");
  if(style == JPF_IMPORTANT) {
    jpf_strbuf_add(&(em->buf),"\n");
  }
  send(em);
  if(!em->len) { return; }
  spp = spaces(em->stack[em->len-1]-1);
  jpf_strbuf_add(&(em->buf),spp);
  free(spp);
}

// XXX full nl support
// TEST Ok heredoc

static int ok_heredoc_line(char *line,struct jpf_strbuf *sep) {
  char c[2];

  if(*line && line[strlen(line)-1] == ' ') { return 0; }
  if(!strncmp(line,jpf_strbuf_str(sep),jpf_strbuf_len(sep))) {
    c[0] = (line[jpf_strbuf_len(sep)]=='1')?'2':'1';
    c[1] = '\0';
    jpf_strbuf_add(sep,c);
  } 
  return 1;
}

static char * ok_heredoc(char *c) {
  char *in,*line;
  int ok;
  struct jpf_strbuf sep;

  jpf_strbuf_init(&sep,0);
  jpf_strbuf_add(&sep,"eof-0");
  line = c;
  for(in=c;*in;in++) {
    if(*in == '\r' || *in == '\f' || *in == '\v') { /* Hassle */
      jpf_strbuf_free(&sep);
      return 0;
    }
    if(*in == '\n') {
      *in = '\0';
      ok = ok_heredoc_line(line,&sep);
      *in = '\n';
      if(!ok) { jpf_strbuf_free(&sep); return 0; }
      line = in+1;
    }
  }
  if(!ok_heredoc_line(line,&sep)) { jpf_strbuf_free(&sep); return 0; }
  return jpf_strbuf_str(&sep);
}

static void start(void *priv) {}

static void end(void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  indent_pop(em);
  jpf_strbuf_add(&(em->buf),"\n\n");
  if(em->outf) {
    send(em);
  } else if(em->outstr) {
    *(em->outstr) = jpf_strbuf_str(&(em->buf));
  }
  if(em->done) { em->done(em); }
}

static void push_array(void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  em->empty = 1;
}

static void push_assoc(void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  em->empty = 2;
}

static void push_null(void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  undent(em,JPF_BEST);
  jpf_strbuf_add(&(em->buf),"!null");
}

static void push_boolean(int v,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  undent(em,JPF_BEST);
  jpf_strbuf_add(&(em->buf),v?"!true":"!false");
}

static void push_number(double v,enum style style,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  undent(em,JPF_BEST);
  switch(style) {
  case JPF_INT:
    jpf_strbuf_add(&(em->buf),"%+"PRId64,(int64_t)v);
    break;
  default:
    jpf_strbuf_add(&(em->buf),"%+lf",v);
    break;
  }
}

// XXX prefer quote start end spaces
// TEST start end spaces
// XXX style bail
// XXX faster jpf_strbuf_add without ..., exploit where useful

static int isouralnum(char c) {
  return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9');
}

static char * escape(char *in,char *esc,int colon,int initp) {
  struct jpf_strbuf out;
  char c[7];
  int init = 1;

  jpf_strbuf_init(&out,0);
  for(;*in;in++) {
    if(*in=='\n') { jpf_strbuf_add(&out,"\\n"); }
    else if(*in=='\r') { jpf_strbuf_add(&out,"\\r"); }
    else if(*in=='\v') { jpf_strbuf_add(&out,"\\v"); }
    else if(*in=='\f') { jpf_strbuf_add(&out,"\\f"); }
    else if((*in>=0 && *in<32) || *in==127) {
      snprintf(c,7,"\\u%4.4X",(unsigned char)*in);
      c[6] = '\0';
      jpf_strbuf_add(&out,c);
    } else if(strchr(esc,*in)) {
      jpf_strbuf_add(&out,"\\");
      snprintf(c,2,"%c",*in);
      jpf_strbuf_add(&out,c);
    } else if(colon && *in == ':' && (*(in+1)==' ' || !*(in+1))) {
      jpf_strbuf_add(&out,"\\");
      snprintf(c,2,"%c",*in);
      jpf_strbuf_add(&out,c);
    } else if(init && !isouralnum(*in)) {
      jpf_strbuf_add(&out,"\\");
      snprintf(c,2,"%c",*in);
      jpf_strbuf_add(&out,c);
    } else {
      snprintf(c,2,"%c",*in);
      jpf_strbuf_add(&out,c);
    }
    init = 0;
  }
  return jpf_strbuf_str(&out);
}

static void str_quoted(struct jpf_emitter *em,char *c) {
  char *ce;

  jpf_strbuf_add(&(em->buf),"\"");
  ce = escape(c,"\"\\",0,0);
  jpf_strbuf_add(&(em->buf),ce);
  free(ce);
  jpf_strbuf_add(&(em->buf),"\"");
}

static void str_unquoted(struct jpf_emitter *em,char *c) {
  char *ce;

  ce = escape(c,"#\\ ",1,1);
  jpf_strbuf_add(&(em->buf),ce);
  free(ce);
}

static char * redent(char *c,int amt) {
  struct jpf_strbuf out;
  char *dent,d[2];

  d[1] = '\0';
  dent = spaces(amt);
  jpf_strbuf_init(&out,0);
  jpf_strbuf_add(&out,dent);
  for(;*c;c++) {
    d[0] = *c;  
    jpf_strbuf_add(&out,d);
    if(*c == '\n' && *(c+1)!='\n') { jpf_strbuf_add(&out,dent); }
  }
  free(dent);
  return jpf_strbuf_str(&out);
}

// TEST heredoc bail
static void str_heredoc(struct jpf_emitter *em,char *c) {
  char *sep,*dc,*dsep;
  int dent = 2;

  sep = ok_heredoc(c);
  if(!sep) { str_quoted(em,c); return; }
  if(em->len) { dent = em->stack[em->len-1]+2; }
  dc = redent(c,dent);
  dsep = redent(sep,dent);
  jpf_strbuf_add(&(em->buf),"< ");
  jpf_strbuf_add(&(em->buf),sep);
  jpf_strbuf_add(&(em->buf),"\n");
  jpf_strbuf_add(&(em->buf),dc);
  jpf_strbuf_add(&(em->buf),"\n");
  jpf_strbuf_add(&(em->buf),dsep);
  free(sep);
  free(dsep);
  free(dc);
  em->nl = 1;
}

static void str_best(struct jpf_emitter *em,char *c,int key) {
  char *q,*uq;

  q = escape(c,"\"\\",0,0);
  uq = escape(c,"#\\ ",1,1);
  if((!*c && key) || strlen(q)+2 <= strlen(uq)) {
    str_quoted(em,c);
  } else {
    str_unquoted(em,c);
  }
  free(q);
  free(uq);
}

// XXX other output methods
static void push_string(char *c,enum style style,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  undent(em,JPF_BEST);
  switch(style) {
  case JPF_QUOTED:
    str_quoted(em,c);
    break;
  case JPF_UNQUOTED:
    str_unquoted(em,c);
    break;
  case JPF_HEREDOC:
    str_heredoc(em,c);
    break;
  default:
    str_best(em,c,0);
    break;
  }
}

static void error(char *c,int line,int col,void *priv) {
}

static void end_member(void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  indent_pop(em);
}

static void start_member(enum style style,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  em->empty = 0;
  undent(em,style);
  jpf_strbuf_add(&(em->buf),"- ");
  indent_push(em,curcol(em));
}

static void start_key(char *key,enum style style,int lena,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;
  int pad = 0;
  char * sp;

  em->empty = 0;
  undent(em,style);
  str_best(em,key,1);
  if(lena>-1) {
    pad = lena-strlen(key)+1;
  }
  if(pad<1) { pad = 1; }
  sp = spaces(pad);
  jpf_strbuf_add(&(em->buf),":%s",sp);
  free(sp);
  indent_push(em,curcol(em));
}

static void end_key(char *key,void *priv) {
  struct jpf_emitter *em = (struct jpf_emitter *)priv;

  indent_pop(em);
}

static void jpf_emit_init(struct jpf_callbacks *cb,struct jpf_emitter *em) {
  *cb = (struct jpf_callbacks) {
    .start = start,
    .end = end,
    .push_array = push_array,
    .push_assoc = push_assoc,
    .push_null = push_null,
    .push_boolean = push_boolean,
    .push_number = push_number,
    .push_string = push_string,
    .error = error,
    .start_member = start_member,
    .end_member = end_member,
    .start_key = start_key,
    .end_key = end_key,
  };
  jpf_strbuf_init(&(em->buf),0);
  em->outstr = 0;
  em->outf = 0;
  em->len = em->size = 0;
  em->stack = 0;
  em->nl = 0;
  em->empty = 0;
  em->failed = 0;
  em->done = 0;
}

static void jpf_emit_close(struct jpf_emitter *em) {
  fclose(em->outf); // XXX check for errors
}

void jpf_emit_done(struct jpf_emitter *em) {
  jpf_strbuf_free(&(em->buf));
  free(em->stack);
}

void jpf_emit_file(struct jpf_callbacks *cb,struct jpf_emitter *em,
                   FILE *f) {
  jpf_emit_init(cb,em);
  em->outf = f;
}

void jpf_emit_fd(struct jpf_callbacks *cb,struct jpf_emitter *em,int fd) {
  int newfd;

  jpf_emit_init(cb,em);
  newfd = dup(fd); /* To close at will */
  if(newfd==-1) {
    em->failed = jpf_message("dup failed on fd %d",fd);
    return;
  }
  em->outf = fdopen(newfd,"wb");
  if(!em->outf) {
    em->failed = jpf_message("Could not open fd '%d' for writing",newfd);
    return;
  } 
  em->done = jpf_emit_close;
}

// TEST all methods
void jpf_emit_filename(struct jpf_callbacks *cb,struct jpf_emitter *em,
                       char *filename) {
  jpf_emit_init(cb,em);
  em->outf = fopen(filename,"wb");
  if(!em->outf) {
    em->failed = jpf_message("Could not open '%s' for writing",filename);
  } else {
    em->done = jpf_emit_close;
  }
}

void jpf_emit_str(struct jpf_callbacks *cb,struct jpf_emitter *em,
                  char **out) {
  jpf_emit_init(cb,em);
  em->outstr = out;
}

char * jpf_emit_failed(struct jpf_emitter *em) {
  return em->failed;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#include "jpf.h"
#include "util.h"

struct jpf_dfparser {
  struct lexer *lx;
  struct jpf_strbuf errors;
  /* Stack */
  int len,size;
  struct jpf_value **stack;
};

// XXX test styles
struct jpf_value * jpfv_alloc(enum jpf_type type,enum style style) {
  struct jpf_value *out;

  out = jpf_safe_malloc(sizeof(struct jpf_value));
  out->type = type;
  out->style = style;
  return out;
}

struct jpf_value * jpfv_number(double n) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_NUMBER,JPF_BEST);
  v->v.number = n;
  return v;
}

struct jpf_value * jpfv_number_int(double n) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_NUMBER,JPF_INT);
  v->v.number = n;
  return v;
}

struct jpf_value * jpfv_string(char *c) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_STRING,JPF_BEST);
  v->v.string = jpf_safe_strdup(c);
  return v;
}

struct jpf_value * jpfv_array(void) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_ARRAY,JPF_BEST);
  v->v.array.size = v->v.array.len = 0;
  v->v.array.v = 0;
  return v;
}

struct jpf_value * jpfv_important_array(int very) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_ARRAY,very?JPF_VIMPORTANT:JPF_IMPORTANT);
  v->v.array.size = v->v.array.len = 0;
  v->v.array.v = 0;
  return v;
}


struct jpf_value * jpfv_assoc(void) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_ASSOC,JPF_BEST);
  v->v.assoc.size = v->v.assoc.len = 0;
  v->v.assoc.k = 0;
  v->v.assoc.v = 0;
  return v;
}

struct jpf_value * jpfv_important_assoc(int very) {
  struct jpf_value *v;

  v = jpfv_alloc(JPFV_ASSOC,very?JPF_VIMPORTANT:JPF_IMPORTANT);
  v->v.assoc.size = v->v.assoc.len = 0;
  v->v.assoc.k = 0;
  v->v.assoc.v = 0;
  return v;
}

void jpfv_array_add(struct jpf_value *v,struct jpf_value *member) {
  if(v->v.array.size==v->v.array.len) {
    v->v.array.size = (v->v.array.size*3/2)+8;
    v->v.array.v =
      jpf_safe_realloc(v->v.array.v,v->v.array.size*sizeof(struct jpf_value *));
  }
  v->v.array.v[v->v.array.len++] = member;
}

void jpfv_assoc_add(struct jpf_value *v,char *key,struct jpf_value *member) {
  if(v->v.assoc.size==v->v.assoc.len) {
    v->v.assoc.size = (v->v.assoc.size*3/2)+8;
    v->v.assoc.k =
      jpf_safe_realloc(v->v.assoc.k,v->v.assoc.size*sizeof(char *));
    v->v.assoc.v =
      jpf_safe_realloc(v->v.assoc.v,v->v.assoc.size*sizeof(struct jpf_value *));
  }
  v->v.assoc.k[v->v.assoc.len] = jpf_safe_strdup(key);
  v->v.assoc.v[v->v.assoc.len++] = member;
}

struct jpf_value * jpfv_lookup(struct jpf_value *v,char *key) {
  int i;

  if(v->type!=JPFV_ASSOC) { return 0; }
  for(i=0;i<v->v.assoc.len;i++) {
    if(!strcmp(v->v.assoc.k[i],key)) {
      return v->v.assoc.v[i];
    }
  }
  return 0;
}

// TEST jpfv_int
int jpfv_int64(struct jpf_value *v,int64_t *out) {
  char *end;
  int w;

  if(!v) { return -2; }
  switch(v->type) {
  case JPFV_NUMBER: *out = lrint(v->v.number); break;
  case JPFV_FALSE: *out = 0; break;
  case JPFV_NULL: *out = 0; break;
  case JPFV_TRUE: *out = 1; break;
  case JPFV_STRING:
    if(!*(v->v.string)) { *out = 0; }
    else {
      w = strtol(v->v.string,&end,0);
      if(!end || *end) { return -1; }
      *out = w;
    }
    break;
  default:
    return -1;
  }
  return 0;
}

int jpfv_int(struct jpf_value *v,int *out) {
  int64_t ret,r;

  if(!v) { return -2; }
  r = jpfv_int64(v,&ret);
  if(r) { return r; }
  *out = (int)ret;
  return 0;
}

// TEST jpfv_bool
int jpfv_bool(struct jpf_value *v) {
  int n,r;

  if(!v) { return -2; }
  if(v->type == JPFV_NUMBER) {
    return !(v->v.number==0.0);
  } else {
    r = jpfv_int(v,&n);
    if(r) { return r; }
    return !!n;
  }
}

void jpfv_free(struct jpf_value *v) {
  int i;

  switch(v->type) {
  case JPFV_STRING:
    free(v->v.string);
    break;
  case JPFV_ARRAY:
    for(i=0;i<v->v.array.len;i++)
      jpfv_free(v->v.array.v[i]);
    free(v->v.array.v);
    break;
  case JPFV_ASSOC:
    for(i=0;i<v->v.assoc.len;i++) {
      free(v->v.assoc.k[i]);
      jpfv_free(v->v.assoc.v[i]);
    }
    free(v->v.assoc.k);
    free(v->v.assoc.v);
    break;
  default:
    break;
  }
  free(v);
}

static void jpf_dfp_free(struct jpf_dfparser *dfp,int start) {
  int i;

  for(i=start;i<dfp->len;i++) {
    jpfv_free(dfp->stack[i]);
  }
  free(dfp->stack);
}

static void jpfv_push(struct jpf_dfparser *dfp,struct jpf_value *v) {
  if(dfp->len==dfp->size) {
    dfp->size = (dfp->size*3/2)+8;
    dfp->stack = jpf_safe_realloc(dfp->stack,dfp->size*sizeof(struct jpf_value *));
  }
  dfp->stack[dfp->len++] = v;
}

static void start(void *priv) {}
static void end(void *priv) {}

static void push_array(void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_array());
}

static void push_assoc(void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_assoc());
}

static void push_null(void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_alloc(JPFV_NULL,JPF_BEST));
}

static void push_boolean(int v,void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_alloc(v?JPFV_TRUE:JPFV_FALSE,JPF_BEST));
}

static void push_number(double v,enum style style,void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_number(v));
}

static void push_string(char *c,enum style style,void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpfv_push(dfp,jpfv_string(c));
}

static void error(char *c,int line,int col,void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;

  jpf_strbuf_add(&(dfp->errors),"error[line=%d col=%d]: %s\n",line,col,c);
}

static void end_member(void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;
 
  if(dfp->len<2) {
    jpf_strbuf_add(&(dfp->errors),"Confused stack");
  } else {
    jpfv_array_add(dfp->stack[dfp->len-2],dfp->stack[dfp->len-1]);
    dfp->len--;
  }
}

static void start_member(enum style style,void *priv) {}

static void start_key(char *key,enum style style,int lena,void *priv) {}

static void end_key(char *key,void *priv) {
  struct jpf_dfparser * dfp = (struct jpf_dfparser *)priv;
 
  if(dfp->len<2) {
    jpf_strbuf_add(&(dfp->errors),"Confused stack");
  } else {
    jpfv_assoc_add(dfp->stack[dfp->len-2],key,dfp->stack[dfp->len-1]);
    dfp->len--;
  }
}

static void set_callbacks(struct jpf_callbacks *cb) {
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
}

char * jpf_dfparse(struct lexer *lx,struct jpf_value **out) {
  struct jpf_dfparser dfp;
  struct jpf_callbacks cb;

  set_callbacks(&cb);
  dfp.lx = lx;
  jpf_strbuf_init(&(dfp.errors),0);
  dfp.len = dfp.size = 0;
  dfp.stack = 0;
  jpf_lex_go(lx,&cb,&dfp);
  if(dfp.len!=1) {
    jpf_strbuf_add(&(dfp.errors),"Stack not complete after parse\n");
  }
  if(jpf_strbuf_len(&(dfp.errors))) {
    jpf_dfp_free(&dfp,0);
    return jpf_strbuf_str(&(dfp.errors));
  } else {
    *out = dfp.stack[0];
    jpf_dfp_free(&dfp,1);
    jpf_strbuf_free(&(dfp.errors));
    return 0;
  }
}

static void emit(struct jpf_value *v,struct jpf_callbacks *cb,void *priv) {
  int i,n,mxk;

  switch(v->type) {
  case JPFV_ARRAY:
    cb->push_array(priv);
    for(i=0;i<v->v.array.len;i++) {
      cb->start_member(v->style,priv);
      emit(v->v.array.v[i],cb,priv);
      cb->end_member(priv);
    }
    break;
  case JPFV_ASSOC:
    cb->push_assoc(priv);
    mxk = -1;
    for(i=0;i<v->v.assoc.len;i++) {
      n = strlen(v->v.assoc.k[i]);
      if(n>mxk) { mxk = n; }
    }
    for(i=0;i<v->v.assoc.len;i++) {
      cb->start_key(v->v.assoc.k[i],v->style,mxk,priv);
      emit(v->v.assoc.v[i],cb,priv);
      cb->end_key(v->v.assoc.k[i],priv);
    }
    break;
  case JPFV_STRING: cb->push_string(v->v.string,JPF_BEST,priv); break;
  case JPFV_NUMBER: cb->push_number(v->v.number,v->style,priv); break;
  case JPFV_TRUE:   cb->push_boolean(1,priv); break;
  case JPFV_FALSE:  cb->push_boolean(0,priv); break;
  case JPFV_NULL:   cb->push_null(priv); break;
  }
}

void jpf_emit_df(struct jpf_value *v,struct jpf_callbacks *cb,void *priv) {
  cb->start(priv);
  emit(v,cb,priv);
  cb->end(priv);
}


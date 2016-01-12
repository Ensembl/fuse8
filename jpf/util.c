#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void alloc_chk(void *v) {
  if(v) { return; }
  fprintf(stderr,"Out of memory\n");
  exit(255);
}

#define ERRBUFLEN 1024
void * jpf_safe_strerror(int errnum) {
  char *buffer;

  buffer = jpf_safe_malloc(ERRBUFLEN);
  strerror_r(errnum,buffer,ERRBUFLEN);
  return buffer;
}

void * jpf_safe_malloc(size_t s) {
  void *out;

  out = malloc(s);
  alloc_chk(out);
  return out;
}

void * jpf_safe_realloc(void *p,size_t s) {
  void *out;

  out = realloc(p,s);
  alloc_chk(out);
  return out;
}

char * jpf_safe_strdup(char *c) {
  char *out;

  out = strdup(c);
  alloc_chk(out);
  return out;
}

char * jpf_vmessage(char *fmt,va_list ap) {
  int r;
  char *out,c[2]; /* SUSv2 (but not C99) specifies error if size 0, so 2 */
  va_list ap2;

  va_copy(ap2,ap);
  r = vsnprintf(c,2,fmt,ap2);
  va_end(ap2);
  out = jpf_safe_malloc(r+1);
  va_copy(ap2,ap);
  vsnprintf(out,r+1,fmt,ap2);
  va_end(ap2);
  out[r] = '\0';
  return out;
}

char * jpf_message(char *fmt,...) {
  va_list ap;
  char *out;

  va_start(ap,fmt);
  out = jpf_vmessage(fmt,ap);
  va_end(ap);
  return out;
}

void jpf_strbuf_init(struct jpf_strbuf *t,size_t maxsize) {
  t->s = jpf_safe_malloc(1);
  t->s[0] = '\0';
  t->len = t->size = 0;
  t->maxsize = maxsize;
}

void jpf_strbuf_free(struct jpf_strbuf *t) {
  free(t->s);
  t->s = 0;
}

char * jpf_strbuf_extend(struct jpf_strbuf *t,int len) {
  int offset,newlen;

  offset = t->len;
  newlen = t->len + len;
  if(t->maxsize && newlen > t->maxsize) { return 0; }
  t->len = newlen;
  if(t->size <= t->len) {
    t->size = (t->len*3/2)+16;
    t->s = jpf_safe_realloc(t->s,t->size+1);
  }
  return t->s+offset;
}

void jpf_strbuf_retract(struct jpf_strbuf *t,int num) {
  t->len -= num;
  t->s[t->len] = '\0';
}

int jpf_strbuf_add(struct jpf_strbuf *t,char *c,...) {
  char *s,*d;
  va_list ap;

  va_start(ap,c);
  s = jpf_vmessage(c,ap);
  va_end(ap);
  d = jpf_strbuf_extend(t,strlen(s));
  if(d) { strcpy(d,s); }
  free(s);
  return !!d;
}

char *jpf_strbuf_str(struct jpf_strbuf *t) { return t->s; }
int jpf_strbuf_len(struct jpf_strbuf *t) { return t->len; }

void jpf_strbuf_trim(struct jpf_strbuf *t) {
  while(t->len) {
    if(t->s[t->len-1]!=' ') { break; }
    t->len--;
  }
  t->s[t->len] = '\0';
}

int isournl(char c) {
  return c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

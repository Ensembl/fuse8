#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "strbuf.h"
#include "misc.h"

void strbuf_init(struct strbuf *t,size_t maxsize) {
  t->s = safe_malloc(1);
  t->s[0] = '\0';
  t->len = t->size = 0;
  t->maxsize = maxsize;
}

void strbuf_free(struct strbuf *t) {
  free(t->s);
  t->s = 0;
}

char * strbuf_extend(struct strbuf *t,int len) {
  int offset,newlen;

  offset = t->len;
  newlen = t->len + len;
  if(t->maxsize && newlen > t->maxsize) { return 0; }
  t->len = newlen;
  if(t->size <= t->len) {
    t->size = (t->len*3/2)+16;
    t->s = safe_realloc(t->s,t->size+1);
  }
  return t->s+offset;
}

void strbuf_retract(struct strbuf *t,int num) {
  t->len -= num;
  t->s[t->len] = '\0';
}

int strbuf_add(struct strbuf *t,char *c,...) {
  char *s,*d;
  va_list ap;

  va_start(ap,c);
  s = make_string_v(c,ap);
  va_end(ap);
  d = strbuf_extend(t,strlen(s));
  if(d) { strcpy(d,s); }
  free(s);
  return !!d;
}

char *strbuf_str(struct strbuf *t) { return t->s; }
int strbuf_len(struct strbuf *t) { return t->len; }


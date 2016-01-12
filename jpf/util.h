#ifndef JPF_UTIL_H
#define JPF_UTIL_H

#include <stdarg.h>

void * jpf_safe_malloc(size_t s);
void * jpf_safe_realloc(void *p,size_t s);
char * jpf_safe_strdup(char *c);

char * jpf_message(char *fmt,...);
char * jpf_vmessage(char *fmt,va_list ap);
void * jpf_safe_strerror(int errnum);

/* DO NOT inspect, only explicit here for stack allocation */
struct jpf_strbuf {
  char *s;
  size_t len,size,maxsize;
};

void jpf_strbuf_init(struct jpf_strbuf *t,size_t maxsize);
void jpf_strbuf_free(struct jpf_strbuf *t);
int jpf_strbuf_add(struct jpf_strbuf *t,char *c,...);
char *jpf_strbuf_str(struct jpf_strbuf *t);
int jpf_strbuf_len(struct jpf_strbuf *t);
char * jpf_strbuf_extend(struct jpf_strbuf *t,int len);
void jpf_strbuf_retract(struct jpf_strbuf *t,int num);
void jpf_strbuf_trim(struct jpf_strbuf *t);

int isournl(char c);

#endif

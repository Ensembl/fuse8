#ifndef UTIL_STRBUF_H
#define UTIL_STRBUF_H

/* DO NOT inspect, only explicit here for stack allocation */
struct strbuf {
  char *s;
  size_t len,size,maxsize;
};

void strbuf_init(struct strbuf *t,size_t maxsize);
void strbuf_free(struct strbuf *t);
char * strbuf_extend(struct strbuf *t,int len);
void strbuf_retract(struct strbuf *t,int num);
int strbuf_add(struct strbuf *t,char *c,...);
char *strbuf_str(struct strbuf *t);
int strbuf_len(struct strbuf *t);

#endif

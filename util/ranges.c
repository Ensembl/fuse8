#include <stdlib.h>
#include <stdint.h>

#include "misc.h"
#include "strbuf.h"
#include "ranges.h"

/* ALL RANGES ARE SEMI-OPEN */

struct range {
  int64_t a,b;
  struct range *next;
};

void ranges_init(struct ranges *rr) {
  rr->r = 0;
}

void ranges_free(struct ranges *rr) {
  struct range *r;

  while(rr->r) {
    r = rr->r->next;
    free(rr->r);
    rr->r = r;
  }
}

static void add(struct range **rr,int64_t a,int64_t b) {
  struct range *r;

  r = safe_malloc(sizeof(struct range));
  r->a = a;
  r->b = b;
  r->next = *rr;
  *rr = r;
}

static int unify(int64_t *xa,int64_t *xb,int64_t ya,int64_t yb) {
  /* Fail: xa x x x x x [xb] ya y y y y y [yb], or vice versa */
  if(ya>*xb || *xa>yb) { return 0; }
  if(ya<*xa) { *xa = ya; }
  if(yb>*xb) { *xb = yb; }
  return 1; 
}

/* Completely disjoint ranges are copied. Others are merged into incoming */
void ranges_add(struct ranges *rr,int64_t a,int64_t b) {
  struct range *new = 0,*old; 

  while(rr->r) {
    old = rr->r;
    rr->r = old->next;
    if(!unify(&a,&b,old->a,old->b)) {
      old->next = new;
      new = old;
    } else {
      free(old);
    }
  }
  add(&new,a,b);
  rr->r = new;
}

static int chop(struct range **rr,int64_t c) {
  struct range *r;

  for(r=*rr;r;r=r->next) {
    if(r->a < c && r->b > c) {
      add(rr,c,r->b);
      r->b = c;
      return 1;
    }
  }
  return 0;
}

void ranges_remove(struct ranges *rr,int64_t a,int64_t b) {
  struct range *old,*new = 0;

  chop(&(rr->r),a); 
  chop(&(rr->r),b);
  /* After those chops, interval will either be all in or all out */
  while(rr->r) {
    old = rr->r;
    rr->r = old->next;
    if(a>=old->b || b<=old->a) {
      old->next = new;
      new = old;
    } else {
      free(old);
    }
  }
  rr->r = new;
}

void ranges_start(struct ranges *rr,struct rangei *ri) {
  ri->rr = rr;
  ri->r = 0;
}

int ranges_next(struct rangei *ri,int64_t *a,int64_t *b) {
  if(ri->r) { ri->r = ri->r->next; } else { ri->r = ri->rr->r; }
  if(!ri->r) { return 0; }
  *a = ri->r->a;
  *b = ri->r->b;
  return 1;
}

int ranges_num(struct ranges *rr) { // XXX do it efficiently
  struct rangei ri;
  int out = 0;
  int64_t x,y;
  
  ranges_start(rr,&ri);
  while(ranges_next(&ri,&x,&y)) { out++; }
  return out;
}

int ranges_empty(struct ranges *rr) { return !(rr->r); }

void ranges_merge(struct ranges *a,struct ranges *b) {
  struct rangei ri;
  int64_t x,y;

  ranges_start(b,&ri);
  while(ranges_next(&ri,&x,&y)) {
    ranges_add(a,x,y);
  }
}

void ranges_copy(struct ranges *dst,struct ranges *src) {
  ranges_init(dst);
  ranges_merge(dst,src);  
}

void ranges_difference(struct ranges *a,struct ranges *b) {
  struct rangei ri;
  int64_t x,y;

  ranges_start(b,&ri);
  while(ranges_next(&ri,&x,&y)) {
    ranges_remove(a,x,y);
  }
}

void ranges_blockify_expand(struct ranges *a,int size) {
  struct ranges out;
  struct rangei ri;
  int64_t x,y;

  ranges_init(&out);
  ranges_start(a,&ri);
  while(ranges_next(&ri,&x,&y)) {
    ranges_add(&out,(x/size)*size,((y+size-1)/size)*size);
  }
  ranges_free(a);
  *a = out;
}

void ranges_blockify_reduce(struct ranges *a,int size) {
  struct ranges out;
  struct rangei ri;
  int64_t x,y;

  ranges_init(&out);
  ranges_start(a,&ri);
  while(ranges_next(&ri,&x,&y)) {
    x = (x+size-1)/size;
    y = y/size;
    if(y>x) {
      ranges_add(&out,x*size,y*size);
    }
  }
  ranges_free(a);
  *a = out;
}

char * ranges_print(struct ranges *rr) {
  struct strbuf str;
  struct rangei ri;
  int64_t x,y;
  int first;

  strbuf_init(&str,0);
  ranges_start(rr,&ri);
  first = 1;
  while(ranges_next(&ri,&x,&y)) {
    if(!first) { strbuf_add(&str,","); }
    strbuf_add(&str,"[%lld,%lld)",x,y);
    first = 0;
  } 
  if(first) { strbuf_add(&str,"(empty)"); }
  return strbuf_str(&str);
}

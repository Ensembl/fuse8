#ifndef UTIL_ASSOC_H
#define UTIL_ASSOC_H

#include "misc.h"

struct assoc;

/* Just here so you can declare them on the stack, don't peek! */
struct assoc_iter {
  struct assoc *a;
  int i;
};

struct assoc * assoc_create(type_free_cb k,void *kp,type_free_cb v,void *vp);
struct ref * assoc_ref(struct assoc *a);
void assoc_acquire(struct assoc *a);
void assoc_release(struct assoc *a);
void assoc_set(struct assoc *a,char *k,void *v);
void * assoc_lookup(struct assoc *a,char *k);
void associ_start(struct assoc *a,struct assoc_iter *e);
int associ_next(struct assoc_iter *e);
char * associ_key(struct assoc_iter *);
void * associ_value(struct assoc_iter *);
int assoc_len(struct assoc *a);

#endif

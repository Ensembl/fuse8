#ifndef UTIL_ARRAY_H
#define UTIL_ARRAY_H

#include "misc.h"

struct array;

struct array * array_create(type_free_cb v,void *vb);
struct ref * array_ref(struct array *a);
void array_acquire(struct array *);
void array_release(struct array *);
void array_release_v(void *);
void * array_index(struct array *a,int idx);
int array_length(struct array *a);
void array_insert(struct array *a,void *v);
int array_set(struct array *a,int idx,void *v);
int array_set_nf(struct array *a,int idx,void *v);
int array_remove(struct array *a);
int array_remove_nf(struct array *a);

#endif

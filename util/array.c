#define _GNU_SOURCE /* For qsort_r */
#include <stdlib.h>

#include "misc.h"
#include "array.h"

struct array {
  struct ref r;
  int n,N;
  void **e;
  type_free_cb freev;
  void *freevp;
};

static void array_ref_free(void *data) {
  struct array *a = (struct array *)data;

  free(a);
}

static void array_ref_release(void *data) {
  struct array *a = (struct array *)data;
  int i;

  for(i=0;i<a->n;i++) {
    if(a->freev)
      a->freev(a->e[i],a->freevp);
  }
  free(a->e);
  a->e = 0;
  a->n = a->N = 0;
}

struct array * array_create(type_free_cb v,void *vp) {
  struct array *a;

  a = safe_malloc(sizeof(struct array));
  ref_create(&(a->r));
  ref_on_release(&(a->r),array_ref_release,a);
  ref_on_free(&(a->r),array_ref_free,a);
  a->e = 0;
  a->n = a->N = 0;
  a->freev = v;
  a->freevp = vp;
  return a;
}

struct ref * array_ref(struct array *a) { return &(a->r); }
void array_acquire(struct array *a) { ref_acquire(&(a->r)); }
void array_release(struct array *a) { ref_release(&(a->r)); }
void array_release_v(void *a) { array_release((struct array *)a); }

void * array_index(struct array *a,int idx) {
  if(idx<0 || idx>=a->n) { return 0; }
  return a->e[idx];
}
int array_length(struct array *a) { return a->n; }

void array_insert(struct array *a,void *v) {
  if(a->n==a->N) {
    a->N = (a->N*3/2)+8;
    a->e = safe_realloc(a->e,sizeof(void *)*a->N);
  }
  a->e[a->n++] = v;
}

int array_set_i(struct array *a,int idx,void *v,int fr) {
  if(idx>=a->n) { return 0; }
  if(a->freev && fr)
    a->freev(a->e[idx],a->freevp);
  a->e[idx] = v;
  return 1;
}

int array_set(struct array *a,int idx,void *v) {
  return array_set_i(a,idx,v,1);
}

int array_set_nf(struct array *a,int idx,void *v) {
  return array_set_i(a,idx,v,0);
}

static int array_remove_i(struct array *a,int fr) {
  if(!a->n) { return 0; }
  a->n--;
  if(fr && a->freev)
    a->freev(a->e[a->n],a->freevp);
  if(a->n*4 < a->N && a->N>=16) {
    a->N /= 2;
    a->e = safe_realloc(a->e,sizeof(void*)*a->N);
  }
  return 1;
}

int array_remove(struct array *a) {
  return array_remove_i(a,1);
}

int array_remove_nf(struct array *a) {
  return array_remove_i(a,0);
}

void array_sort(struct array *a,compare_fn cmp,void *payload) {
  qsort_r(a->e,a->n,sizeof(void *),cmp,payload);

}

#include <stdlib.h>
#include <string.h>
#include "misc.h"

#include "hash.h"
#include "assoc.h"

struct assoc_el {
  char *k;
  void *v;
};

char tomb = '\0';

struct assoc {
  struct ref r;
  int t,n,N;
  struct assoc_el *e;
  type_free_cb freek,freev;
  void *freekp,*freevp;
};

static void assoc_resize(struct assoc *in,int size);

static void assoc_freeel(struct assoc *a,struct assoc_el *e) {
  if(a->freek) { a->freek(e->k,a->freekp); }
  if(a->freev) { a->freev(e->v,a->freevp); }
}

static void assoc_ref_release(void *data) {
  struct assoc *a = (struct assoc *)data;
  struct assoc_iter e;

  if(a->freek || a->freev) {
    associ_start(a,&e);
    while(associ_next(&e)) {
      assoc_freeel(a,&(e.a->e[e.i]));
      e.a->e[e.i].k = &tomb;
    }
  }
  a->n = 0;
  assoc_resize(a,8);
}

static void assoc_ref_free(void *data) {
  struct assoc *a = (struct assoc *)data;

  free(a->e);
  free(a);
}

static struct assoc * assoc_make(type_free_cb k,void *kp,
                                 type_free_cb v,void *vp,int size) {
  struct assoc *a;
  int i;

  a = safe_malloc(sizeof(struct assoc));
  ref_create(&(a->r));
  a->t = 0;
  a->n = 0;
  a->N = size;
  a->e = safe_malloc(sizeof(struct assoc_el)*size);
  for(i=0;i<size;i++) { a->e[i].k = 0; }
  a->freek = k;
  a->freekp = kp;
  a->freev = v;
  a->freevp = vp;  
  return a;
}

struct assoc * assoc_create(type_free_cb k,void *kp,type_free_cb v,void *vp) {
  struct assoc *a;

  a = assoc_make(k,kp,v,vp,8);
  ref_on_release(&(a->r),assoc_ref_release,a);
  ref_on_free(&(a->r),assoc_ref_free,a);
  return a;
}

static void assoc_resize(struct assoc *in,int size) {
  struct assoc *out;
  struct assoc_iter iter;

  out = assoc_make(in->freek,in->freekp,in->freev,in->freevp,size);
  associ_start(in,&iter);
  while(associ_next(&iter)) {
    assoc_set(out,associ_key(&iter),associ_value(&iter));
  }
  free(in->e);
  in->e = out->e;
  in->N = out->N;
  in->t = 0;
  free(out);
}

struct ref * assoc_ref(struct assoc *a) { return &(a->r); }
void assoc_acquire(struct assoc *a) { ref_acquire(&(a->r)); }
void assoc_release(struct assoc *a) { ref_release(&(a->r)); }

struct assoc_el * assoc_find(struct assoc *a,char *k) {
  uint32_t h;

  if(!k) { return 0; }
  h = str_hash(k) % a->N;
  while(a->e[h].k) {
    if(!strcmp(a->e[h].k,k) && a->e[h].k != &tomb) { return &(a->e[h]); }
    h++;
    h %= a->N;
  }
  return 0;
}

void * assoc_lookup(struct assoc *a,char *k) {
  struct assoc_el *e;

  e = assoc_find(a,k);
  if(e) { return e->v; } else { return 0; }
}

static void assoc_remove(struct assoc *a,char *k) {
  uint32_t h;

  if(!k) { return; }
  h = str_hash(k) % a->N;
  while(a->e[h].k) {
    if(!strcmp(a->e[h].k,k) && a->e[h].k != &tomb) {
      assoc_freeel(a,&(a->e[h]));
      a->e[h].k = &tomb;
      a->n--;
      a->t++;
      if(a->n*4 < a->N && a->N>8) { assoc_resize(a,a->N/2); }
      else if(a->t > a->N/4) { assoc_resize(a,a->N); }
      return;
    }
    h++;
    h %= a->N;
  }
}

void assoc_set(struct assoc *a,char *k,void *v) {
  uint32_t h;
  struct assoc_el *e;

  if(!k) { return; }
  if(!v) { assoc_remove(a,k); return; }
  e = assoc_find(a,k);
  if(e) {
    assoc_freeel(a,e);
    e->k = k;
    e->v = v;
  } else {
    a->n++;
    if(2*a->n > a->N) { assoc_resize(a,a->N*2); }
    h = str_hash(k) % a->N;
    while(a->e[h].k && a->e[h].k != &tomb) {
      h++;
      h %= a->N;
    }
    a->e[h].k = k;
    a->e[h].v = v;
  }
}

void associ_start(struct assoc *a,struct assoc_iter *e) {
  e->a = a;
  e->i = -1;
}

int associ_next(struct assoc_iter *e) {
  e->i++;
  while(e->i < e->a->N) {
    if(e->a->e[e->i].k && e->a->e[e->i].k != &tomb) { return 1; }
    e->i++;
  }
  return 0;
}

char * associ_key(struct assoc_iter *e) { return e->a->e[e->i].k; }
void * associ_value(struct assoc_iter *e) { return e->a->e[e->i].v; }

int assoc_len(struct assoc *a) { return a->n; }

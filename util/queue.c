#include <stdio.h>

#include "misc.h"
#include "queue.h"

struct member {
  void *data;
  struct member *next;
};

struct queue {
  struct ref r;
  int n;
  struct member *first,**lastp;
  type_free_cb freev;
  void *priv;
};

static void q_ref_release(void *data) {
  struct queue *q = (struct queue *)data;
  void *d;

  while(queue_length(q)) {
    d = queue_remove(q);
    if(d && q->freev) { q->freev(d,q->priv); }
  }
}

static void q_ref_free(void *data) {
  struct queue *q = (struct queue *)data;

  free(q);
}

struct queue * queue_create(type_free_cb freev,void *priv) {
  struct queue *q;

  q = safe_malloc(sizeof(struct queue));
  ref_create(&(q->r));
  ref_on_release(&(q->r),q_ref_release,q);
  ref_on_free(&(q->r),q_ref_free,q);
  q->n = 0;
  q->first = 0;
  q->lastp = &(q->first);
  q->freev = freev;
  q->priv = priv;
  return q;
}

void queue_add(struct queue *q,void *data) {
  struct member *m;

  m = safe_malloc(sizeof(struct member));
  m->data = data;
  m->next = 0;
  *(q->lastp) = m;
  q->lastp = &(m->next);
  q->n++;
}

void * queue_remove(struct queue *q) {
  struct member *m;
  void *data;

  m = q->first;
  if(!m) { return 0; }
  q->n--;
  q->first = m->next;
  if(!q->first) { q->lastp = &(q->first); }
  data = m->data;
  free(m);
  return data; 
}

int queue_length(struct queue *q) { return q->n; }

void queue_release(struct queue *q) { ref_release(&(q->r)); }


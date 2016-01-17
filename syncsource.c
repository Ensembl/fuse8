#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "source.h"
#include "types.h"
#include "syncsource.h"
#include "request.h"
#include "util/logging.h"
#include "util/event.h"

#define NUMTHREADS 128

CONFIG_LOGGING(syncsource)

enum qtype { Q_CLOSE, Q_READ, Q_WRITE };
enum atype { A_CLOSE, A_READ, A_WRITE, A_QUIT };

union type {
  enum qtype q;
  enum atype a;
};

struct member {
  union type type;
  struct syncsource *src;
  struct block *bk;
  struct request *rq;
  struct chunk *ck;
};

struct syncqueue {
  struct ref r;
  struct wqueue *qu;
  struct evdata *ans;
  pthread_t threads[NUMTHREADS];
  int quitting;
};

// XXX sync prefilter
static void add_a(struct syncqueue *sq,enum atype type,
                   struct syncsource *src,
                   struct request *rq,struct chunk *ck);
    
static void job(struct syncqueue *sq,struct member *job) {
  struct chunk *ck;

  log_debug(("worker job type Q%d",job->type.q));
  switch(job->type.q) {
  case Q_CLOSE:
    job->src->close(job->src);
    add_a(sq,A_CLOSE,job->src,0,0);
    break;
  case Q_READ:
    ck = job->src->read(job->src,job->rq);
    add_a(sq,A_READ,job->src,job->rq,ck);
    break;
  case Q_WRITE:
    job->src->write(job->src,job->rq,job->ck);
    add_a(sq,A_WRITE,job->src,job->rq,0);
    break;
  }
  if(job->src && job->src->src) { src_release(job->src->src); }
}

static void * worker(void *data) {
  struct syncqueue *sq = (struct syncqueue *)data;
  struct member *m;

  while(1) {
    m = wqueue_get_work(sq->qu);
    if(!m) { log_debug(("Worker done")); return 0; }
    job(sq,m);
    free(m);
  }
}

static int result(struct syncqueue *q,struct member *job) {
  struct source *src;

  log_debug(("processing response type A%d",job->type.a));
  switch(job->type.a) {
  case A_QUIT:
    return 1;
    break;
  case A_READ:
    if(job->ck) { rq_found_data(job->rq,job->ck); }
    src_release(job->src->src);
    rq_run_next(job->rq);
    rq_release(job->rq);
    break;
  case A_WRITE:
    src_release(job->src->src);
    rq_run_next_write(job->rq);
    rq_release(job->rq);
    break;
  case A_CLOSE:
    src = job->src->src;
    src_release(job->src->src);
    free(job->src);
    job->src = 0;
    sq_release(q);
    break;
  }
  return 0;
}

static void sq_consume(void *data,void *priv) {
  struct member *m = (struct member *)data;
  struct syncqueue *sq = (struct syncqueue *)priv;

  log_debug(("got consumable"));
  if(result(sq,m)) {
    log_debug(("consumer quitting"));
    ref_release_weak(&(sq->r));
  }
  free(m);
}

struct event * sq_consumer(struct syncqueue *sq) {
  return evdata_event(sq->ans);
}

static struct member * new_member_ck(union type type,
                                     struct syncsource *src,
                                     struct request *rq,
                                     struct chunk *ck) {
  struct member *m;

  m = safe_malloc(sizeof(struct member));
  m->type = type;
  if(src && src->src) { src_acquire(src->src); }
  m->src = src;
  m->rq = rq;
  m->ck = ck;
  return m;
}

static void add_q(struct syncqueue *sq,enum qtype qtype,
                  struct syncsource *src,struct request *rq,
                  struct chunk *ck) {
  union type type;

  type.q = qtype;
  wqueue_send_work(sq->qu,new_member_ck(type,src,rq,ck));
}


static void add_a(struct syncqueue *sq,enum atype atype,
                   struct syncsource *src,struct request *rq,
                   struct chunk *ck) {
  union type type;

  type.a = atype;
  evdata_send(sq->ans,new_member_ck(type,src,rq,ck));
}

static void sq_destroy(void *data) {
  struct syncqueue *sq = (struct syncqueue *)data;
  int i;

  log_debug(("syncqueue release"));
  wqueue_acquire_weak(sq->qu);
  wqueue_release(sq->qu);
  for(i=0;i<NUMTHREADS;i++) {
    pthread_join(sq->threads[i],0);
  }
  ref_acquire_weak(&(sq->r)); /* We hang around until A_QUIT processed */
  log_debug(("threads released, waiting for A_QUIT"));
  add_a(sq,A_QUIT,0,0,0);
}

static void sq_free(void *data) {
  struct syncqueue *sq = (struct syncqueue *)data;
  
  log_debug(("syncqueue free"));
  wqueue_release_weak(sq->qu);
  evdata_release(sq->ans);
  free(sq);
}

struct ref * sq_ref(struct syncqueue *sq) { return &(sq->r); }
void sq_acquire(struct syncqueue *sq) { ref_acquire(&(sq->r)); }
void sq_release(struct syncqueue *sq) { ref_release(&(sq->r)); }

struct syncqueue * sq_create(struct event_base *eb) {
  struct syncqueue *sq;
  pthread_attr_t attr;
  int i;

  sq = safe_malloc(sizeof(struct syncqueue));
  ref_create(&(sq->r));
  ref_on_release(&(sq->r),sq_destroy,sq);
  ref_on_free(&(sq->r),sq_free,sq);
  sq->quitting = 0;
  sq->qu = wqueue_create();
  sq->ans = evdata_create(eb,sq_consume,sq);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for(i=0;i<NUMTHREADS;i++) {
    pthread_create(&(sq->threads[i]),&attr,worker,sq);
  }
  return sq;
}


static void src_close(struct source *src) {
  struct syncsource *ss = (struct syncsource *)(src->priv);

  log_debug(("adding src_close job"));
  if(!ss) { return; }
  add_q(ss->sq,Q_CLOSE,ss,0,0);
}

static void src_read(struct source *src,struct request *rq) {
  struct syncsource *ss = (struct syncsource *)(src->priv);

  if(ss->read) {
    log_debug(("adding src_read job"));
    rq_acquire(rq);
    add_q(ss->sq,Q_READ,ss,rq,0);
  } else {
    rq_run_next(rq);
  }
}

static void src_write(struct source *src,struct request *rq,
		      struct chunk *ck) {
  struct syncsource *ss = (struct syncsource *)(src->priv);

  if(ss->write) {
    log_debug(("adding src_write job"));
    rq_acquire(rq);
    add_q(ss->sq,Q_WRITE,ss,rq,ck);
  } else {
    rq_run_next_write(rq);
  }
}

struct source * syncsource_create(struct syncqueue *sq,
                                  struct syncsource *ss) {
  struct source *src;

  ss->sq = sq;
  src = src_create();
  src->priv = ss;
  src->close = src_close;
  src->read = src_read;
  src->write = src_write;
  src->stat = 0; // XXX support (can be sync)
  src->readlink = 0; // XXX support (can be sync)
  ss->src = src;
  sq_acquire(sq);
  return src;
}

struct source * syncsource_source(struct syncsource *ss) {
  return ss->src;
}

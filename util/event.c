#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <event2/event.h>

#include "event.h"
#include "misc.h"
#include "array.h"
#include "logging.h"
#include "queue.h"

CONFIG_LOGGING(event);

/***** DATA EVENT, ie threads -> event *****/

typedef void (*evdata_cb_fn)(void *data,void *priv);

struct evdata {
  struct ref r;
  pthread_mutex_t mutex;
  struct event *ev;
  struct array *queue;
  int pipe[2];
  evdata_cb_fn cb;
  void *priv;
};

static void ring_bell(struct evdata *ed) {
  int i,r;
  char c = '\0';

  for(i=0;i<100;i++) {
    r = write(ed->pipe[1],&c,1);
    if(r>0) { log_debug(("rang bell i=%d",i)); return; }
    usleep(i*i*1000); 
  }
  die("Could not write to internal pipe\n");
}

void evdata_send(struct evdata *ed,void *data) {
  pthread_mutex_lock(&(ed->mutex));
  array_insert(ed->queue,data);
  ring_bell(ed);
  pthread_mutex_unlock(&(ed->mutex));
}

#define GULP 128
static void reset_bell(struct evdata *ed) {
  int r;
  char c[GULP];

  // XXX +EWOULDBLOCK for security
  r = read(ed->pipe[0],c,GULP);
  if(r<0 && r!=EAGAIN && r!=EWOULDBLOCK && r!=EINTR) {
    die("Could not read from pipe");
  }
}

static void consume(evutil_socket_t fd,short what,void *arg) {
  struct evdata *ed = (struct evdata *)arg;
  struct array *ev;
  void *data;
  int i;

  log_debug(("waiting for consumables"));
  reset_bell(ed);
  log_debug(("got consumables"));
  pthread_mutex_lock(&(ed->mutex));
  ev = ed->queue;
  ed->queue = array_create(0,0);
  pthread_mutex_unlock(&(ed->mutex));
  for(i=0;i<array_length(ev);i++) {
    ed->cb(array_index(ev,i),ed->priv);
  }
  array_release(ev);
}

static void ed_release(void *data) {
  struct evdata *ed = (struct evdata *)data;

  array_release(ed->queue);
}

static void ed_free(void *data) {
  struct evdata *ed = (struct evdata *)data;

  event_del(ed->ev);
  event_free(ed->ev);
  pthread_mutex_destroy(&(ed->mutex));
  close(ed->pipe[0]);
  close(ed->pipe[1]);
  free(ed);
}

struct evdata * evdata_create(struct event_base *eb,
                              evdata_cb_fn cb,void *priv) {
  struct evdata *ed;

  ed = safe_malloc(sizeof(struct evdata));
  ref_create(&(ed->r));
  ref_on_release(&(ed->r),ed_release,ed);
  ref_on_free(&(ed->r),ed_free,ed);
  pthread_mutex_init(&(ed->mutex),0);
  ed->queue = array_create(0,0);
  if(pipe(ed->pipe)<0) { die("Cannot create pipe"); }
  ed->cb = cb;
  ed->priv = priv;
  ed->ev = event_new(eb,ed->pipe[0],EV_READ|EV_PERSIST,consume,ed);
  return ed;
}

struct event * evdata_event(struct evdata *ed) { return ed->ev; }

void evdata_release(struct evdata *ed) { ref_release(&(ed->r)); }

/***** WORK QUEUE (ie event -> threads) *****/

struct wqueue {
  struct ref r;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  struct queue *queue;
  int quit;
};

static void wq_release(void *data) {
  struct wqueue *wq = (struct wqueue *)data;

  log_debug(("work queue released"));
  pthread_mutex_lock(&(wq->mutex));
  wq->quit = 1;
  pthread_cond_broadcast(&(wq->cond));
  pthread_mutex_unlock(&(wq->mutex));
}

static void wq_free(void *data) {
  struct wqueue *wq = (struct wqueue *)data;

  log_debug(("work queue freed"));
  queue_release(wq->queue);
  pthread_mutex_destroy(&(wq->mutex));
  pthread_cond_destroy(&(wq->cond));
  free(wq);
}


void * wqueue_get_work(struct wqueue *wq) {
  void *out;

  pthread_mutex_lock(&(wq->mutex));
  while(!queue_length(wq->queue)) {
    if(wq->quit) {
      pthread_mutex_unlock(&(wq->mutex));
      return 0;
    }
    pthread_cond_wait(&(wq->cond),&(wq->mutex)); 
  }
  out = queue_remove(wq->queue);
  if(queue_length(wq->queue)) {
    pthread_cond_signal(&(wq->cond));
  }
  pthread_mutex_unlock(&(wq->mutex));
  return out;
}

void wqueue_send_work(struct wqueue *wq,void *work) {
  pthread_mutex_lock(&(wq->mutex));
  if(!queue_length(wq->queue)) {
    pthread_cond_signal(&(wq->cond));
  }
  queue_add(wq->queue,work);
  pthread_mutex_unlock(&(wq->mutex));
}

struct wqueue * wqueue_create(void) {
  struct wqueue *wq;

  wq = safe_malloc(sizeof(struct wqueue));
  ref_create(&(wq->r));
  ref_on_release(&(wq->r),wq_release,wq);
  ref_on_free(&(wq->r),wq_free,wq);
  pthread_mutex_init(&(wq->mutex),0);
  pthread_cond_init(&(wq->cond),0);
  wq->queue = queue_create(0,0);
  wq->quit = 0;
  return wq;
}

void wqueue_release(struct wqueue *wq) { ref_release(&(wq->r)); }
void wqueue_acquire(struct wqueue *wq) { ref_acquire(&(wq->r)); }
void wqueue_release_weak(struct wqueue *wq) { ref_release_weak(&(wq->r)); }
void wqueue_acquire_weak(struct wqueue *wq) { ref_acquire_weak(&(wq->r)); }

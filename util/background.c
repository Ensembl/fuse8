#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "background.h"

#include "misc.h"
#include "queue.h"
#include "event.h"
#include "logging.h"

CONFIG_LOGGING(background)

struct background {
  bgd_fn fn;
  void *payload;
  struct ref r;
  struct wqueue *wq;
  pthread_t thread;
  int running;
};

#define FLAG_QUIT 0x00000001

static void bb_ref_release(void *data) {
  struct background *bb = (struct background *)data;

  background_stop(bb);
}

static void bb_ref_free(void *data) {
  struct background *bb = (struct background *)data;
  void *more;

  background_stop(bb); // Belt and braces
  /* Free pending jobs */
  wqueue_acquire_weak(bb->wq);
  wqueue_release(bb->wq);
  log_debug(("draining jobs"));
  while(!wqueue_should_quit(bb->wq)) {
    more = wqueue_get_work(bb->wq);
    if(more) { bb->fn(bb,more,1,bb->payload); }
  }
  log_debug(("jobs drained"));
  /* Done */
  wqueue_release_weak(bb->wq);
  free(bb);
}

struct background * background_create(bgd_fn fn,void *payload) {
  struct background *bb;

  bb = safe_malloc(sizeof(struct background));
  bb->wq = wqueue_create();
  ref_create(&(bb->r));
  ref_on_release(&(bb->r),bb_ref_release,bb);
  ref_on_free(&(bb->r),bb_ref_free,bb);
  bb->running = 0;
  bb->fn = fn;
  bb->payload = payload;
  return bb;
}

void background_release(struct background *bb) {
  ref_release(&(bb->r));
}

void background_add(struct background *bb,void *filename) {
  wqueue_send_work(bb->wq,strdup(filename));
}

static void * worker(void *data) {
  struct background *bb = (struct background *)data;
  void *next;

  // XXX check priority doesn't update other threads
  setpriority(PRIO_PROCESS,0,19);
  while(1) {
    next = wqueue_get_work(bb->wq);
    if(wqueue_should_quit(bb->wq)) { break; }
    if(wqueue_get_flags(bb->wq,FLAG_QUIT)) { break; }
    if(!next) { continue; }
    bb->fn(bb,next,0,bb->payload);
  }
  log_debug(("worker quitting"));
  return 0;
}

void background_start(struct background *bb) {
  pthread_attr_t attr;

  if(bb->running) { return; }
  ref_acquire_weak(&(bb->r));
  pthread_attr_init(&attr); // XXX check for error
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
  log_debug(("launching thread"));
  pthread_create(&(bb->thread),&attr,worker,bb);
  bb->running = 1;
}

void background_stop(struct background *bb) {
  if(!bb->running) { return; }
  wqueue_set_flags(bb->wq,FLAG_QUIT,0);
  log_debug(("waiting for stop (join)"));
  pthread_join(bb->thread,0);
  log_debug(("received stop (join)"));
  bb->running = 0;
  ref_release_weak(&(bb->r));
}

void background_finish(struct background *bb) {
  if(!bb->running) { return; }
  wqueue_quit(bb->wq,1);
  log_debug(("waiting for finish (join)"));
  pthread_join(bb->thread,0);
  log_debug(("received finish (join)"));
  wqueue_quit(bb->wq,0);
  bb->running = 0;
  ref_release_weak(&(bb->r));
}

void background_yield(struct background *bb) {
  sched_yield();
}

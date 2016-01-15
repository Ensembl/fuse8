#include "failures.h"

#include <string.h>
#include <event2/event.h>
#include <inttypes.h>

#include "util/misc.h"
#include "util/logging.h"

CONFIG_LOGGING(failures)

struct fail {
  char *where;
  int64_t at;
};

struct failures {
  uint64_t timeout;
  struct array *fails;
  struct event *timer;
};

static void tick(evutil_socket_t fd,short what,void *arg) {
  struct failures *f = (struct failures *)arg;
  struct fail *fl;
  struct array *new;
  uint64_t now;
  int i;

  log_debug(("timer tick"));
  now = microtime();
  new = array_create(0,0);
  for(i=0;i<array_length(f->fails);i++) {
    fl = array_index(f->fails,i);
    if(f->timeout && fl->at+f->timeout > now) {
      log_debug(("Failure for '%s' has not expired",fl->where));
      array_insert(new,fl);
    } else {
      log_debug(("Failure for '%s' has expired",fl->where));
      free(fl->where);
      free(fl);
    }
  }
  array_release(f->fails);
  f->fails = new;
}

void failures_free(struct failures *f) {
  log_debug(("free"));
  f->timeout = 0;
  tick(0,0,f);
  array_release(f->fails);
  event_del(f->timer);
  event_free(f->timer);
  free(f);
}

struct failures * failures_new(struct event_base *eb,uint64_t timeout) {
  struct failures *f;
  struct timeval interval;

  f = safe_malloc(sizeof(struct failures));
  f->fails = array_create(0,0);
  f->timer = event_new(eb,-1,EV_PERSIST,tick,f);
  f->timeout = timeout * 1000000;
  interval.tv_sec = (timeout/4000000);
  interval.tv_usec = 0;
  event_add(f->timer,&interval);
  return f;
}

void failures_fail(struct failures *f,char *path) {
  struct fail *fl;

  log_debug(("Setting failure for '%s'",path));
  fl = safe_malloc(sizeof(struct fail));
  fl->where = strdup(path);
  fl->at = microtime();
  array_insert(f->fails,fl);
}

int failures_check(struct failures *f,char *path) {
  struct fail *fl;
  int i;
  int64_t now;
  
  now = microtime();
  for(i=0;i<array_length(f->fails);i++) {
    fl = array_index(f->fails,i);
    if(f->timeout && !strcmp(path,fl->where) && fl->at+f->timeout > now) {
      log_debug(("Refusing '%s': it has failed",path));
      return 0;
    }
  }
  log_debug(("'%s' has not failed",path));
  return 1;
}

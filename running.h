#ifndef RUNNING_H
#define RUNNING_H

#include <event2/event.h>
#include <event2/dns.h>

#include "jpf/jpf.h"
#include "running.h"
#include "util/array.h"
#include "sourcelist.h"
#include "syncif.h"

struct running;

typedef struct source * (*src_create_fn)(struct running *rr,
                                         struct jpf_value *config);

void run_src_register(struct running *rr,char *type,src_create_fn fn);

typedef struct interface * (*ic_create_fn)(struct running *rr,
                                           struct jpf_value *config);

void run_ic_register(struct running *rr,char *type,ic_create_fn fn);

/* Feel free to read */
struct running {
  int have_quit,stats_fd;
  struct event_base *eb;
  struct evdns_base *edb;
  struct assoc *src_shop,*ic_shop;
  struct sourcelist *sl;
  struct syncqueue *sq;
  struct syncif *si;
  struct rotator *rot;
  struct array *icc;
  struct array *src;
  struct ref need_loop;
  struct ref ic_running;
  struct event *stat_timer,*sigkill_timer;
  struct timeval stat_timer_interval;
};

void run(char *);

#endif

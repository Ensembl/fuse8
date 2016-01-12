#ifndef SYNCSOURCE_H
#define SYNCSOURCE_H

#include <event2/event.h>
#include "util/event.h"
#include "types.h"

struct syncqueue;
struct syncsource;

struct syncqueue * sq_create(struct event_base *eb);
struct event * sq_consumer(struct syncqueue *ed);

typedef void (*ss_fn)(struct syncsource *);
typedef struct chunk * (*ss_read_fn)(struct syncsource *,struct request *rq);
typedef struct chunk * (*ss_write_fn)(struct syncsource *,struct request *rq,
                                      struct chunk *ck);

struct syncsource {
  struct syncqueue *sq;
  struct source *src;
  char *name;
  ss_fn close;
  ss_read_fn read;
  ss_write_fn write;
  void *priv;
};

struct source * syncsource_create(struct syncqueue *q,struct syncsource *ss);
struct ref * sq_ref(struct syncqueue *sq);
void sq_acquire(struct syncqueue *);
void sq_release(struct syncqueue *);
struct source * syncsource_source(struct syncsource *ss);

#endif

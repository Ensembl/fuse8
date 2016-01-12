#include "util/misc.h"
#include "util/event.h"
#include "util/logging.h"
#include "sourcelist.h"
#include "types.h"
#include "syncif.h"

CONFIG_LOGGING(syncif);

enum type {
  I_READ, I_STAT, I_LOOKUP, I_READDIR, I_READLINK
};

struct irequest {
  enum type type;
  struct sourcelist *sl;
  /* used by read */
  char *spec;
  int64_t offset,length;
  req_fn done;
  void *priv;
};

struct syncif {
  struct ref r;
  struct evdata *ed;
};

void si_read(struct syncif *si,struct sourcelist *sl,char *spec,
             int64_t offset,int64_t length,req_fn done,void *priv) {
  struct irequest *rq;

  rq = safe_malloc(sizeof(struct irequest));
  rq->type = I_READ;
  rq->sl = sl;
  rq->spec = spec;
  rq->offset = offset;
  rq->length = length;
  rq->done = done;
  rq->priv = priv;
  evdata_send(si->ed,rq);
}

static void if_consume(void *data,void *priv) {
  struct irequest *rq = (struct irequest *)data;
  //struct syncif *si = (struct syncif *)priv;

  log_debug(("if consuming"));
  switch(rq->type) {
  case I_READ:
    sl_read(rq->sl,rq->spec,rq->offset,rq->length,rq->done,rq->priv);
    break;
  }
  free(rq);
  log_debug(("if consumed"));
}

static void si_on_release(void *data) {
}

static void si_on_free(void *data) {
  struct syncif *si = (struct syncif *)data;

  evdata_release(si->ed);
  free(si);
}

struct syncif * syncif_create(struct event_base *eb) {
  struct syncif *si;

  si = safe_malloc(sizeof(struct syncif));
  ref_create(&(si->r));
  ref_on_release(&(si->r),si_on_release,si);
  ref_on_free(&(si->r),si_on_free,si);
  si->ed = evdata_create(eb,if_consume,si);
  return si;
}

struct event * si_consumer(struct syncif *si) {
  return evdata_event(si->ed);
}

void si_release(struct syncif *si) { ref_release(&(si->r)); }

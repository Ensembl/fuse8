#include "hits.h"

#include <inttypes.h>
#include <string.h>
#include <event2/event.h>

#include "util/misc.h"
#include "util/assoc.h"
#include "jpf/jpf.h"

struct hit {
  char *uri;
  int64_t bytes;
  struct assoc *sources;
};

struct hits {
  struct assoc *data;
  struct event *timer;
  int fd;
};

static void add_entry(struct hit *h,struct jpf_value *entry) {
  struct assoc_iter ai;
  struct jpf_value *sources;

  jpfv_assoc_add(entry,"uri",jpfv_string(h->uri));
  jpfv_assoc_add(entry,"bytes",jpfv_number_int(h->bytes));
  sources = jpfv_array();
  associ_start(h->sources,&ai);
  while(associ_next(&ai)) {
    jpfv_array_add(sources,jpfv_string(associ_key(&ai)));
  }
  jpfv_assoc_add(entry,"sources",sources);
}

static void add_entries(struct hits *hh,struct jpf_value *entries) {
  struct assoc_iter ai;
  struct jpf_value *entry;

  associ_start(hh->data,&ai);
  while(associ_next(&ai)) {
    entry = jpfv_assoc();
    add_entry((struct hit *)associ_value(&ai),entry);
    jpfv_array_add(entries,entry);
  } 
}

static void hit_free(void *target,void *priv) {
  struct hit *h = (struct hit *)target;

  assoc_release(h->sources);
  free(h);
}

static void hits_dump(evutil_socket_t fd,short what,void *arg) {
  struct hits *hh = (struct hits *)arg;
  struct jpf_value *record,*entries,*requests;
  struct jpf_callbacks jpf_emitter_cb;
  struct jpf_emitter jpf_emitter;
  char *time_str;

  entries = jpfv_important_assoc(0);
  time_str = iso_localtime(0);
  jpfv_assoc_add(entries,"time",jpfv_string(time_str));
  free(time_str);
  requests = jpfv_important_array(0);
  add_entries(hh,requests);
  jpfv_assoc_add(entries,"requests",requests);
  record = jpfv_important_array(1);
  jpfv_array_add(record,entries);
  jpf_emit_fd(&jpf_emitter_cb,&jpf_emitter,hh->fd);
  jpf_emit_df(record,&jpf_emitter_cb,&jpf_emitter);
  jpf_emit_done(&jpf_emitter);
  jpfv_free(record);
  /* clear */
  assoc_release(hh->data);
  hh->data = assoc_create(type_free,0,hit_free,0);
}

struct hits * hits_new(struct event_base *eb,int fd,uint64_t interval) {
  struct hits *hh;
  struct timeval tv;

  hh = safe_malloc(sizeof(struct hits));
  hh->data = assoc_create(type_free,0,hit_free,0);
  hh->timer = event_new(eb,-1,EV_PERSIST,hits_dump,hh);
  tv.tv_sec = interval;
  tv.tv_usec = 0;
  event_add(hh->timer,&tv);
  hh->fd = fd;
  return hh;
}

void hits_free(struct hits *h) {
  event_del(h->timer);
  event_free(h->timer);
  assoc_release(h->data);
  free(h);
}

void hit_add(struct hits *hh,char *uri,char *source,int64_t bytes) {
  struct hit *h;

  source = strdup(source);
  h = (struct hit *)assoc_lookup(hh->data,uri);
  if(!h) {
    h = safe_malloc(sizeof(struct hit));
    h->uri = strdup(uri);
    h->bytes = 0;
    h->sources = assoc_create(type_free,0,0,0);
    assoc_set(hh->data,h->uri,h);
  }
  h->bytes += bytes;
  assoc_set(h->sources,source,source);
}


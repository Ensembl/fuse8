#ifndef UTIL_EVENT_H
#define UTIL_EVENT_H

#include <event2/event.h>

typedef void (*evdata_cb_fn)(void *data,void *priv);

/* DATA EVENTS */
struct evdata;

void evdata_send(struct evdata *ed,void *data);
struct evdata * evdata_create(struct event_base *eb,
                              evdata_cb_fn cb,void *priv);
struct event * evdata_event(struct evdata *ed);
void evdata_release(struct evdata *ed);

/* WORK QUEUES */

struct wqueue;

void * wqueue_get_work(struct wqueue *wq);
void wqueue_send_work(struct wqueue *wq,void *work);
struct wqueue * wqueue_create(void);

void wqueue_release(struct wqueue *wq);
void wqueue_acquire(struct wqueue *wq);
void wqueue_release_weak(struct wqueue *wq);
void wqueue_acquire_weak(struct wqueue *wq);

int wqueue_should_quit(struct wqueue *wq);
void wqueue_quit(struct wqueue *wq,int val);

void wqueue_set_flags(struct wqueue *wq,int set,int reset);
int wqueue_get_flags(struct wqueue *wq,int mask);

#endif

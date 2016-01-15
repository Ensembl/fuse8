#ifndef HITS_H
#define HITS_H

#include <inttypes.h>
#include <event2/event.h>

struct hits;

struct hits * hits_new(struct event_base *eb,int fd,uint64_t interval);
void hits_free(struct hits *h);
void hit_add(struct hits *hh,char *uri,char *source,int64_t bytes);

#endif

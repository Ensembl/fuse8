#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <stdlib.h>
#include <inttypes.h>

struct httpclient;

#include "connection.h"

struct http_stats {
  int64_t dns_time;
};

struct httpclient {
  /* libevent stuff */
  struct event_base *eb;
  struct evdns_base *edb;
  struct connections *cnn;
};

typedef void (*http_fn)(int,char *,int64_t,int,void *,struct http_stats *);

struct httpclient * httpclient_create(struct event_base *eb,
                                      struct evdns_base *edb);
void httpclient_finish(struct httpclient *cli);
void http_request(struct httpclient *cli,
                  char *uris,size_t off,size_t size,
                  http_fn callback,void *priv);


#endif

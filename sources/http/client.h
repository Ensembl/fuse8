#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <stdlib.h>
#include <inttypes.h>

struct httpclient;

#include "connection.h"
#include "../../util/misc.h"

struct http_stats {
  int64_t dns_time;
};

typedef void (*http_finished)(void *);

struct httpclient {
  http_finished f_cb;
  void *f_priv;
  /* libevent stuff */
  struct event_base *eb;
  struct evdns_base *edb;
  struct connections *cnn;
};

typedef void (*http_fn)(int,char *,int64_t,int,void *,struct http_stats *);

struct httpclient * httpclient_create(struct event_base *eb,
                                      struct evdns_base *edb);
void httpclient_finish(struct httpclient *cli,http_finished cb,void *priv);
void http_request(struct httpclient *cli,
                  char *uris,size_t off,size_t size,
                  http_fn callback,void *priv);


#endif

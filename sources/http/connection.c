#include <stdio.h>
#include <string.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/util.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "connection.h"
#include "client.h"
#include "../../util/misc.h"
#include "../../util/logging.h"
#include "../../util/dns.h"

CONFIG_LOGGING(http);

typedef void (*conn_cb)(struct connection *conn,void *priv);

// XXX dns time

struct connections {
  struct connection *conns;
  struct httpclient *cli;
  struct event *timer;

  /* stats */
  int64_t n_new,dns_time;
};

struct connection {
  struct connections *cnn;
  struct evhttp_connection *evcon;
  char *host;
  int port;
  int64_t last_used;
  int refs;
  conn_cb callback;
  void *priv;
  struct connection *next;

  /* stats */
  int64_t dns_start; 
};

static void free_connection(struct connection *cn) {
  log_debug(("Freeing connection to %s",cn->host));
  if(cn->evcon) { evhttp_connection_free(cn->evcon); }
  free(cn->host);
  free(cn);
}

#define TOO_OLD 5000000 /* 5 sec */
static void timer(evutil_socket_t fd,short what,void *arg) {
  struct connections *cnn = (struct connections *)arg;
  struct connection *cn,*cn2,*new,**end;
  int64_t now;

  log_debug(("timer called"));
  now = microtime();
  new = 0;
  end = &new;
  for(cn=cnn->conns;cn;cn=cn2) {
    cn2 = cn->next;
    if(cn->refs || cn->last_used+TOO_OLD > now) {
      /* keep it */
      *end = cn;
      cn->next = 0;
      end = &(cn->next);
    } else {
      free_connection(cn);
    }
  }
  cnn->conns = new;
}

void unget_connection(struct connection *conn) {
  conn->refs--;
}

static void conn_dns_done(const char *host,void *data) {
  struct connection *conn = (struct connection *)data;

  conn->cnn->dns_time += microtime() - conn->dns_start;
  conn->evcon = evhttp_connection_base_new(conn->cnn->cli->eb,0,
                                           host,conn->port);
  conn->callback(conn,conn->priv);
  /* link it in */
  conn->next = conn->cnn->conns;
  conn->cnn->conns = conn;  
}

// XXX multi-creation race
// TEST timeouts

void get_connection(struct connections *cnn,
                    const char *host,int port,
                    conn_cb callback,void *priv) {
  struct connection *cn;
  int64_t now;

  now = microtime();
  for(cn=cnn->conns;cn;cn=cn->next) {
    if(!strcmp(host,cn->host) && port == cn->port) {
      cn->refs++;
      cn->last_used = now;
      log_debug(("Using existing connection"));
      callback(cn,priv);
      return;
    }
  }
  log_debug(("Creating new connection"));
  cn = safe_malloc(sizeof(struct connection));
  cn->cnn = cnn;
  cn->host = strdup(host);
  cn->port = port;
  cn->callback = callback;
  cn->priv = priv;
  cn->cnn = cnn;
  cn->refs = 1;
  cn->next = 0;
  cn->last_used = now;
  cnn->n_new++;
  cn->dns_start = microtime();
  dns_resolve(cnn->cli->edb,host,conn_dns_done,cn);
}

struct evhttp_connection * evconnection(struct connection *conn) {
  return conn->evcon;
}

struct connections * cnn_make(struct httpclient *cli) {
  struct connections *cnn;
  struct timeval timer_period = {3,0};

  cnn = safe_malloc(sizeof(struct connections));
  cnn->conns = 0;
  cnn->n_new = 0;
  cnn->dns_time = 0;
  cnn->cli = cli;
  cnn->timer = event_new(cli->eb,-1,EV_PERSIST,timer,cnn);
  event_add(cnn->timer,&timer_period);
  return cnn;
}

void cnn_free(struct connections *cnn) {
  struct connection *cn,*cn2;

  for(cn=cnn->conns;cn;cn=cn2) {
    cn2 = cn->next;
    free_connection(cn);
  }
  event_del(cnn->timer);
  event_free(cnn->timer);
  free(cnn); 
}

void cnn_stats(struct connections *cnn,int64_t *n_new,int64_t *dns_time) {
  if(n_new) { *n_new = cnn->n_new; }
  if(dns_time) { *dns_time = cnn->dns_time; }
}

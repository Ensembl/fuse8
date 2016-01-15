#include "connection.h"

#include <string.h>
#include <event2/event.h>
#include <event2/http.h>

#include "client.h"
#include "../../util/logging.h"
#include "../../util/dns.h"

/* connections contains the global state for this module.
 * endpoint contains the state for a host/port combination.
 * connection contains an individual connection.
 * conn_request conetains an individual request for a connection.
 *
 * We limit the number of simultaneous connecitons to be nice to remote
 * servers.
 *
 * try_link tries to match existing connections to existing requests and
 * if it does, marks the connection as in use and returns it, removing it
 * from the request list.
 *
 * try_new, if there are pending requests and there aren't too many
 * connections creates new connections. It then calls try_resolve.
 *
 * try_resolve issues a DNS query on behalf of a new connection. When a
 * response comes back it's put into the ready state, as accepted by
 * try_link. And then calls it.
 *
 * get_connection places a request on the request queue. It then, in
 * hope, calls try_link for an existing ready connection. It then calls
 * try_new. If try_link succeeded the request will already be gone. If not,
 * then try_link will attempt to establish new connections.
 *
 * unget_connection places the connection back into the ready state, and
 * then recalls try_link to satisfy any pending requests.
 *
 * tidy does periodic conneciton tidying. Old connections are removed, and
 * then try_new is called to see if any new connections can be created.
 *
 * Whenever connections become ready, try_link must be called. Whenever
 * connections are freed try_new must be called.
 */

CONFIG_LOGGING(http);

// XXX timeout inuse
// XXX configurable
#define MAX_CONN 3
#define DNS_WAIT 30000000

struct connections {
  struct ref r;
  struct httpclient *cli;
  struct event * timer;
  struct endpoint *epp;

  /* stats */
  int64_t n_new,dns_time;
};

struct endpoint {
  char *host;
  int port;
  int n_paused,n_conn;
  struct connections *cnn;
  struct connection *conn;
  struct conn_request *rqq;
  struct endpoint *next;
};

enum conn_state {
  CONN_NEW, CONN_AWAITDNS, CONN_READY, CONN_INUSE, CONN_FAILEDDNS
};

struct connection {
  enum conn_state state;
  struct endpoint *ep;
  struct evhttp_connection *evcon;
  struct connection *next;
  int64_t last_used;

  /* stats */
  int64_t dns_start;
};

struct conn_request {
  conn_cb callback;
  void *priv;
  struct conn_request *next;
};

#if 0
/* Useful for extreme debugging */
static void dump_ep(struct endpoint *ep) {
  struct connection *conn;

  log_debug(("dump"));
  for(conn=ep->conn;conn;conn=conn->next) {
    log_debug(("  -> %d",conn->state));
  }
}
#endif

static void free_connection(struct connection *conn) {
  if(conn->evcon) { evhttp_connection_free(conn->evcon); }
  free(conn);
}

static void free_endpoint(struct endpoint *ep) {
  struct endpoint **epp;

  log_debug(("freeing endpoint"));
  for(epp=&(ep->cnn->epp);*epp;epp=&((*epp)->next)) {
    if(*epp==ep) {
      *epp=ep->next;
      break;
    }
  }
  ref_release_weak(&(ep->cnn->r));
  free(ep->host);
  free(ep);
}

static struct endpoint * get_endpoint(struct connections *cnn,
                                      const char *host,int port) {
  struct endpoint *ep;

  for(ep=cnn->epp;ep;ep=ep->next) {
    if(!strcmp(ep->host,host) && ep->port == port) {
      return ep;
    }
  }
  ep = safe_malloc(sizeof(struct endpoint));
  ep->host = strdup(host);
  ep->port = port;
  ep->next = cnn->epp;
  ep->rqq = 0;
  ep->n_paused = 0;
  ep->n_conn = 0;
  ep->cnn = cnn;
  ep->conn = 0;
  cnn->epp = ep;
  ref_acquire_weak(&(cnn->r));
  return ep;
}

static void try_link(struct endpoint *ep) {
  struct connection *conn;
  struct conn_request *rqq;

  for(conn=ep->conn;conn;conn=conn->next) {
    if(conn->state != CONN_READY) { continue; }
    if(!ep->rqq) { return; }
    log_debug(("Request satisfied"));
    rqq = ep->rqq;
    ep->rqq = ep->rqq->next;
    conn->state = CONN_INUSE;
    conn->last_used = microtime();
    rqq->callback(conn,rqq->priv);
    free(rqq);  
  } 
}

void unget_connection(struct connection *conn) {
  log_debug(("Connection returned"));
  conn->state = CONN_READY;
  try_link(conn->ep);
}

static void resolved(const char *host,void *data) {
  struct connection *cn = (struct connection *)data;

  // XXX failed DNS
  if(!host) {
    log_warn(("DNS failed"));
    cn->state = CONN_FAILEDDNS;
    cn->last_used = microtime();
    return;
  }
  log_debug(("DNS answer"));
  cn->evcon = evhttp_connection_base_new(cn->ep->cnn->cli->eb,0,host,
                                         cn->ep->port);
  cn->state = CONN_READY;
  cn->ep->cnn->n_new++;
  cn->ep->cnn->dns_time += microtime() - cn->dns_start;
  try_link(cn->ep);
}

static void try_resolve(struct endpoint *ep) {
  struct connection *conn;

  for(conn=ep->conn;conn;conn=conn->next) {
    if(conn->state != CONN_NEW) { continue; }
    conn->state = CONN_AWAITDNS;
    conn->dns_start = microtime();
    log_debug(("DNS question"));
    dns_resolve(ep->cnn->cli->edb,ep->host,resolved,conn);
  }
}

static void try_new(struct endpoint *ep) {
  struct connection *conn;
  int i;

  for(i=0;ep->rqq && ep->n_conn<MAX_CONN;i++) {
    log_debug(("New connection"));
    conn = safe_malloc(sizeof(struct connection));
    conn->state = CONN_NEW;
    conn->ep = ep;
    conn->evcon = 0;
    conn->next = ep->conn;
    ep->conn = conn;
    ep->n_conn++;
    ep->n_paused--;
  }
  try_resolve(ep);
}

void get_connection(struct connections *cnn,
                    const char *host,int port,
                    conn_cb callback,void *priv) {
  struct endpoint *ep;
  struct conn_request *crq;

  log_debug(("Request %s:%d",host,port));
  crq = safe_malloc(sizeof(struct conn_request));
  ep = get_endpoint(cnn,host,port);
  crq->callback = callback;
  crq->priv = priv;
  crq->next = ep->rqq;
  ep->rqq = crq;
  ep->n_paused++;
  try_link(ep);
  try_new(ep);
}

struct evhttp_connection * evconnection(struct connection *conn) {
  return conn->evcon;
}

#define TOO_OLD 5000000
static void tidy_endpoint(struct endpoint *ep) {
  struct connection *c,*new;
  uint64_t now;

  now = microtime();
  new = 0;
  while(ep->conn) {
    c = ep->conn;
    ep->conn = c->next;
    if(c->state == CONN_READY && c->last_used+TOO_OLD < now) {
      /* dispose */
      log_debug(("tidying away connection"));
      free_connection(c);
      ep->n_conn--;
    } else {
      /* keep */
      if(c->state == CONN_FAILEDDNS && c->last_used+DNS_WAIT <now) {
        c->state = CONN_NEW;
      }
      c->next = new;
      new = c;
    }
  }
  try_resolve(ep);
  ep->conn = new;
  try_new(ep);
  if(!ep->conn && !ep->rqq) {
    free_endpoint(ep);
  }
}

static void tidy(evutil_socket_t fd,short what,void *arg) {
  struct connections *cnn = (struct connections *)arg;
  struct endpoint *ep,*ep2;

  for(ep=cnn->epp;ep;ep=ep2) {
    ep2 = ep->next;
    tidy_endpoint(ep);
  }
}

static void cnn_on_release(void *priv) {
  struct connections *cnn = (struct connections *)priv;

  event_del(cnn->timer);
}

static void cnn_on_free(void *priv) {
  struct connections *cnn = (struct connections *)priv;

  event_free(cnn->timer);
  free(cnn);  
}

struct connections * cnn_make(struct httpclient *cli) {
  struct connections *cnn;
  struct timeval timer_period = {3,0};

  cnn = safe_malloc(sizeof(struct connections));
  cnn->epp = 0;
  cnn->dns_time = 0;
  cnn->n_new = 0;
  cnn->cli = cli;
  ref_create(&(cnn->r));
  ref_on_release(&(cnn->r),cnn_on_release,cnn);
  ref_on_free(&(cnn->r),cnn_on_free,cnn);
  cnn->timer = event_new(cli->eb,-1,EV_PERSIST,tidy,cnn);
  event_add(cnn->timer,&timer_period);
  return cnn;
}

void cnn_free(struct connections *cnn) {
  ref_release(&(cnn->r));
}

void cnn_stats(struct connections *cnn,int64_t *n_new,int64_t *dns_time) {
  if(n_new) { *n_new = cnn->n_new; }
  if(dns_time) { *dns_time = cnn->dns_time; }
}

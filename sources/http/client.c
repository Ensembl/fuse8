#include <stdio.h>
#include <string.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/util.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "client.h"
#include "connection.h"
#include "../../util/misc.h"
#include "../../util/logging.h"
#include "../../util/dns.h"

CONFIG_LOGGING(http);

struct http_request {
  struct httpclient *cli;
  char *uris;
  char *host;
  struct evhttp_uri *uri;
  int port,retries;
  /* stats */
  struct http_stats stats;
  int64_t dns_start;
  /**/
  char *out;
  int64_t offset,len;
  http_fn callback;
  void *priv;
  struct connection *conn;
};

static int try(struct http_request *rq);

static void free_rq(struct http_request *rq) {
  rq->retries = -1; /* stop attempts to resurrect during destroy */
  free(rq->out);
  if(rq->uris) { free(rq->uris); rq->uris = 0; }
  if(rq->host) { free(rq->host); rq->host = 0; }
  if(rq->uri) { evhttp_uri_free(rq->uri); rq->uri = 0; }
  if(rq->conn) { unget_connection(rq->conn,0); rq->conn = 0; }
  free(rq);
}

static void error(struct http_request *rq,char *msg) {
  msg = strdup(msg);
  log_warn(("http error: '%s'",msg));
  if(rq->conn) { unget_connection(rq->conn,1); rq->conn = 0; }
  if(try(rq)) {
    log_warn(("too many errors, failing request"));
    rq->callback(0,msg,rq->len,0,rq->priv,&(rq->stats));
    free_rq(rq);
  }
  free(msg);
}

static int parse_range(const char *range,
                       int64_t *from,int64_t *to,int64_t *len) {
  char *q,*r;

  if(strncmp(range,"bytes ",6)) { return 1; }
  *from = strtoull(range+6,&q,10);
  if(*q!='-') { return 1; }
  *to = strtoull(q+1,&r,10);
  if(*r!='/') { return 1; }
  *len = strtoull(r+1,&q,10);
  if(*q) { return 1; }
  return 0;
}

// XXX timeouts
static void done(struct evhttp_request *req,void *priv) {
  struct evkeyvalq *headers;
  struct http_request *rq;
  int nread,offset,amt,code;
  const char *range;
  int64_t from,to,len;
  int eof;

  rq = (struct http_request *)priv;
  if(!req || !(rand()%10)) {
    error(rq,"Request failed");
    return; 
  }
  code = evhttp_request_get_response_code(req);
  if(code<200 || code>299) {
    error(rq,"Bad status"); // XXX codes
    return;
  }
  if(code!=206) {
    error(rq,"Server does not support range requests");
    return;
  }
  headers = evhttp_request_get_input_headers(req); 
  if(!headers) {
    error(rq,"Cannot retrieve headers");
    return;
  }
  range = evhttp_find_header(headers,"Content-Range");
  if(!range) {
    error(rq,"Content range header missing");
    return;
  }
  if(parse_range(range,&from,&to,&len)) {
    error(rq,"Content range header invalid");
    return;
  }
  eof = (len == to+1);
  if(from != rq->offset || (!eof && to != rq->offset+rq->len-1)) {
    error(rq,"Unexpected range returned");
    return;
  }
  log_debug(("EOF? = %d",eof));
  offset = 0;
  amt = rq->len;
  while(amt) {
    nread = evbuffer_remove(evhttp_request_get_input_buffer(req),
                            rq->out+offset,amt);
    if(nread <= 0) { break; }
    offset += nread;
    amt -= nread;
  }
  if(amt && !eof) {
    error(rq,"Short buffer");
    return;
  }
  rq->callback(1,rq->out,rq->len-amt,eof,rq->priv,&(rq->stats));
  free_rq(rq);
}

static void make_request(struct connection *conn,void *priv) {
  struct http_request *rq = (struct http_request *)priv;
  char *range;
  struct evkeyvalq *reqh;
  struct evhttp_request *req;
  int r;

  if(rq->retries==-1) { return; } /* In middle of free! */
  if(!conn) {
    error(rq,"Could not create connection");
    return;
  }
  rq->conn = conn;
  // XXX non-blocking DNS / cache
  req = evhttp_request_new(done,rq);
  if(!req) {
    error(rq,"Could not create request");
    return;
  }

  reqh = evhttp_request_get_output_headers(req);
  evhttp_add_header(reqh,"Host", rq->host);
  range = make_string("bytes=%llu-%llu",
		(unsigned long long)rq->offset,
	        (unsigned long long)(rq->offset+rq->len-1));
  evhttp_add_header(reqh,"Range",range);
  free(range);
  r = evhttp_make_request(evconnection(conn),req,EVHTTP_REQ_GET,rq->uris);
  if(r) {
    error(rq,"Request failed");
    return;
  }
}

struct httpclient * httpclient_create(struct event_base *eb,
                                      struct evdns_base *edb) {
  struct httpclient *cli;

  cli = safe_malloc(sizeof(struct httpclient));
  cli->eb = eb;
  cli->edb = edb;
  cli->cnn = cnn_make(cli);
  return cli;
}

static void cnn_finished(void *priv) {
  struct httpclient *cli = (struct httpclient *)priv;
  http_finished f_cb;
  void *f_priv;

  f_cb = cli->f_cb;
  f_priv = cli->f_priv;
  log_debug(("connections finished: closing"));
  free(cli);
  f_cb(f_priv);
}

void httpclient_finish(struct httpclient *cli,http_finished cb,void *priv) {
  log_debug(("finish called: waiting for connections"));
  cli->f_cb = cb;
  cli->f_priv = priv;
  cnn_free(cli->cnn,cnn_finished,cli);
}

#define MAX_RETRIES 10
static int try(struct http_request *rq) {
  if(rq->retries > MAX_RETRIES || rq->retries==-1) { return -1; }
  rq->retries++;
  rq->dns_start = microtime();
  get_connection(rq->cli->cnn,rq->host,rq->port,make_request,rq);
  return 0;
}

// XXX tidy up
void http_request(struct httpclient *cli,char *uris,size_t off,size_t size,
                  http_fn callback,void *priv) {
  struct http_request *rq;
  const char *host;

  rq = safe_malloc(sizeof(struct http_request));
  rq->out = safe_malloc(size);
  rq->offset = off;
  rq->len = size;
  rq->callback = callback;
  rq->priv = priv;
  rq->cli = cli;
  rq->conn = 0;
  rq->uri = 0;

  // XXX fail not only noent
  // XXX informative errors
  rq->uris = strdup(uris);
  rq->uri = evhttp_uri_parse(uris);
  if(!rq->uri) {
    error(rq,"Invalid URI");
    return;
  }
  host = evhttp_uri_get_host(rq->uri);
  if (!host) {
    error(rq,"Bad/missing host in URI");
    return;
  }
  rq->host = strdup(host);
  rq->port = evhttp_uri_get_port(rq->uri);
  rq->stats = (struct http_stats){ .dns_time = 0 };
  rq->retries = 0;
  if(rq->port==-1) { rq->port=80; }
  try(rq);  
}


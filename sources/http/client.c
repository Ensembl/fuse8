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
  /* used between dns and main request */
  char *uris;
  char *host;
  struct evhttp_uri *uri;
  int port;
  /* stats */
  struct http_stats stats;
  int64_t dns_start;
  /**/
  int success;
  char *out;
  int64_t offset,len;
  http_fn callback;
  void *priv;
  struct connection *conn;
};

static void error(struct http_request *rq,char *msg) {
  rq->success = 0;
  free(rq->out);
  rq->out = strdup(msg);
  log_warn(("http error: '%s'",msg));
  rq->callback(rq->success,rq->out,rq->len,0,rq->priv,&(rq->stats));
  free(rq->out);
  if(rq->uris) { free(rq->uris); }
  if(rq->host) { free(rq->host); }
  if(rq->uri) { evhttp_uri_free(rq->uri); rq->uri = 0; }
  free(rq);
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
  if(!req) {
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
  rq->callback(rq->success,rq->out,rq->len-amt,eof,rq->priv,&(rq->stats));
  free(rq->out);
  unget_connection(rq->conn);
  free(rq);
}

static void make_request(struct connection *conn,void *priv) {
  struct http_request *rq = (struct http_request *)priv;
  char *range;
  struct evkeyvalq *reqh;
  struct evhttp_request *req;
  int r;

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
  evhttp_uri_free(rq->uri);
  rq->uri = 0;
  r = evhttp_make_request(evconnection(conn),req,EVHTTP_REQ_GET,rq->uris);
  if(r) {
    error(rq,"Request failed");
    return;
  }
  free(rq->uris);
  free(rq->host);
  rq->uris = 0;
  rq->host = 0;
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

void httpclient_finish(struct httpclient *cli) {
  cnn_free(cli->cnn);
  free(cli);
}

// XXX tidy up
void http_request(struct httpclient *cli,
                  char *uris,size_t off,size_t size,
                  http_fn callback,void *priv) {
  struct http_request *rq;
  const char *host;

  rq = safe_malloc(sizeof(struct http_request));
  rq->success = 1;
  rq->out = safe_malloc(size);
  rq->offset = off;
  rq->len = size;
  rq->callback = callback;
  rq->priv = priv;
  rq->cli = cli;
  rq->conn = 0;

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
  if(rq->port==-1) { rq->port=80; }
  rq->dns_start = microtime();
  get_connection(rq->cli->cnn,rq->host,rq->port,make_request,rq);
}

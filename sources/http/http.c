#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

#include "http.h"
#include "client.h"
#include "../../request.h"
#include "../../util/misc.h"
#include "../../util/logging.h"
#include "../../source.h"

#define PREFIX "http://"

CONFIG_LOGGING(http)

#define HTTPBLOCKSIZE 65536

struct http {
  struct httpclient *cli;

  /* stats */
  int64_t dns_time;
};

struct httpwholereq {
  struct source *ds;
  int count,failed_errno;
};

struct httpreq {
  struct request *rq;
  struct httpwholereq *wr;
  int64_t offset,length;
};

static struct http * http_open(struct event_base *base,
                               struct evdns_base *dbase) {
  struct http *out;
  
  out = safe_malloc(sizeof(struct http));
  out->cli = httpclient_create(base,dbase);
  out->dns_time = 0;
  return out;
}

static void read_done(int success,char *data,int64_t len,int eof,
                      void *priv,struct http_stats *stats) {
  struct httpreq *hr = (struct httpreq *)priv;
  struct http *ht;
  struct request *rq;
  struct chunk *ck;
  struct httpwholereq *wr;
  int failed_errno;

  rq = hr->rq;
  wr = hr->wr;
  ht = (struct http *)(wr->ds->priv);
  ht->dns_time += stats->dns_time;
  log_debug(("got http result"));
  if(success) {
    log_debug(("got http success"));
    ck = rq_chunk(wr->ds,data,hr->offset,len,eof,0);
    rq_found_data(rq,ck); 
  } else {
    // XXX better errors for logging/stats
    log_debug(("got http failure"));
    wr->failed_errno = EIO;
  }
  rq_release(rq);
  free(hr);
  if(!--wr->count) {
    log_debug(("all done"));
    failed_errno = wr->failed_errno;
    src_release(wr->ds);
    free(wr);
    if(failed_errno) {
      log_debug(("at least one subrequest failed errno=%d",failed_errno));
      rq_error(rq,failed_errno);
    } else {
      rq_run_next(rq); 
    }
  }
}

static void do_request(struct http *ht,struct httpwholereq *wr,
                       struct request *rq,
                       int64_t offset,int64_t length) {
  struct httpreq *hr;

  log_debug(("requesting %"PRId64"+%"PRId64,offset,length));
  hr = safe_malloc(sizeof(struct httpreq));
  hr->wr = wr;
  hr->rq = rq;
  hr->offset = offset;
  hr->length = length;
  rq_acquire(rq);
  http_request(ht->cli,rq->spec,offset,length,read_done,hr);
}

static void http_read(struct source *ds,struct request *rq) {
  struct http *ht = (struct http *)(ds->priv);
  struct httpwholereq *wr;
  struct ranges blocks;
  struct rangei ri;
  int64_t x,y;

  if(!strncmp(rq->spec,PREFIX,strlen(PREFIX))) {
    wr = safe_malloc(sizeof(struct httpwholereq));
    ranges_copy(&blocks,&(rq->desired));
    ranges_blockify_expand(&blocks,HTTPBLOCKSIZE);
    if(log_do_debug) {
      char *r1 = ranges_print(&(rq->desired));
      char *r2 = ranges_print(&blocks);
      log_debug(("desired: %s expanded: %s size=%d",r1,r2,HTTPBLOCKSIZE));
      free(r1);
      free(r2);
    }
    wr->ds = ds;
    src_acquire(ds);
    wr->count = ranges_num(&blocks);
    wr->failed_errno = 0;
    ranges_start(&blocks,&ri);
    while(ranges_next(&ri,&x,&y)) {
      do_request(ht,wr,rq,x,y-x);
    }
    ranges_free(&blocks);
  } else {
    rq_run_next(rq);
  } 
}

static void http_close_done(void *priv) {
  struct source *ds = (struct source *)priv;

  log_debug(("close done"));
  src_release(ds);
}

static void http_src_close(struct source *ds) {
  struct http *c = (struct http *)(ds->priv);

  src_acquire(ds);
  httpclient_finish(c->cli,http_close_done,ds);
  free(c);
}

static void cache_stats(struct source *src,struct jpf_value *out) {
  struct http *c = (struct http *)(src->priv);
  int64_t n_conns_new,dns_time;

  cnn_stats(c->cli->cnn,&n_conns_new,&dns_time);
  jpfv_assoc_add(out,"dns_secs",jpfv_number(dns_time/1000000));
  jpfv_assoc_add(out,"conns_total",jpfv_number_int(n_conns_new));
}

// XXX limit simul requests
struct source * source_http_make(struct running *rr,
                                 struct jpf_value *conf) {
  struct source *ds;

  ds = src_create();
  ds->priv = http_open(rr->eb,rr->edb);
  ds->read = http_read;
  ds->write = 0;
  ds->stats = cache_stats;
  ds->close = http_src_close;
  return ds;
}

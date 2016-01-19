#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include "util/misc.h"
#include "util/ranges.h"
#include "util/logging.h"

#include "request.h"
#include "sourcelist.h"
#include "interface.h"
#include "source.h"

CONFIG_LOGGING(request)

static void rq_clear_sl(struct request *rq) {
  if(rq->src) {
    log_debug(("releasing '%s'",rq->src->name));
    src_release(rq->src);
    rq->src = 0;
  }
}

static void rq_ref_release(void *data) {
  struct request *rq = (struct request *)data;

  log_debug(("request release"));
  rq_clear_sl(rq);
  sl_release(rq->sl);
}

static void rq_ref_free(void *data) {
  struct request *rq = (struct request *)data;

  log_debug(("request free"));
  ranges_free(&(rq->desired));
  if(rq->out) { free(rq->out); }
  free(rq->spec);
  free(rq);
}

struct request * rq_create(struct sourcelist *sl,
                           char *spec,int64_t offset,int64_t length,
                           req_fn done,void *priv) {
  struct request *rq;

  log_info(("creating request spec='%s' offset=%"PRId64"+%"PRId64,
           spec,offset,length));
  rq = safe_malloc(sizeof(struct request));
  ref_create(&(rq->r));
  ref_on_release(&(rq->r),rq_ref_release,rq);
  ref_on_free(&(rq->r),rq_ref_free,rq);
  rq->sl = sl;
  rq->spec = strdup(spec);
  rq->out = 0;
  rq->chunks = 0;
  rq->failed_errno = 0;
  rq->offset = offset;
  rq->length = length;
  rq->done = done;
  rq->priv = priv;
  rq->start = microtime();
  sl_acquire(sl);
  ranges_init(&(rq->desired));
  ranges_add(&(rq->desired),rq->offset,rq->offset+rq->length);
  return rq;
}

void rq_acquire(struct request *rq) { ref_acquire(&(rq->r)); }
void rq_release(struct request *rq) { ref_release(&(rq->r)); }

static void rq_reset_sl(struct request *rq) {
  struct source *next;

  next = sl_get_root(rq->sl);
  if(next) {
    log_debug(("acquiring '%s' in reset",next->name));
    src_acquire(next);
  }
  rq_clear_sl(rq);
  rq->src = next;
}

static void rq_advance_sl(struct request *rq) {
  struct source *next;

  next = src_get_next(rq->src);
  if(next) {
    log_debug(("acquiring '%s' in advance",next->name));
    src_acquire(next);
  }
  rq_clear_sl(rq);
  rq->src = next;
}

static void account_chunks(struct request *rq) {
  struct chunk *c;

  for(c=rq->chunks;c;c=c->next) {
    src_collect(c->origin,rq->length);
  }
}

static void collect_time(struct request *rq) {
  int64_t taken;

  taken = microtime() - rq->start;
  sl_stat_time(rq->sl,taken);
  log_debug(("Request took %"PRId64"ms\n",taken/1000));
}

// XXX eliminate tail-recursion ehere and elsewhere
void rq_run_next_write(struct request *rq) {
  struct chunk *c;

  if(!rq->chunks) {
    log_debug(("writing done"));
    rq_clear_sl(rq); 
    collect_time(rq);
    rq_release(rq);
    return;
  }
  if(!rq->src) {
    rq_reset_sl(rq);
  } else {
    if(rq->p_start) {
      if(rq->src->write) {
        src_collect_wtime(rq->src,microtime()-rq->p_start);
      }
      rq->p_start = 0;
    }
    rq_advance_sl(rq);
  }
  if(rq->src) {
    if(rq->chunks->origin == rq->src) {
      log_debug(("reached self so finishing write of this chunk"));
    } else if(rq->src->write) {
      c = rq->chunks;
      log_debug(("running write2 in '%s' on chunk %"PRId64"+%"PRId64,
                 rq->src->name,c->offset,c->length));
      rq->p_start = microtime();
      rq->src->write(rq->src,rq,c);
      return;
    } else {
      log_debug(("source cannot write2"));
      rq_run_next_write(rq);
      return;
    }
  }
  rq_clear_sl(rq);
  c = rq->chunks->next;
  src_release(rq->chunks->origin);
  free(rq->chunks->out);
  free(rq->chunks);
  rq->chunks = c;
  log_debug(("advance chunk"));
  rq_run_next_write(rq);
}

static void rq_run_writes(struct request *rq) {
  rq_clear_sl(rq);
  rq->p_start = 0;
  rq_run_next_write(rq);
}

void rq_run_next(struct request *rq) {
  char *c;
 
  if(rq->failed_errno) {
    src_set_failed(rq->src,rq->spec);
    if(rq->src) { src_collect_error(rq->src); }
    log_debug(("sending error errno=%d",rq->failed_errno));
    rq->done(rq->failed_errno,rq->out,rq->priv);
    rq_clear_sl(rq);
    collect_time(rq);
    rq_release(rq);
    return;
  }
  if(log_do_debug) {
    c = ranges_print(&(rq->desired));
    log_debug(("desired: %s",c));
    free(c);
  }
  // XXX short circuit when fulfilled?
  if(ranges_empty(&(rq->desired))) {
    log_debug(("reads fulfilled errno=%d",rq->failed_errno));
    if(rq->src) {
      log_info(("satisfied by '%s'",rq->src->name));
      sl_record_hit(rq->sl,rq->spec,rq->src->name,rq->length);
    }
    rq->done(rq->failed_errno,rq->out,rq->priv);
    account_chunks(rq);
    rq_run_writes(rq);
    return;
  }
  if(!rq->src) {
    rq_reset_sl(rq);
  } else {
    if(rq->p_start) {
      src_collect_rtime(rq->src,microtime()-rq->p_start);
      rq->p_start = 0;
    }
    rq_advance_sl(rq);
  }
  if(rq->src) {
    /* More to do */
    if(rq->src->read && src_path_ok(rq->src,rq->spec)) {
      log_debug(("running read2 on next source"));
      rq->p_start = microtime();
      rq->src->read(rq->src,rq);
    } else {
      log_debug(("next source cannot read2"));
      rq_run_next(rq);
    }
  } else {
    log_debug(("read2 failed"));
    // XXX do something sensible
    // XXX free chunks
    account_chunks(rq);
    rq->done(rq->failed_errno||EIO,0,rq->priv);
    rq_clear_sl(rq);
    collect_time(rq);
    rq_release(rq);
  }
}

void rq_run(struct request *rq) {

  rq->out = safe_malloc(rq->length);
  rq->src = 0;
  if(!rq->length) {
    rq->done(rq->failed_errno,0,rq->priv);
    collect_time(rq);
    return;
  }
  rq->p_start = 0;
  rq_acquire(rq);
  rq_run_next(rq);
}

struct chunk * rq_chunk(struct source *sc,char *data,
                        int64_t offset,int64_t length,int eof,
                        struct chunk *next) {
  struct chunk *c;

  c = safe_malloc(sizeof(struct chunk));
  c->out = safe_malloc(length);
  memcpy(c->out,data,length);
  c->offset = offset;
  c->length = length;
  c->eof = eof;
  c->origin = sc;
  c->next = next;
  return c;
}

void rq_found_data(struct request *rq,struct chunk *c) {
  struct chunk *d;
  struct ranges r;
  struct rangei ri;
  int64_t x,y;

  while(c) {
    log_debug(("processing report of data at %"PRId64"+%"PRId64,
              c->offset,c->length));
    /* Help satisfy request */
    ranges_init(&r);
    ranges_add(&r,c->offset,c->offset+c->length);
    ranges_remove(&r,0,rq->offset);
    ranges_remove(&r,rq->offset+rq->length,INT64_MAX);
    ranges_start(&r,&ri);
    while(ranges_next(&ri,&x,&y)) {
      log_debug(("copying range %"PRId64"-%"PRId64,x,y));
      memcpy(rq->out+x-rq->offset,c->out+x-c->offset,y-x);
    }
    ranges_free(&r);
    ranges_remove(&(rq->desired),c->offset,c->offset+c->length);
    /* Update desire given knoledge of eof */
    if(c->eof) {
      log_debug(("early eof: rest of file does not exist"));
      ranges_remove(&(rq->desired),c->offset+c->length,INT64_MAX);
    }
    /* Move chunks to request (for later write) */
    src_acquire(c->origin);
    d = c->next;
    c->next = rq->chunks;
    rq->chunks = c;
    c = d;
  }
}

void rq_error(struct request *rq,int failed_errno) {
  rq->failed_errno = failed_errno;
  rq_run_next(rq);
}


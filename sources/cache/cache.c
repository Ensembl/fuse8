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
#include <event2/event.h>

#include "cache.h"

#include "../../running.h"
#include "../../util/misc.h"
#include "../../util/hash.h"
#include "../../util/ranges.h"
#include "../../util/logging.h"
#include "../../request.h"
#include "../../jpf/jpf.h"

// XXX del cachefile on exit

// XXX start time to stats
#define HEADERSIZE(c) ((c)->entries*sizeof(struct header))
#define BODYSIZE(c)   ((c)->entries*(c)->block_size)
#define FILESIZE(c) (HEADERSIZE(c)+BODYSIZE(c))
#define MUTEXREGIONS 16
#define OFFSET(c,slot) (HEADERSIZE(c)+(slot*(c)->block_size))
#define MUTEX_FOR(x,c) ((x)*MUTEXREGIONS/c->entries)

CONFIG_LOGGING(cache);

/* Set target by alpha-smoothing given input xn, sum of n samples */
#define ALPHASMOOTH 10
static void alpha(int64_t *v,int64_t x,int64_t n) {
  if(n>ALPHASMOOTH) {
    *v = x/n;
  } else if(n) {
    *v = (x+(ALPHASMOOTH-n)*(*v))/ALPHASMOOTH;
  }
}

static void timer_tick(evutil_socket_t fd, short what, void *arg) {
  struct cache *c = (struct cache *)arg;

  log_debug(("stats timer called"));
  alpha(&(c->lifespan),c->cur_lifespan,c->n_lifespan);
  alpha(&(c->hit_rate),c->hits*100,c->hits+c->misses);
  log_debug(("current lifespan %"PRId64"us",c->lifespan));
  log_debug(("current hitrate %"PRId64"%%",c->hit_rate));
}

static void cache_stats(struct source *src,struct jpf_value *out) {
  struct cache *c = (struct cache *)(src->priv);

  jpfv_assoc_add(out,"lifespan_sec",
                 jpfv_number(((float)c->lifespan)/1000000));
  jpfv_assoc_add(out,"hitrate_perc",jpfv_number(c->hit_rate));
  jpfv_assoc_add(out,"locked_out_num",jpfv_number_int(c->locked_out));
  jpfv_assoc_add(out,"lockheld_sec",
                 jpfv_number(((float)c->lock_time)/1000000));
  if(c->ops->stats)
    c->ops->stats(c,out,c->priv);
}

static struct cache * cache_open(struct event_base *eb,
                                 struct jpf_value *conf,
                                 struct cache_ops *ops,void *priv) {
  struct cache *c;
  struct timeval one_min = { 60, 0 }; // XXX conf
  struct jpf_value *path;
  int64_t block,entries,set;
  
  path = jpfv_lookup(conf,"filename");
  if(!path) { die("No path to cachefile specified"); }
  if(jpfv_int64(jpfv_lookup(conf,"block"),&block) ||
     jpfv_int64(jpfv_lookup(conf,"entries"),&entries) ||
     jpfv_int64(jpfv_lookup(conf,"set_size"),&set)) {
    die("Bad config"); // XXX do it properly when we have schema
  }
  c = safe_malloc(sizeof(struct cache));
  c->block_size = block;
  c->entries = entries;
  c->set_size = set;
  c->ones = safe_malloc(HASHSIZE);
  c->zeros = safe_malloc(HASHSIZE);
  memset(c->ones,255,HASHSIZE);
  memset(c->zeros,0,HASHSIZE);
  /* Stats */
  c->lifespan = 0;
  c->cur_lifespan = 0;
  c->n_lifespan = 0;
  c->hits = c->misses = c->hit_rate = 0;
  c->locked_out = 0;
  c->lock_time = 0;
  /* Timer */
  c->timer = event_new(eb,-1,EV_PERSIST,timer_tick,c);
  event_add(c->timer,&one_min);
  /* Subtype */
  c->ops = ops;
  c->priv = priv;
  c->ops->open(c,conf,c->priv);
  return c;
}

static void cache_close(struct cache *c) {
  c->ops->close(c,c->priv);
  free(c->ones);
  free(c->zeros);
  event_del(c->timer);
  event_free(c->timer);
  free(c);
}

static struct hash * cache_hash(struct request *rq,int64_t bk) {
  char *key;
  struct hash *h;
  
  log_debug(("Request offset='%"PRId64"'",bk));
  key = make_string("%"PRId64":%s",bk,rq->spec);
  h = make_hash(key);
  free(key);
  return h;
}

static int cache_lock(struct cache *c,int slot) {
  struct header *h;
  int ok;

  if(c->ops->lock(c,slot,c->priv)) { c->locked_out++; return 0; }
  c->lock_start = microtime();
  c->ops->get_header(&h,c,slot,c->priv);
  ok = memcmp(h->hash,c->ones,HASHSIZE);
  if(ok) {
    if(!memcmp(h->hash,c->zeros,HASHSIZE)) {
      log_debug(("slot was empty"));
    } else {
      log_debug(("slot was used age=%"PRId64,microtime()-h->created));
      c->cur_lifespan += microtime()-h->created;
      c->n_lifespan++;
    }
    memset(h->hash,255,HASHSIZE);
    h->created = microtime();
    c->ops->set_header(c,h,slot,c->priv);
  }
  c->ops->header_done(c,h,slot,c->priv);
  if(!ok) {
    c->lock_time += microtime() - c->lock_start;
    c->ops->unlock(c,slot,c->priv);
  }
  return ok; 
}

static int cache_check_lock(struct cache *c,int slot,struct hash *hh) {
  struct header *h;
  int found;

  if(c->ops->lock(c,slot,c->priv)) { c->locked_out++; return 0; }
  c->lock_start = microtime();
  c->ops->get_header(&h,c,slot,c->priv);
  found = !hash_cmp(hh,h->hash,HASHSIZE);
  if(found) {
    memset(h->hash,255,HASHSIZE);
    c->ops->set_header(c,h,slot,c->priv);
  }
  c->ops->header_done(c,h,slot,c->priv);
  if(!found) {
    c->lock_time += microtime() - c->lock_start;
    c->ops->unlock(c,slot,c->priv);
  }
  return found;
}

static void cache_unlock(struct cache *c,int slot,struct hash *hh) {
  struct header *h;

  c->ops->get_header(&h,c,slot,c->priv); 
  memcpy(h->hash,hash_data(hh),hash_len(hh));
  c->ops->set_header(c,h,slot,c->priv);
  c->ops->header_done(c,h,slot,c->priv);
  c->ops->unlock(c,slot,c->priv);
  c->lock_time += microtime() - c->lock_start;
}

// XXX all writes to async
static void write_block(struct cache *c,struct request *rq,
                        char *data,int64_t block) {
  struct hash *h;
  uint64_t slot;

  h = cache_hash(rq,block);
  slot = (hash_mod(h,c->entries) + (rand()%c->set_size)) %c->entries;
  log_debug(("writing block at %"PRId64" (%"PRId64")",block,slot));
  if(cache_lock(c,slot)) {
    c->ops->write_data(c,slot,data,c->priv);
    cache_unlock(c,slot,h);
  }
  free_hash(h);
}

static void ds_write(struct source *ds,struct request *rq,struct chunk *ck) {
  struct cache *c = (struct cache *)(ds->priv);
  struct ranges blocks;
  struct rangei ri;
  int64_t x,y,bk,tail;
  char *taildata;

  log_debug(("writing chunk at %"PRId64"+%"PRId64,ck->offset,ck->length));
  ranges_init(&blocks);
  ranges_add(&blocks,ck->offset,ck->offset+ck->length);
  ranges_blockify_reduce(&blocks,c->block_size);
  ranges_start(&blocks,&ri);
  while(ranges_next(&ri,&x,&y)) {
    for(bk=x/c->block_size;bk<y/c->block_size;bk++) {
      write_block(c,rq,ck->out+bk*c->block_size-ck->offset,bk*c->block_size);
    }
  }
  tail = (ck->offset+ck->length)%c->block_size;
  if(ck->eof && tail) {
    /* One last block */
    // XXX prove safe
    bk = (ck->offset+ck->length)/c->block_size;
    taildata = safe_malloc(c->block_size);
    memcpy(taildata,ck->out+bk*c->block_size-ck->offset,tail);
    memset(taildata+tail,0,c->block_size-tail);
    write_block(c,rq,taildata,bk*c->block_size);
    free(taildata);
  }
  ranges_free(&blocks);
  rq_run_next_write(rq);
}

static void read_block(struct source *ds,struct request *rq,int64_t bk) {
  struct cache *c = (struct cache *)(ds->priv);
  struct hash *h;
  struct chunk *ck;
  uint64_t slot,i;
  char *data;

  h = cache_hash(rq,bk);
  slot = hash_mod(h,c->entries);
  log_debug(("considering block at %"PRId64" (%"PRId64")",bk,slot));
  for(i=0;i<c->set_size;i++) {
    if(cache_check_lock(c,slot,h)) {
      c->ops->read_data(c,slot,&data,c->priv);
      ck = rq_chunk(ds,data,bk,c->block_size,0,0);
      rq_found_data(rq,ck);
      c->ops->read_done(data,c->priv);
      cache_unlock(c,slot,h);
      free_hash(h);
      log_debug(("found in cache"));
      c->hits++;
      return; 
    }
    slot++;
    slot %= c->entries;
  }
  free_hash(h);
  log_debug(("not found in cache"));
  c->misses++;
}

static void ds_read(struct source *ds,struct request *rq) {
  struct cache *c = (struct cache *)(ds->priv);
  struct ranges blocks;
  struct rangei ri;
  int64_t x,y,bk;

  log_debug(("read spec='%s'",rq->spec));
  ranges_blockify_expand(&(rq->desired),c->block_size);
  ranges_copy(&blocks,&(rq->desired)); /* Modified during iter = bad */
  ranges_start(&blocks,&ri);
  while(ranges_next(&ri,&x,&y)) {
    log_debug(("Considering range %"PRId64"-%"PRId64,x,y));
    for(bk=x/c->block_size;bk<y/c->block_size;bk++) {
      read_block(ds,rq,bk*c->block_size);
    }
  }
  ranges_free(&blocks);
  rq_run_next(rq);
}

static void ds_close(struct source *ds) {
  cache_close((struct cache *)ds->priv);
}

struct source * source_cache_make(struct running *rr,
                                  struct jpf_value *conf,
                                  struct cache_ops *ops,void *priv) {
  struct source *ds;
  struct cache *c;

  c = cache_open(rr->eb,conf,ops,priv);
  ds = src_create();
  ds->priv = c;
  ds->read = ds_read;
  ds->write = ds_write;
  ds->close = ds_close;
  ds->stats = cache_stats;
  return ds;
}

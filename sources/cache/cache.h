#ifndef SOURCES_CACHE_H
#define SOURCES_CACHE_H

#define HASHSIZE 64

#include "../../util/hash.h"
#include "../../jpf/jpf.h"
#include "../../running.h"

struct header {
  char hash[HASHSIZE];
  int64_t created;
};

struct cache {
  struct cache_ops *ops;
  void *priv;
  struct event *timer,*dequeue_timer,*fast_timer;
  void *ones,*zeros;

  /* config */
  int64_t block_size,entries,set_size;

  /* stats */
  int64_t lifespan,cur_lifespan,n_lifespan,hits,misses,hit_rate;
};

struct cache_ops {
  void (*open)(struct cache *c,struct jpf_value *conf,void *priv);
  void (*close)(struct cache *c,void *priv);
  void (*get_header)(struct header **h,struct cache *c,int slot,void *priv);
  void (*set_header)(struct cache *c,struct header *h,int slot,void *priv);
  void (*header_done)(struct cache *c,struct header *h,int slot,void *priv);
  void (*read_data)(struct cache *c,int slot,char **data,void *priv);
  void (*write_data)(struct cache *c,int slot,char *data,void *priv);
  void (*read_done)(char *data,void *priv);
  void (*stats)(struct cache *c,struct jpf_value *out,void *priv);
  void (*queue_write)(struct cache *c,struct hash *h,char *data,void *priv);
  int (*dequeue_prepare)(struct cache *c,void *priv);
  int (*dequeue_go)(struct cache *c,void *priv);
};

void cache_queue_write(struct cache *c,struct hash *h,char *data,void *priv);

struct source * source_cache_make(struct running *rr,
                                  struct jpf_value *conf,
                                  struct cache_ops *ops,void *priv);

#endif

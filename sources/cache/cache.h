#ifndef SOURCES_CACHE_H
#define SOURCES_CACHE_H

#define HASHSIZE 64

#include "../../jpf/jpf.h"
#include "../../running.h"

struct header {
  char hash[HASHSIZE];
  int64_t created;
};

struct cache {
  struct cache_ops *ops;
  void *priv;
  struct event *timer;
  void *ones,*zeros;

  /* config */
  int64_t block_size,entries,set_size;

  /* stats */
  int64_t lifespan,cur_lifespan,n_lifespan,hits,misses,hit_rate,locked_out;
  int64_t lock_start,lock_time;
};

struct cache_ops {
  void (*open)(struct cache *c,struct jpf_value *conf,void *priv);
  void (*close)(struct cache *c,void *priv);
  int (*lock)(struct cache *c,int slot,void *priv);
  void (*unlock)(struct cache *c,int slot,void *priv);
  void (*get_header)(struct header **h,struct cache *c,int slot,void *priv);
  void (*set_header)(struct cache *c,struct header *h,int slot,void *priv);
  void (*header_done)(struct cache *c,struct header *h,int slot,void *priv);
  void (*read_data)(struct cache *c,int slot,char **data,void *priv);
  void (*write_data)(struct cache *c,int slot,char *data,void *priv);
  void (*read_done)(char *data,void *priv);
  void (*stats)(struct cache *c,struct jpf_value *out,void *priv);
};

struct source * source_cache_make(struct running *rr,
                                  struct jpf_value *conf,
                                  struct cache_ops *ops,void *priv);

#endif

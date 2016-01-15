#include <stdio.h>
#include <inttypes.h>
#include "util/misc.h"
#include "util/logging.h"

#include "types.h"
#include "source.h"
#include "request.h"
#include "hits.h"

CONFIG_LOGGING(sourcelist)

static void sl_ref_release(void *data) {
  struct sourcelist *sl = (struct sourcelist *)data;
  struct source *src,*srcn;

  log_debug(("sourcelist release"));
  for(src=sl->root;src;src=srcn) {
    srcn = src->next;
    src_release(src);
  }
}

static void sl_ref_free(void *data) {
  struct sourcelist *sl = (struct sourcelist *)data;

  log_debug(("sourcelist free"));
  if(sl->hits) { hits_free(sl->hits); }
  free(sl);
}

struct sourcelist * sl_create(void) {
  struct sourcelist *sl;

  sl = safe_malloc(sizeof(struct sourcelist));
  sl->root = 0;
  sl->hits = 0;
  sl->bytes = sl->n_hits = sl->time = 0;
  ref_create(&(sl->r));
  ref_on_release(&(sl->r),sl_ref_release,sl);
  ref_on_free(&(sl->r),sl_ref_free,sl);
  return sl;
}

void sl_acquire(struct sourcelist *sl) { ref_acquire(&(sl->r)); }
void sl_release(struct sourcelist *sl) { ref_release(&(sl->r)); }
struct ref * sl_ref(struct sourcelist *sl) { return &(sl->r); }
void sl_acquire_weak(struct sourcelist *sl) { ref_acquire_weak(&(sl->r)); }
void sl_release_weak(struct sourcelist *sl) { ref_release_weak(&(sl->r)); }

struct source * sl_get_root(struct sourcelist *sl) {
  return sl->root;
}

void sl_add_src(struct sourcelist *sl,struct source *src) {
  struct source **last;

  src_acquire(src);
  src->sl = sl;
  sl_acquire_weak(sl);
  for(last=&(sl->root);*last;last=&((*last)->next))
    ;
  *last = src;
  src->prev = last;
  src->next = 0;
}

// XXX inode sharing
// XXX lock re modification
// XXX stat rest

void sl_stat_time(struct sourcelist *sl,int64_t rtime) {
  sl->time += rtime;
  log_debug(("requests num=%"PRId64" bytes=%"PRId64" time=%"PRId64"us",
            sl->n_hits,sl->bytes,sl->time));
}

void sl_read(struct sourcelist *sl,char *spec,int64_t offset,
             int64_t length,req_fn done,void *priv) {
  struct request *rq;

  rq = rq_create(sl,spec,offset,length,done,priv);
  sl->n_hits++;
  sl->bytes += length;
  rq_run(rq);
  rq_release(rq);
}

int sl_stat(struct sourcelist *sl,int inode,struct fuse_stat *fs) {
  struct source *src;

  for(src=sl->root;src;src=src->next) {
    if(src->stat && !src->stat(src,inode,fs)) { return 0; }
  }
  return 1;
}

int sl_lookup(struct sourcelist *sl,int inode,
              const char *name,struct fuse_stat *fs) {
  struct source *src;

  for(src=sl->root;src;src=src->next) {
    if(src->lookup && !src->lookup(src,inode,name,fs)) { return 0; }
  }
  return 1;
}

int sl_readdir(struct sourcelist *sl,int inode,int **members) {
  struct source *src;

  for(src=sl->root;src;src=src->next) {
    if(src->readdir && !src->readdir(src,inode,members)) { return 0; }
  }
  return 1;
}

int sl_readlink(struct sourcelist *sl,int inode,char **out) {
  struct source *src;

  for(src=sl->root;src;src=src->next) {
    if(src->readlink && !src->readlink(src,inode,out)) { return 0; }
  }
  return 1;
}

void sl_set_hits(struct sourcelist *sl,struct hits *hits) {
  sl->hits = hits;
}

void sl_record_hit(struct sourcelist *sl,char *uri,char *source,
                   int64_t bytes) {
  if(!sl->hits) { return; }
  hit_add(sl->hits,uri,source,bytes);
}

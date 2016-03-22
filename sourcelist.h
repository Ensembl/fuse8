#ifndef SOURCELIST_H
#define SOURCELIST_H

#include "types.h"
#include "source.h"
#include "running.h"

struct sourcelist * sl_create(void);
void sl_release(struct sourcelist *sl);
void sl_acquire(struct sourcelist *sl);
void sl_open(struct sourcelist *sl);
void sl_add_src(struct sourcelist *sl,struct source *src);
struct ref * sl_ref(struct sourcelist *sl);

void sl_read(struct sourcelist *sl,char *spec,
             int64_t version,int64_t offset,int64_t length,
             req_fn done,void *priv);
int sl_stat(struct sourcelist *sl,int inode,struct fuse_stat *fs);
int sl_lookup(struct sourcelist *sl,int inode,
              const char *name,struct fuse_stat *fs);
int sl_readdir(struct sourcelist *sl,int inode,int **members);
int sl_readlink(struct sourcelist *sl,int inode,char **out);
struct source * sl_find(struct sourcelist *sl,char *name);

/* Internal use */
void sl_acquire_weak(struct sourcelist *sl);
void sl_release_weak(struct sourcelist *sl);
struct source * sl_get_root(struct sourcelist *sl);
void sl_stat_time(struct sourcelist *sl,int64_t rtime);
void sl_set_hits(struct sourcelist *sl,struct hits *hits);
void sl_record_hit(struct sourcelist *sl,char *uri,char *source,
                   int64_t bytes);

#endif

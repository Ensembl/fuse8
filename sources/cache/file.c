#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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
#include "file.h"

// XXX start time to stats
#define HEADERSIZE(c) ((c)->entries*sizeof(struct header))
#define BODYSIZE(c)   ((c)->entries*(c)->block_size)
#define FILESIZE(c) (HEADERSIZE(c)+BODYSIZE(c))
#define OFFSET(c,slot) (HEADERSIZE(c)+(slot*(c)->block_size))

CONFIG_LOGGING(cachefile);

struct cache_file {
  char *lock;
  int fd;

  /* lock breaking */
  int64_t seen_when;
  char *seen;
};

// XXX properly marshalled header
// XXX slots not int
static void get_header(struct header **h,struct cache *c,int slot,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  // XXX handle errors
  *h = malloc(sizeof(struct header));
  lseek(cf->fd,slot*sizeof(struct header),SEEK_SET);
  read_all(cf->fd,*(char **)h,sizeof(struct header));
}

static void set_header(struct cache *c,struct header *h,int slot,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  // XXX handle errors
  lseek(cf->fd,slot*sizeof(struct header),SEEK_SET);
  write_all(cf->fd,(char *)h,sizeof(struct header));
}

static void header_done(struct cache *c,struct header *h,int slot,void *p) {
  free(h);
}

static void write_data(struct cache *c,int slot,char *data,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  // XXX handle errors
  lseek(cf->fd,OFFSET(c,slot),SEEK_SET);
  write_all(cf->fd,data,c->block_size);
}

static void read_data(struct cache *c,int slot,char **data,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  // XXX handle errors
  *data = safe_malloc(c->block_size);
  lseek(cf->fd,OFFSET(c,slot),SEEK_SET);
  read_all(cf->fd,*data,c->block_size);
}

static void read_done(char *data,void *priv) {
  free(data);
}

static void cf_open(struct cache *c,struct jpf_value *conf,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  struct jpf_value *path; 
  struct strbuf lockp;
  int flags;
 
  flags = 0;
  path = jpfv_lookup(conf,"filename");
  if(!path) { die("No path to cachefile specified"); }
  cf->fd = open(path->v.string,O_CREAT|O_RDWR|O_TRUNC|flags,0666);
  if(cf->fd<0) { die("Cannot create/open cache file"); }
  if(ftruncate(cf->fd,FILESIZE(c))<0) { die("Cannot extend cache file"); }
  strbuf_init(&lockp,0);
  strbuf_add(&lockp,"%s",path->v.string);
  strbuf_add(&lockp,"%s","-lock");
  cf->lock = strbuf_str(&lockp);
  log_debug(("Lock is '%s'",cf->lock));
  cf->seen = 0;
}

static void cf_close(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  if(close(cf->fd)<0) { die("Cannot close cache file"); }
  free(cf->lock);
  free(cf);
}

#define ANCIENT_LOCK 30000000
static int lock(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  char *contents;
  int r;
  int64_t now;

  r = lock_path(cf->lock);
  if(!r) {
    if(cf->seen) {
      free(cf->seen);
      cf->seen = 0;
    }
    return r;
  }
  now = microtime();
  if(!read_file(cf->lock,&contents)) {
    if(cf->seen && strcmp(cf->seen,contents)) {
      free(cf->seen);
      cf->seen = 0;
    }
    if(!cf->seen) {
      cf->seen = strdup(contents);
      cf->seen_when = now;
    }
    if(cf->seen_when+ANCIENT_LOCK<now) {
      log_warn(("ancient lock: busting it"));
      unlink(cf->lock);
      return lock_path(cf->lock);   
    }
  }
  return r;
}

static void unlock_done(void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  log_debug(("fsync done"));
  unlock_path(cf->lock);
}

static void unlock(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  log_debug(("fsync-ing"));
  fsync_async(cf->fd,unlock_done,cf);
}

static struct cache_ops ops = {
  .open = cf_open,
  .close = cf_close,
  .get_header = get_header,
  .set_header = set_header,
  .header_done = header_done,
  .read_data = read_data,
  .write_data = write_data,
  .read_done = read_done,
  .stats = 0,
  .lock = lock,
  .unlock = unlock,
};

struct source * source_cachefile2_make(struct running *rr,
                                       struct jpf_value *conf) {
  struct cache_file *c;
 
  c = safe_malloc(sizeof(struct cache_file));
  return source_cache_make(rr,conf,&ops,c);
}

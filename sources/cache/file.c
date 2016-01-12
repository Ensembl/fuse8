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
#include "file.h"

// XXX start time to stats
#define HEADERSIZE(c) ((c)->entries*sizeof(struct header))
#define BODYSIZE(c)   ((c)->entries*(c)->block_size)
#define FILESIZE(c) (HEADERSIZE(c)+BODYSIZE(c))
#define MUTEXREGIONS 16
#define OFFSET(c,slot) (HEADERSIZE(c)+(slot*(c)->block_size))
#define MUTEX_FOR(x,c) ((x)*MUTEXREGIONS/c->entries)

CONFIG_LOGGING(cachefile);

struct cache_file {
  char *lock;
  int fd;
  pthread_mutex_t mutexes[MUTEXREGIONS];
};

// XXX properly marshalled header
// XXX slots not int
static void get_header(struct header **h,struct cache *c,int slot,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  // XXX handle errors
  pthread_mutex_lock(cf->mutexes+MUTEX_FOR(slot,c));
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
  struct cache_file *cf = (struct cache_file *)p;

  free(h);
  pthread_mutex_unlock(cf->mutexes+MUTEX_FOR(slot,c));
}

// XXX timeout for lock

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

// XXX slotted locks
static int lock(struct cache *c,int slot,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  return lock_path(cf->lock);  
}

static void unlock(struct cache *c,int slot,void *p) {
  struct cache_file *cf = (struct cache_file *)p;

  unlock_path(cf->lock);
}

static void cf_open(struct cache *c,struct jpf_value *conf,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  struct jpf_value *path; 
  struct strbuf lockp;
  int i;
 
  path = jpfv_lookup(conf,"filename");
  if(!path) { die("No path to cachefile specified"); }
  for(i=0;i<MUTEXREGIONS;i++) {
    pthread_mutex_init(cf->mutexes+i,0);
  }
  cf->fd = open(path->v.string,O_CREAT|O_RDWR|O_TRUNC,0666);
  if(cf->fd<0) { die("Cannot create/open cache file"); }
  if(ftruncate(cf->fd,FILESIZE(c))<0) { die("Cannot extend cache file"); }
  strbuf_init(&lockp,0);
  strbuf_add(&lockp,"%s",path->v.string);
  strbuf_add(&lockp,"%s","-lock");
  cf->lock = strbuf_str(&lockp);
  log_debug(("Lock is '%s'",cf->lock));
}

static void cf_close(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  int i;

  for(i=0;i<MUTEXREGIONS;i++) {
    pthread_mutex_destroy(cf->mutexes+i);
  }
  if(close(cf->fd)<0) { die("Cannot close cache file"); }
  free(cf->lock);
  free(cf);
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
  .lock = lock,
  .unlock = unlock,
  .stats = 0
};

struct source * source_cachefile2_make(struct running *rr,
                                       struct jpf_value *conf) {
  struct cache_file *c;
 
  c = safe_malloc(sizeof(struct cache_file));
  return source_cache_make(rr,conf,&ops,c);
}

#include <stdio.h>
#include <stdlib.h>
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
#include "mmap.h"

// XXX start time to stats
#define HEADERSIZE(c) ((c)->entries*sizeof(struct header))
#define BODYSIZE(c)   ((c)->entries*(c)->block_size)
#define FILESIZE(c) (HEADERSIZE(c)+BODYSIZE(c))
#define OFFSET(c,slot) (HEADERSIZE(c)+(slot*(c)->block_size))
#define SLOT(cm,c,slot) ((cm)->data+OFFSET(c,slot))

CONFIG_LOGGING(cachemmap);

struct cache_mmap {
  int fd;
  char *data;
};

// XXX properly marshalled header
// XXX slots not int
static void get_header(struct header **h,struct cache *c,int slot,void *p) {
  struct cache_mmap *cm = (struct cache_mmap *)p;

  *h = ((struct header *)cm->data)+slot;
}

static void set_header(struct cache *c,struct header *h,int slot,void *p) {}

static void header_done(struct cache *c,struct header *h,int slot,void *p) {
}

static void write_data(struct cache *c,int slot,char *data,void *p) {
  struct cache_mmap *cm = (struct cache_mmap *)p;

  memcpy(SLOT(cm,c,slot),data,c->block_size);
}

static void read_data(struct cache *c,int slot,char **data,void *p) {
  struct cache_mmap *cm = (struct cache_mmap *)p;

  *data = SLOT(cm,c,slot);
}

static void read_done(char *data,void *priv) {}

static void cm_open(struct cache *c,struct jpf_value *conf,void *priv) {
  struct cache_mmap *cm = (struct cache_mmap *)priv;
  struct jpf_value *path; 
 
  path = jpfv_lookup(conf,"filename");
  if(!path) { die("No path to cachefile specified"); }
  cm->fd = open(path->v.string,O_CREAT|O_RDWR|O_TRUNC,0666);
  if(cm->fd<0) { die("Cannot create/open cache file"); }
  if(ftruncate(cm->fd,FILESIZE(c))<0) { die("Cannot extend cache file"); }
  cm->data = mmap(0,FILESIZE(c),PROT_READ|PROT_WRITE,MAP_SHARED,cm->fd,0);
  log_debug(("mmap %s at %p-%p",
             path->v.string,cm->data,cm->data+FILESIZE(c)));
  if(cm->data==((void *)-1)) { die("Cannot mmap cachemmap file"); }
}

static void cm_close(struct cache *c,void *priv) {
  struct cache_mmap *cm = (struct cache_mmap *)priv;

  if(close(cm->fd)<0) { die("Cannot close cache file"); }
  free(cm);
}

static struct cache_ops ops = {
  .open = cm_open,
  .close = cm_close,
  .get_header = get_header,
  .set_header = set_header,
  .header_done = header_done,
  .read_data = read_data,
  .write_data = write_data,
  .read_done = read_done,
  .stats = 0,
  .queue_write = cache_queue_write,
  .dequeue_prepare = 0,
  .dequeue_go = 0,
};

struct source * source_cachemmap2_make(struct running *rr,
                                       struct jpf_value *conf) {
  struct cache_file *c;
 
  c = safe_malloc(sizeof(struct cache_mmap));
  return source_cache_make(rr,conf,&ops,c);
}

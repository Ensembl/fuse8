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
  char *lock,*spoolfile,*spoolinfile;
  int fd;
  FILE *spoolf,*spoolinf;

  /* stats */
  int64_t n_gdequeue,n_bdequeue;
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

static void cf_open(struct cache *c,struct jpf_value *conf,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  struct jpf_value *path; 
  struct strbuf lockp,spoolinp;
  int flags;
 
  flags = 0;
  cf->spoolf = 0;
  cf->spoolfile = 0;
  cf->spoolinfile = 0;
  path = jpfv_lookup(conf,"spoolfile");
  if(path) {
    cf->spoolfile = strdup(path->v.string);
    strbuf_init(&spoolinp,0);
    strbuf_add(&spoolinp,"%s",path->v.string);
    strbuf_add(&spoolinp,"%s","-in");
    cf->spoolinfile = strbuf_str(&spoolinp);
    flags |= O_SYNC;
  }
  unlink(cf->spoolfile);
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
  cf->n_gdequeue = cf->n_bdequeue = 0;
}

static void cf_close(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  if(close(cf->fd)<0) { die("Cannot close cache file"); }
  free(cf->lock);
  free(cf->spoolfile);
  free(cf->spoolinfile);
  free(cf);
}

int dequeue_prepare(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  if(cf->spoolf) {
    fclose(cf->spoolf);
    cf->spoolf = 0;
    cf->n_bdequeue++;
    if(lock_path(cf->lock)) {
      log_debug(("dequeue lock contention, holding off"));
      return 0;
    }
    if(rename(cf->spoolfile,cf->spoolinfile)) {
      unlink(cf->spoolfile);
      unlock_path(cf->lock);
      log_warn(("dequeue spoolfile could not be renamed errno=%d",errno));
      return 0;
    }
    cf->spoolinf = fopen(cf->spoolinfile,"r");
    if(!cf->spoolinf) {
      log_warn(("Cannot open spoolin file"));
      unlock_path(cf->lock);
      return 0;
    }
    log_debug(("preparing do dequeue"));
    cf->n_gdequeue++;
    cf->n_bdequeue--;
    return 1;
  } else {
    unlink(cf->spoolfile);
    return 0;
  }
}

int dequeue_go(struct cache *c,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  struct hash *h;
  int64_t start;
  int more = 1;
  char *hstr,*data;
  int64_t blen;

  start = microtime();
  data = safe_malloc(c->block_size);
  log_debug(("dequeueing"));
  while(microtime()-start < 1000) {
    if(fscanf(cf->spoolinf,"%m[0-9a-fA-F]:%"PRId64"\n",&hstr,&blen)<1) {
      more = 0;
      break;
    }
    log_debug(("Got hash '%s' len=%"PRId64,hstr,blen));
    h = hash_str(hstr);
    if(!h) {
      more = 0;
      break;
    }
    free(hstr);
    if(fread(data,c->block_size,1,cf->spoolinf)<1) {
      more = 0;
      break;
    }
    cache_queue_write(c,h,data,priv);
    free_hash(h);
  }
  if(!more) {
    log_debug(("finished dequeueing"));
    fclose(cf->spoolinf);
    cf->spoolinf = 0;
    unlink(cf->spoolinfile);
    unlock_path(cf->lock);
  }
  free(data);
  return more;
}

static void spool_open(struct cache_file *cf) {
  if(!cf->spoolfile || cf->spoolf) { return; }
  cf->spoolf = fopen(cf->spoolfile,"ab");
  if(!cf->spoolf) {
    log_warn(("Could not open spool file (errno=%d)",errno));
  }
}

void queue_write(struct cache *c,struct hash *h,char *data,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;
  char *hashstr;
  int corrupt = 0;

  if(!cf->spoolfile) {
    cache_queue_write(c,h,data,priv);
    return;
  }
  spool_open(cf);
  if(!cf->spoolf) { return; }
  hashstr = print_hash(h);
  if(fprintf(cf->spoolf,"%s:%"PRId64"\n",hashstr,c->block_size)<0) {
    log_warn(("cannot spool: fprintf failed errno=%d",errno));
    corrupt = 1;
  }
  if(fwrite(data,c->block_size,1,cf->spoolf)!=1) {
    log_warn(("cannot spool: fwrite failed errno=%d",errno));
    corrupt = 1;
  }
  free(hashstr);
  if(corrupt) {
    unlink(cf->spoolfile);
    fclose(cf->spoolf);
  }
}

static void stats(struct cache *c,struct jpf_value *out,void *priv) {
  struct cache_file *cf = (struct cache_file *)priv;

  jpfv_assoc_add(out,"dequeue_good",jpfv_number_int(cf->n_gdequeue));
  jpfv_assoc_add(out,"dequeue_bad",jpfv_number_int(cf->n_bdequeue));
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
  .stats = stats,
  .queue_write = queue_write,
  .dequeue_prepare = dequeue_prepare,
  .dequeue_go = dequeue_go,
};

struct source * source_cachefile2_make(struct running *rr,
                                       struct jpf_value *conf) {
  struct cache_file *c;
 
  c = safe_malloc(sizeof(struct cache_file));
  return source_cache_make(rr,conf,&ops,c);
}

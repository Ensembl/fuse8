#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/misc.h"
#include "util/ranges.h"
#include "util/strbuf.h"
#include "jpf/jpf.h"

struct fuse_stat {
  char *filename;
  char *uri;
  int inode;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  off_t size;
};

struct sourcelist;
struct source;
struct chunk;
struct request;

// XXX inodes not int!
typedef void (*src_fn)(struct source *);
typedef int (*src_stat_fn)(struct source *,int inode,struct fuse_stat *);
typedef int (*src_lookup_fn)(struct source *,int inode,
                             const char *name,struct fuse_stat *);
typedef int (*src_readdir_fn)(struct source *,int inode,int **members);
typedef void (*src_request_fn)(struct source *,struct request *);
typedef int (*src_readlink_fn)(struct source *,int inode,char **out);
typedef void (*src_write_fn)(struct source *,struct request *,
                             struct chunk *ck);
typedef void (*src_stats_fn)(struct source *,struct jpf_value *);

// XXX string manipulation (replace strdupcatnfree)
// XXX prefilter
struct source {
  struct ref r;
  struct source *next,**prev;
  struct sourcelist *sl;
  struct failures *fails;

  char *name;
  void *priv;
  src_fn close;
  src_stat_fn stat;
  src_lookup_fn lookup;
  src_readdir_fn readdir;
  src_readlink_fn readlink;
  src_request_fn read;
  src_write_fn write;
  src_stats_fn stats;

  /* stats */
  uint64_t bytes,hits,r_time,w_time,errors,writes;
};

struct sourcelist {
  struct ref r;
  struct source *root;
 
  struct hits *hits; 
  uint64_t bytes,n_hits,time;
};

struct interface;
typedef void (*ic_fn)(struct interface *);

struct interface {
  struct ref r;

  char *name;
  void *priv;
  ic_fn close,quit;

  /* stats */
  int64_t bytes,hits,errors;
};

/* You may (should!) inspect this */
struct chunk {
  char *out; // XXX don't copy
  int64_t offset,length;
  int eof;
  struct chunk *next;
  struct source *origin;
};

typedef void (*req_fn)(int failed_errno,char *data,void *priv);

struct request {
  struct ref r;
  struct interface *ic;
  struct sourcelist *sl;

  struct chunk *chunks;
  struct source *src;

  char *spec,*out;
  int64_t offset,length;
  struct ranges desired;
  int failed_errno;

  req_fn done;
  void *priv;
  
  /* stats */
  uint64_t start,p_start;
};

#endif

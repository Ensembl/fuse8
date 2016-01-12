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
#include <errno.h>

#include "file2.h"
#include "../syncsource.h"
#include "../util/misc.h"
#include "../util/path.h"
#include "../util/logging.h"
#include "../source.h"
#include "../request.h"

#define PREFIX "file://"

#define FILEBLOCKSIZE 65536

CONFIG_LOGGING(file)

struct file {
  char *root;
};

static struct file * file_open(char *path) {
  struct file *out;
  
  out = safe_malloc(sizeof(struct file));
  out->root = strdup(path);
  return out;
}

static void file_close(struct file *c) {
  free(c->root);
  free(c);
}

#define MAXPATHDEPTH 1000

static int track_path(char *path,char *dir) {
  ino_t root_ino,path_ino;
  struct stat st;
  int i;

  dir = strdup(dir);
  if(stat("/",&st)<0) { free(dir); return 0; }
  root_ino = st.st_ino;
  if(stat(path,&st)<0) { free(dir); return 0; }
  path_ino = st.st_ino;
  for(i=0;i<MAXPATHDEPTH;i++) {
    if(stat(dir,&st)<0) { free(dir); return 0; }
    if(st.st_ino == root_ino) { free(dir); return 0; }
    if(st.st_ino == path_ino) { free(dir); return 1; }
    dir = strdupcatnfree(dir,"../",0,dir,0);
  }
  free(dir);
  return 0;
}

static int is_regular(char *dir,char *file) {
  char *path;
  struct stat st;

  path = strdupcatnfree(dir,"/",file,0,0);
  if(stat(path,&st)<0) { free(path); return 0; }
  if(!S_ISREG(st.st_mode)) { free(path); return 0; }
  free(path);
  return 1;
}

static void do_request(int fd,struct syncsource *ss,
                       int64_t start,int64_t len,struct chunk **ck) {
  char *buf;
  int n;

  buf = safe_malloc(len);
  log_debug(("do_request %"PRId64"+%"PRId64,start,len));
  if(lseek(fd,start,SEEK_SET)>=0) {
    n = read_all(fd,buf,len);
    if(n<0) { return; }
    *ck = rq_chunk(syncsource_source(ss),buf,start,n,n<len,*ck);
    free(buf);
  }
  // XXX own our errors
}

static struct chunk * file_read(struct syncsource *ss,struct request *rq) {
  struct file *c = (struct file *)(ss->priv);
  struct chunk *ck = 0;
  struct ranges blocks;
  struct rangei ri;
  char *dir,*file;
  int fd;
  int64_t x,y;

  if(!strncmp(rq->spec,PREFIX,strlen(PREFIX))) {
    to_dir_file(rq->spec+strlen(PREFIX),&dir,&file);
    if(*file && track_path(c->root,dir) && is_regular(dir,file)) {
      ranges_copy(&blocks,&(rq->desired));
      ranges_blockify_expand(&blocks,FILEBLOCKSIZE);
      if(log_do_debug) {
        char *r1 = ranges_print(&(rq->desired));
        char *r2 = ranges_print(&blocks);
        log_debug(("desired: %s expanded: %s size=%d",r1,r2,FILEBLOCKSIZE));
        free(r1);
        free(r2);
      }
      file = strdupcatnfree(dir,file,0,file,0);
      fd = open(file,O_RDONLY); // XXX errors
      ranges_start(&blocks,&ri);
      while(ranges_next(&ri,&x,&y)) {
        do_request(fd,ss,x,y-x,&ck);
      }
      ranges_free(&blocks);
      close(fd);
    }
    free(dir);
    free(file);
  }
  return ck;
}

static void ds_close(struct syncsource *src) {
  file_close((struct file *)src->priv);
}

struct source * source_file2_make(struct running *rr,
                                  struct jpf_value *conf) {
  struct syncsource *ss;
  struct jpf_value *root;

  root = jpfv_lookup(conf,"root");
  if(!root) { die("Root not specified"); }
  // XXX init to util
  ss = safe_malloc(sizeof(struct syncsource));
  ss->priv = file_open(root->v.string);
  ss->read = file_read;
  ss->write = 0;
  ss->close = ds_close;
  return syncsource_create(rr->sq,ss);
}

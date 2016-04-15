#include <string.h>
#include <pthread.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#include "compressor.h"

#include "misc.h"
#include "event.h"
#include "logging.h"
#include "background.h"

CONFIG_LOGGING(compressor)

/* Not too big to avoid starving important jobs */
#define CHUNK (64*1024)

struct compressor {
  struct ref r;
  struct background *bgd;
};

struct job {
  z_stream strm;
  int str_inited,in_fd,out_fd,in_tail;
  unsigned char *chunk_in,*chunk_out;
};

static int block(struct job *j) {
  int r;

  j->strm.avail_in = read_all(j->in_fd,j->chunk_in,CHUNK);
  if(j->strm.avail_in == -1) {
    log_error(("Cannot read file errno=%d",errno));
    return 1;
  }
  if(j->strm.avail_in == 0) { j->in_tail = 1; }
  j->strm.next_in = j->chunk_in;
  log_debug(("read %d bytes for compression",j->strm.avail_in));
  do {
    j->strm.avail_out = CHUNK;
    j->strm.next_out = j->chunk_out;
    r = deflate(&(j->strm),j->in_tail?Z_FINISH:Z_NO_FLUSH);
    if(r == Z_STREAM_ERROR) {
      log_error(("Cannot deflate errno=%d",errno));
      return 1;
    }
    if(write_all(j->out_fd,j->chunk_out,CHUNK-j->strm.avail_out)<0) {
      log_error(("Cannot write errno=%d",errno));
      return 1;
    }
  } while(j->strm.avail_out == 0);
  log_debug(("compression done"));
  return 0;
}

// XXX return codes
static void job(struct background *bb,void *data,int dispose,void *pay) {
  char *path = (void *)data;
  char *new_path=0,*tmp_path=0;
  int r;
  struct job j;

  if(dispose) { free(data); return; }
  log_debug(("got compression job %s",path));
  j.str_inited = 0;
  j.in_fd = j.out_fd = -1;
  j.chunk_in = j.chunk_out = 0;
  j.strm.zalloc = Z_NULL;
  j.strm.zfree = Z_NULL;
  j.strm.opaque = Z_NULL;
  // XXX proper gzip header
  r = deflateInit2(&(j.strm),9,Z_DEFLATED,31,9,Z_DEFAULT_STRATEGY);
  if(r!=Z_OK) {
    log_error(("Cannot compress"));
    goto finish;
  }
  j.str_inited = 1;
  j.in_fd = open(path,O_RDONLY);
  if(j.in_fd==-1) {
    log_error(("Cannot open file to read compress"));
    goto finish;
  }
  tmp_path = make_string("%s.gz.tmp",path);
  j.out_fd = open(tmp_path,O_WRONLY|O_CREAT|O_EXCL,0666); // XXX follow perm of in_fd
  if(j.out_fd==-1) {
    log_error(("Cannot open file to write during compress"));
    goto finish;
  }
  j.chunk_in = safe_malloc(CHUNK);
  j.chunk_out = safe_malloc(CHUNK);
  j.in_tail = 0;
  while(!j.in_tail) {
    if(block(&j)) {
      goto finish;
    }
    background_yield(bb);
  }
  new_path = make_string("%s.gz",path);
  if(rename(tmp_path,new_path)) {
    log_error(("cannot rename compressed file"));
    goto finish;
  }
  unlink(path);
  log_debug(("rename %s -> %s",tmp_path,new_path));
finish:
  if(j.out_fd!=-1) { close(j.out_fd); }
  if(j.in_fd!=-1) { close(j.in_fd); }
  if(j.str_inited) { deflateEnd(&(j.strm)); }
  if(j.chunk_in) { free(j.chunk_in); }
  if(j.chunk_out) { free(j.chunk_out); }
  if(new_path) { free(new_path); }
  if(tmp_path) { free(tmp_path); }
  free(path);
}

static void cc_ref_release(void *data) {
  struct compressor *cc = (struct compressor *)data;

  background_release(cc->bgd);
}

static void cc_ref_free(void *data) {
  struct compressor *cc = (struct compressor *)data;

  free(cc);
}

struct compressor * compressor_create(void) {
  struct compressor *cc;

  cc = safe_malloc(sizeof(struct compressor));
  cc->bgd = background_create(job,cc);
  ref_create(&(cc->r));
  ref_on_release(&(cc->r),cc_ref_release,cc);
  ref_on_free(&(cc->r),cc_ref_free,cc);
  return cc;
}

void compressor_release(struct compressor *cc) {
  log_debug(("compressor release"));
  ref_release(&(cc->r));
}

struct background * compressor_background(struct compressor *cc) {
  return cc->bgd;
}

void compressor_add(struct compressor *cc,char *filename) {
  background_add(cc->bgd,filename);
}

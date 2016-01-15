#define FUSE_USE_VERSION 30

#include <time.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fuse/fuse_lowlevel.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>

#include "../util/misc.h"
#include "../jpf/jpf.h"
#include "../util/logging.h"
#include "../util/strbuf.h"
#include "../interface.h"
#include "../sourcelist.h"
#include "fuse.h"
#include "../syncif.h"

CONFIG_LOGGING(fuse);

struct fuseif {
  struct interface *ic;
  struct running *rr;
  struct syncif *si;
  struct sourcelist *sl;
  time_t start;
  /* config */
  int kcache;
  /* threading */
  pthread_t thread;
  pthread_mutex_t start_mutex,unmount_mutex;
  pthread_cond_t start_cond;
  char *path;
  /* libfuse stuff */
  char *mountpoint;
  struct fuse_chan *ch;
  struct fuse_session *se;
  int mounted,have_running,did_quit;
};

struct fuse_req {
  struct fuseif *fi;
  char *uri;
  fuse_req_t req;
  size_t size;
};

static void fi_quit(struct interface *ic) {
  struct fuseif *fi = (struct fuseif *)ic->priv;

  log_debug(("fuse interface quit requested"));
  pthread_mutex_lock(&(fi->unmount_mutex));
  if(fi->did_quit) {
    log_debug(("fuse interface already quit"));
    pthread_mutex_unlock(&(fi->unmount_mutex));
    return;
  }
  fi->did_quit = 1;
  pthread_mutex_unlock(&(fi->unmount_mutex));
  log_info(("fuse interface quitting"));
  if(fi->mounted) {
    fuse_unmount(fi->mountpoint,fi->ch);
    fi->mounted = 0;
  }
  if(fi->have_running) {
    log_debug(("release running"));
    ref_release(&(fi->rr->ic_running)); 
    fi->have_running = 0;
  }
  log_debug(("fuse interface quit complete"));
}

static void fi_close(struct interface *ic) {
  struct fuseif *fi = (struct fuseif *)ic->priv;

  fi_quit(ic);
  log_info(("Waiting for main fuse thread to exit"));
  pthread_join(fi->thread,0);
  log_info(("main fuse thread finished"));
  if(fi->se) {
    fuse_session_exit(fi->se);
  }
  sl_release_weak(fi->sl);
  if(fi->se) { fuse_session_destroy(fi->se); }
  free(fi->path);
  free(fi->mountpoint);
  free(fi);
}

static void xfer_stat(struct fuseif *fi,
                      struct fuse_stat *from,struct stat *to) {
  to->st_ino = from->inode;
  to->st_mode = from->mode;
  to->st_uid = from->uid;
  to->st_gid = from->gid;
  to->st_size = from->size;
  to->st_mtime = to->st_atime = to->st_ctime = fi->start;
  to->st_nlink = 1;
  if(from->mode & S_IFDIR) { to->st_nlink++; }
}

static void fuse_getattr(fuse_req_t req, fuse_ino_t ino,
                         struct fuse_file_info *ffi) {
  struct fuseif *fi;
  struct fuse_stat fs;
  struct stat stbuf;

  log_debug(("fuse stat called"));
  fi = (struct fuseif *)fuse_req_userdata(req);
  memset(&stbuf,0,sizeof(stbuf));
  if(sl_stat(fi->sl,ino,&fs)) {
     fuse_reply_err(req,ENOENT);
  } else {
    xfer_stat(fi,&fs,&stbuf);
    fuse_reply_attr(req,&stbuf,1.0); // XXX 1.0?
  }
}

static void fuse_lookup(fuse_req_t req,fuse_ino_t parent,const char *name) {
  struct fuseif *fi;
  struct fuse_stat fs;
  struct fuse_entry_param e;
 
  fi = (struct fuseif *)fuse_req_userdata(req);
  if(sl_lookup(fi->sl,parent,name,&fs)) {
    fuse_reply_err(req,ENOENT);
  } else {
    memset(&e,0,sizeof(e));
    e.attr_timeout = 1.0; // XXX 1.0?
    e.entry_timeout = 1.0;
    xfer_stat(fi,&fs,&(e.attr));
    e.ino = e.attr.st_ino;
    fuse_reply_entry(req,&e);
  }
}

// XXX not int
static void add_entry(fuse_req_t req,struct strbuf *buf,
                      char *name,int ino,int mode) {
  struct stat stbuf;
  int our_size;
  char *where;

  log_debug(("readdir entry '%s' ino=%d",name,ino));
  stbuf.st_ino = ino;
  stbuf.st_mode = mode;
  our_size = fuse_add_direntry(req,strbuf_str(buf),0,name,&stbuf,0);
  where = strbuf_extend(buf,our_size);
  fuse_add_direntry(req,where,our_size,name,&stbuf,strbuf_len(buf));
}

static void add_real_entry(fuse_req_t req,struct fuseif *fi,
                           struct strbuf *buf,int ino) {
  struct fuse_stat fs;

  if(sl_stat(fi->sl,ino,&fs)) { return; }
  add_entry(req,buf,fs.filename,fs.inode,fs.mode);
}

static void fuse_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                         off_t off, struct fuse_file_info *ffi) {
  struct fuseif *fi;
  struct fuse_stat self,parent;
  struct strbuf buf;
  int *members,*m,amt,len;
  
  fi = (struct fuseif *)fuse_req_userdata(req);
  strbuf_init(&buf,0);
  if(sl_readdir(fi->sl,ino,&members)) {
    fuse_reply_err(req,ENOENT);
  } else {
    if(!sl_stat(fi->sl,ino,&self)) {
      add_entry(req,&buf,".",self.inode,self.mode);
    }
    if(!sl_lookup(fi->sl,ino,"..",&parent)) {
      add_entry(req,&buf,"..",parent.inode,parent.mode);
    }
    for(m=members;*m;m++) {
      add_real_entry(req,fi,&buf,*m);
    } 
    free(members);
  }
  len = strbuf_len(&buf);
  if(off<len) {
    amt = len-off;
    if(amt>size) { amt = size; }
    fuse_reply_buf(req,strbuf_str(&buf)+off,amt);
    strbuf_free(&buf);
  } else {
    strbuf_free(&buf);
    fuse_reply_buf(req,0,0);
  }
}

static void fuse_open(fuse_req_t req,fuse_ino_t ino,
                      struct fuse_file_info *ffi) {
  struct fuseif *fi;
  struct fuse_stat stat;

  fi = (struct fuseif *)fuse_req_userdata(req);
  ffi->keep_cache = fi->kcache;
  if(sl_stat(fi->sl,ino,&stat)) {
    fuse_reply_err(req,ENOENT);
  } else if(stat.mode & S_IFDIR) {
    fuse_reply_err(req,EISDIR);
  } else {
    fuse_reply_open(req,ffi);
  }
}

static void read_done(int failed_errno,char *data,void *priv) {
  struct fuse_req *fr;

  fr = (struct fuse_req *)priv;
  if(failed_errno) {
    // XXX better reporting
    log_warn(("read failed for '%s' errno=%d",fr->uri,failed_errno));
    ic_collect(fr->fi->ic,-1);
    fuse_reply_err(fr->req,failed_errno);
  } else {
    // XXX explicit size in request return
    log_debug(("read success"));
    ic_collect(fr->fi->ic,fr->size);
    fuse_reply_buf(fr->req,data,fr->size);
  }
  ic_release(fr->fi->ic);
  free(fr->uri);
  free(fr); 
}

static void fuse_read(fuse_req_t req,fuse_ino_t ino,size_t size,
                      off_t off,struct fuse_file_info *ffi) {
  struct fuseif *fi;
  struct fuse_stat stat;
  struct fuse_req *fr;
  char *uri;

  fi = (struct fuseif *)fuse_req_userdata(req);
  if(sl_stat(fi->sl,ino,&stat)) {
    fuse_reply_err(req,EIO);
    return;
  }
  uri = stat.uri;
  if(size>stat.size-off) { size = stat.size-off; }
  fr = safe_malloc(sizeof(struct fuse_req));
  fr->fi = fi;
  fr->req = req;
  fr->size = size;
  fr->uri = strdup(uri);
  ic_acquire(fi->ic);
  si_read(fi->si,fi->sl,uri,off,size,read_done,fr);
}
// XXX others sl->si
// XXX si_readlink
static void fuse_readlink(fuse_req_t req,fuse_ino_t ino) {
  struct fuseif *fi;
  char *out;

  fi = (struct fuseif *)fuse_req_userdata(req);
  if(sl_readlink(fi->sl,ino,&out)) {
    fuse_reply_err(req,EIO);
    return;
  }
  fuse_reply_readlink(req,out);
  free(out);
}

static struct fuse_lowlevel_ops ll_oper = {
  .lookup   = fuse_lookup,
  .getattr  = fuse_getattr,
  .readdir  = fuse_readdir,
  .readlink = fuse_readlink,
  .open     = fuse_open,
  .read     = fuse_read,
};

static int fuseumount(char *path) {
  int pid,status;

  // XXX make an option
  // XXX move to utils
  pid = fork();
  if(pid==-1) { return -1; }
  if(pid!=0) {
    /* parent */
    while(1) {
      waitpid(pid,&status,0);
      if(WIFEXITED(status) || WIFSIGNALED(status)) { break; }
    }
    if(WIFEXITED(status)) { return WEXITSTATUS(status); }
    return -WTERMSIG(status);
  } else {
    /* child */
    execlp("sudo","-n","umount",path,(char *)0);
    exit(100);
  }
}

static void force_unmount(char *path) {
  /* Do our best to clear from any eralier failed servers */
  umount(path);
  fuseumount(path);
}

#define ARGC 2
static void * fuse_main(void *data) {
  struct fuseif *fi = (struct fuseif *)data;
  char * (argv[ARGC+1]) = {"fuse8",fi->path,0};
  struct fuse_args args = FUSE_ARGS_INIT(ARGC,argv);
  int err;

  log_debug(("fuse main thread starting"));
  ic_acquire(fi->ic);
  force_unmount(fi->path);
  if(fuse_parse_cmdline(&args,&(fi->mountpoint),0,0) != -1 &&
     (fi->ch = fuse_mount(fi->mountpoint,&args))) {
    fi->mounted = 1; /* No lock, still have start mutex */
    log_debug(("fuse mount mounted"));
    fi->se = fuse_lowlevel_new(&args,&ll_oper,sizeof(ll_oper),fi);
    if(fi->se) {
      fuse_session_add_chan(fi->se,fi->ch);
      pthread_mutex_lock(&(fi->start_mutex));
      pthread_cond_signal(&(fi->start_cond));
      pthread_mutex_unlock(&(fi->start_mutex));
      err = fuse_session_loop(fi->se);
      log_debug(("FUSE loop exited"));
      if(err) { log_error(("FUSE loop exited due to error")); }
    }
  }
  fuse_opt_free_args(&args);
  pthread_mutex_lock(&(fi->start_mutex));
  pthread_cond_signal(&(fi->start_cond));
  pthread_mutex_unlock(&(fi->start_mutex));
  fi_quit(fi->ic);
  ic_release(fi->ic);
  return 0;
}

static void fuse_go(struct fuseif *fi) {
  pthread_attr_t attr;
 
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
  fi->ch = 0;
  fi->mounted = 0;
  fi->have_running = 1;
  fi->did_quit = 0;
  time(&(fi->start));
  pthread_mutex_init(&(fi->unmount_mutex),0);
  pthread_mutex_init(&(fi->start_mutex),0);
  pthread_cond_init(&(fi->start_cond),0);
  pthread_mutex_lock(&(fi->start_mutex));
  pthread_create(&(fi->thread),&attr,fuse_main,fi);
  pthread_attr_destroy(&attr);
  pthread_cond_wait(&(fi->start_cond),&(fi->start_mutex)); 
  pthread_mutex_unlock(&(fi->start_mutex));
}

struct interface * ic_fuse_make(struct running *rr,struct jpf_value *conf) {
  struct interface *ic;
  struct fuseif *fi;
  struct jpf_value *path;

  path = jpfv_lookup(conf,"path");
  if(!path) { die("Path missing"); }
  ic = ic_create();
  ic->name = "fuse";
  fi = safe_malloc(sizeof(struct fuseif));
  sl_acquire_weak(rr->sl);
  fi->rr = rr;
  fi->sl = rr->sl; // XXX use rr
  fi->si = rr->si;
  fi->ic = ic;
  fi->se = 0;
  fi->mountpoint = 0;
  fi->kcache = jpfv_bool(jpfv_lookup(conf,"kcache"));
  if(fi->kcache==-2) { fi->kcache = 1; }
  if(fi->kcache==-1) { die("Bad kcache spec"); }
  fi->path = strdup(path->v.string);
  ic->priv = fi;
  ic->close = fi_close;
  ic->quit = fi_quit;
  ref_acquire(&(rr->ic_running));
  fuse_go(fi);
  return ic;
}

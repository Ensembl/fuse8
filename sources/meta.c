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
#include "../util/misc.h"
#include "../util/assoc.h"
#include "../util/array.h"
#include "../util/path.h"
#include "../util/logging.h"
#include "../source.h"
#include "../jpf/jpf.h"

CONFIG_LOGGING(meta)

struct meta {
  int inode;
  struct assoc *stat,*readdir,*lookup;
};

struct metabuild {
  struct source *src;
  struct meta *m;
  struct fuse_stat fdef,ddef,ldef;
  struct assoc *dirfiles,*fninode;
};

static void sm_close(struct source *src) {
  struct meta * meta = (struct meta *)src->priv;

  assoc_release(meta->stat);
  assoc_release(meta->readdir);
  assoc_release(meta->lookup);
  free(meta);
  src_close_finished(src);
}

static void init_stat(struct fuse_stat *st) {
  st->filename = 0;
  st->mode = 0;
  st->uid = 0;
  st->gid = 0;
  st->size = 0;
}

static void set_stat(struct fuse_stat *st,struct jpf_value *val) {
  struct jpf_value *v;
  struct safe_passwd *pw;
  struct safe_group *gr;
  int ok = 1,x;

  // XXX YUK! FACTOR!
  v = jpfv_lookup(val,"user");
  if(v) {
    pw = safe_getpwnam(v->v.string);
    if(!pw) {
      // XXX symbolic errno
      log_error(("Error looking up user '%s': %d",v->v.string,errno));
      ok = 0;
    } else if(!pw->p) {
      log_error(("No such user '%s'",v->v.string));
      safe_passwd_free(pw);
      ok = 0;
    } else {
      st->uid = pw->p->pw_uid;
      safe_passwd_free(pw);
    }
  }
  v = jpfv_lookup(val,"group");
  if(v) {
    gr = safe_getgrnam(v->v.string);
    if(!gr) {
      log_error(("Error looking up group '%s': %d",v->v.string,errno));
      ok = 0;
    } else if(!gr->g) {
      log_error(("No such user '%s'",v->v.string));
      safe_group_free(gr);
      ok = 0;
    } else {
      st->gid = gr->g->gr_gid;
      safe_group_free(gr);
    }
  }
  v = jpfv_lookup(val,"perms");
  if(v) {
    if(jpfv_int(v,&x)) {
      log_error(("Bad permissions, must be number"));
      ok = 0;
    } else {
      st->mode |= x;
    }
  }
  v = jpfv_lookup(val,"size");
  if(v) {
    if(jpfv_int64(v,&(st->size))) {
      log_error(("Bad size, must be number"));
      ok = 0;
    }
  } else {
    st->size = 0;
  }
  st->uri = 0;
  if(S_ISREG(st->mode)) {
    v = jpfv_lookup(val,"uri");
    if(!v) { log_error(("file must have uri\n")); ok = 0; }
    else { st->uri = strdup(v->v.string); }
  } else if(S_ISLNK(st->mode)) {
    v = jpfv_lookup(val,"target");
    if(!v) { log_error(("file must have target\n")); ok = 0; }
    else { st->uri = strdup(v->v.string); }
  }
  if(!ok) {
    exit(1);
  }
}

static void add_to_dir(struct metabuild *mb,char *dir,char *file,
                       struct fuse_stat *st) {
  struct array *a;

  if(!*dir && !*file) { return; } /* root */
  a = (struct array *)assoc_lookup(mb->dirfiles,dir);
  if(!a) {
    a = array_create(type_free,0);
    assoc_set(mb->dirfiles,strdup(dir),a);
  }
  array_insert(a,strdup(file));
}

static void array_free(void *target,void *priv) {
  struct array *a = (struct array *)target;

  array_release(a);
}

static void metabuild_make(struct metabuild *mb,
                           struct source *src,struct meta *m) {
  mb->m = m;
  mb->src = src;
  init_stat(&(mb->fdef));
  init_stat(&(mb->ldef));
  init_stat(&(mb->ddef));
  mb->dirfiles = assoc_create(type_free,0,array_free,0);
  mb->fninode = assoc_create(type_free,0,0,0);
}

static void metabuild_finish(struct metabuild *mb) {
  assoc_release(mb->dirfiles);
  assoc_release(mb->fninode);
}

static void add_type(struct metabuild *mb,struct jpf_value *val,
                     struct fuse_stat *st,char **path) {
  struct jpf_value *v;

  v = jpfv_lookup(val,"file");
  if(v) {
    *st = mb->fdef;
    *path = v->v.string;
    return;
  }
  v = jpfv_lookup(val,"dir");
  if(v) {
    *st = mb->ddef;
    *path = v->v.string;
    return;
  }
  v = jpfv_lookup(val,"link");
  if(v) {
    *st = mb->ldef;
    *path = v->v.string;
    return;
  }
  die("Must specify eicher file, dir, or link in entry"); 
}
 
// XXX symlinks
static void add_file(struct metabuild *mb,struct jpf_value *val) {
  struct fuse_stat *st;
  char *inode,*path,*dir,*file,*dot;

  st = safe_malloc(sizeof(struct fuse_stat));
  add_type(mb,val,st,&path);
  set_stat(st,val);
  st->inode = mb->m->inode++;
  path = trim_end(trim_start(path,"/",0),"/",1);
  if(!strcmp("",path)) { st->inode = 1; }
  // XXX more path sanity checks here
  inode = make_string("%d",st->inode);
  assoc_set(mb->m->stat,inode,st);
  assoc_set(mb->fninode,path,inode);
  path_separate(path,&dir,&file);
  /* Add . */
  dot = make_string("%d,.",st->inode);
  assoc_set(mb->m->lookup,dot,inode);
  /**/ 
  dir = trim_start(dir,"/",1);
  st->filename = strdup(file);
  add_to_dir(mb,dir,file,st);
  free(dir);
  free(file);
}

// XXX detect dups
static void resolve_dir(struct metabuild *mb,char *key,struct array *all) {
  int i,len;
  char *dir_i,*tail,*fn,*file_i,*comp,*dotdot;
  struct array * a;

  a = array_create(0,0);
  dir_i = assoc_lookup(mb->fninode,key);
  assoc_set(mb->m->readdir,dir_i,a);
  len = array_length(all);
  for(i=0;i<len;i++) {
    tail = (char *)array_index(all,i);
    fn = strdupcatnfree(key,"/",tail,0,0);
    fn = trim_start(trim_end(fn,"/",1),"/",1);
    file_i = assoc_lookup(mb->fninode,fn);
    free(fn);
    array_insert(a,file_i);
    comp = strdupcatnfree(dir_i,",",tail,0,0);
    assoc_set(mb->m->lookup,comp,file_i);
    /* Add .. (added for files, but no consequence: saves a stat) */
    dotdot = make_string("%s,..",file_i);
    assoc_set(mb->m->lookup,dotdot,dir_i);
  }
}

// XXX check a dir!
static void resolve_dirs(struct metabuild *mb) {
  struct assoc_iter it;
  char *dotdot,*one;

  associ_start(mb->dirfiles,&it);
  while(associ_next(&it)) {
    resolve_dir(mb,associ_key(&it),(struct array *)associ_value(&it));
  }
  /* Root .. */
  dotdot = strdup("1,..");
  one = strdup("1");
  ref_add_unalloc(src_ref(mb->src),one);
  assoc_set(mb->m->lookup,dotdot,one);
}

static void check_ok(struct meta *m) {
  struct fuse_stat *root;

  root = (struct fuse_stat *)assoc_lookup(m->stat,"1");
  // XXX more checks here
  if(!root) { die("/ missing"); }
}

static void process_file(struct meta *m,struct source *src,
                         struct jpf_value *val) {
  struct jpf_value *defaults,*files,*v;
  struct metabuild mb;
  int i;

  metabuild_make(&mb,src,m);
  defaults = jpfv_lookup(val,"defaults");
  if(defaults) {
    v = jpfv_lookup(defaults,"all");
    if(v) { set_stat(&(mb.fdef),v); }
    mb.ldef = mb.ddef = mb.fdef;
    v = jpfv_lookup(defaults,"files");
    if(v) { set_stat(&(mb.fdef),v); }
    v = jpfv_lookup(defaults,"dirs");
    if(v) { set_stat(&(mb.ddef),v); }
    v = jpfv_lookup(defaults,"links");
    if(v) { set_stat(&(mb.ldef),v); }
  }
  mb.fdef.mode |= S_IFREG;
  mb.ddef.mode |= S_IFDIR;
  mb.ldef.mode |= S_IFLNK;
  files = jpfv_lookup(val,"files");
  if(!files || files->type!=JPFV_ARRAY) { die("No files!"); }
  for(i=0;i<files->v.array.len;i++) {
    add_file(&mb,files->v.array.v[i]);
  }
  resolve_dirs(&mb);
  check_ok(m);
  metabuild_finish(&mb);
}

static void stat_free(void *target,void *priv) {
  struct fuse_stat *fs = (struct fuse_stat *)target;

  free(fs->uri);
  free(fs->filename);
  free(fs);
}

static void readdir_free(void *target,void *priv) {
  struct array *dir = (struct array *)target;

  array_release(dir);
}

static void init_meta(struct meta *m) {
  m->inode = 2;
  m->stat = assoc_create(type_free,0,stat_free,0);
  m->readdir = assoc_create(0,0,readdir_free,0);
  m->lookup = assoc_create(type_free,0,0,0);
}

static void load_file(struct meta *m,struct source *src,char *filename) {
  struct lexer lx;
  struct jpf_value *val;
  char *errors;

  jpf_lex_filename(&lx,filename);
  errors = jpf_dfparse(&lx,&val);
  if(errors) {
    errors = make_string("Errors reading '%s':\n%s",filename,errors);
    die(errors);
  }
  process_file(m,src,val);
  jpfv_free(val);
}

static int sm_stat(struct source *src,int inode,struct fuse_stat *out) {
  struct meta *m = (struct meta *)src->priv;
  struct fuse_stat *st;
  char *inodes;
    
  inodes = make_string("%d",inode);
  st = assoc_lookup(m->stat,inodes);
  if(st) { *out = *st; }
  free(inodes);
  return !st;
}

static int sm_readlink(struct source *src,int inode,char **out) {
  struct meta *m = (struct meta *)src->priv;
  struct fuse_stat *st;
  char *inodes;
    
  inodes = make_string("%d",inode);
  st = assoc_lookup(m->stat,inodes);
  if(!S_ISLNK(st->mode)){ return -1; }
  if(st) { *out = strdup(st->uri); }
  free(inodes);
  return !st;
}

static int sm_lookup(struct source *src,int inode,const char *name,
                     struct fuse_stat *out) {
  struct meta *m = (struct meta *)src->priv;
  struct fuse_stat *st;
  char *key,*inodes;

  key = make_string("%d,%s",inode,name);
  log_debug(("sm_lookup called for key '%s'",key));
  inodes = assoc_lookup(m->lookup,key);
  log_debug(("sm_lookup returned inode '%s'",inodes?inodes:"(null)"));
  free(key);
  if(!inodes) { return 1; }
  st = assoc_lookup(m->stat,inodes);
  if(!st) {
    log_warn(("Unexpected stat failure in sm_lookup: '%s'/%s",key,inodes));
    return 1;
  }
  *out = *st;
  return !st;
}

static int sm_readdir(struct source *src,int inode,int **out) {
  struct meta *m = (struct meta *)src->priv;
  char *inodes;
  struct array *a;
  int i,len;

  inodes = make_string("%d",inode);
  log_debug(("sm_readdir called for inode %s",inodes));
  a = assoc_lookup(m->readdir,inodes);
  if(a) {
    len = array_length(a);
    log_debug(("sm_readdir found %d entries",len));
    *out = safe_malloc((len+1)*sizeof(int));
    for(i=0;i<len;i++) {
      // XXX error check
      (*out)[i] = atoi(array_index(a,i));
    }
    (*out)[len] = 0;
  } else {
    log_debug(("sm_readdir entries not found"));
  }
  free(inodes);
  return !a;
}

struct source * source_meta_make(struct running *rr,
                                 struct jpf_value *conf) {
  struct source *src;
  struct meta *m;
  struct jpf_value *filename;

  src = src_create();
  src->priv = m = safe_malloc(sizeof(struct meta));
  src->stat = sm_stat;
  src->lookup = sm_lookup;
  src->readdir = sm_readdir;
  src->readlink = sm_readlink;
  src->close = sm_close;
  init_meta(m);
  filename = jpfv_lookup(conf,"filename");
  if(!filename) { die("No such file"); }
  load_file(m,src,filename->v.string);
  return src;
}

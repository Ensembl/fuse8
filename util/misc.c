#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <inttypes.h>

#include "misc.h"
#include "strbuf.h"

int64_t microtime(void) {
  struct timeval tv;

  gettimeofday(&tv,0);
  return ((int64_t)tv.tv_usec)+((int64_t)tv.tv_sec)*1000000LL;
}

void die(const char *str) {
  perror(str);
  exit(255);
}

void alloc_chk(void *v) {
  if(v) { return; }
  fprintf(stderr,"Out of memory\n");
  exit(255);
}

void * safe_malloc(size_t s) {
  void *out;

  out = malloc(s);
  alloc_chk(out);
  return out;
}

void * safe_realloc(void *p,size_t s) {
  void *out;

  out = realloc(p,s);
  alloc_chk(out);
  return out;
}

char * make_string(const char *fmt,...) {
  int n,size = 256;
  char *out;
  va_list ap;

  out = safe_malloc(size);
  while(1) {
    va_start(ap,fmt);
    n = vsnprintf(out,size,fmt,ap);
    va_end(ap);
    if(n<0) { fprintf(stderr,"Print failed\n"); exit(255); }
    if(n<size) { return out; }
    size = n+1;
    out = safe_realloc(out,size);
  }
}

struct safe_passwd * safe_getpwnam(char *username) {
  struct safe_passwd *p;
  struct passwd *out;
  int size = sysconf(_SC_GETPW_R_SIZE_MAX);

  p = safe_malloc(sizeof(struct safe_passwd));
  p->p = safe_malloc(sizeof(struct passwd));
  while(1) {
    p->buf = safe_malloc(size);
    errno = 0;
    if(getpwnam_r(username,p->p,p->buf,size,&out)) {
      if(errno!=ERANGE) { return 0; }
    } else {
      if(!out) { free(p->p); p->p = 0; }
      return p;
    }
    size *= 2;
    free(p->buf);
  }
}

void safe_passwd_free(struct safe_passwd *p) {
  free(p->p);
  free(p->buf);
  free(p);
}

struct safe_group * safe_getgrnam(char *groupname) {
  struct safe_group *g;
  struct group *out;
  int size = sysconf(_SC_GETGR_R_SIZE_MAX);

  g = safe_malloc(sizeof(struct safe_group));
  g->g = safe_malloc(sizeof(struct group));
  while(1) {
    g->buf = safe_malloc(size);
    errno = 0;
    if(getgrnam_r(groupname,g->g,g->buf,size,&out)) {
      if(errno!=ERANGE) { return 0; }
    } else {
      if(!out) { free(g->g); g->g = 0; }
      return g;
    }
    size *= 2;
    free(g->buf);
  }
}

void safe_group_free(struct safe_group *g) {
  free(g->g);
  free(g->buf);
  free(g);
}

char * make_string_v(const char *fmt,va_list ap) {
  int n,size = 256;
  char *out;
  va_list ap2;

  out = safe_malloc(size);
  while(1) {
    va_copy(ap2,ap);
    n = vsnprintf(out,size,fmt,ap);
    va_end(ap2);
    if(n<0) { fprintf(stderr,"Print failed\n"); exit(255); }
    if(n<size) { return out; }
    size = n+1;
    out = safe_realloc(out,size);
  }
}

char * gcwd(void) {
  char *out=0;
  int len = 128;

  while(!out) {
    len *= 2;
    out = getcwd(out,len);
    if(!out && errno!=ERANGE) { die("getcwd failed"); }
  }
  return out;
}

/* This function needs to die */
char * strdupcatnfree(char *a,...) {
  char *out,*b;
  int n;
  va_list ap;

  n = strlen(a)+1;
  va_start(ap,a);
  while(1) {
    b = va_arg(ap,char *);
    if(!b) { break; }
    n += strlen(b);
  }
  va_end(ap); 
  out = safe_malloc(n);
  strcpy(out,a);
  va_start(ap,a);
  while(1) {
    b = va_arg(ap,char *);
    if(!b) { break; }
    strcat(out,b);
  }
  while(1) {
    b = va_arg(ap,char *);
    if(!b) { break; }
    free(b);
  }
  va_end(ap);
  return out;
}

char * trim_start(char *target,char *duds,int freep) {
  char *out;
  int i;

  for(i=0;target[i];i++) {
    if(!strchr(duds,target[i])) { break; }
  }
  out = strdup(target+i);
  if(freep) { free(target); }
  return out;
}

char * trim_end(char *target,char *duds,int freep) {
  char *out;
  int i;

  for(i=strlen(target)-1;i>=0;i--) {
    if(!strchr(duds,target[i])) { break; }
  }
  out = strdup(target);
  out[i+1] = '\0';
  if(freep) { free(target); }
  return out;
}

char * fgetline(FILE *f,int maxlen) {
  char *c = 0;
  int len = 0;
  int i,j;

  for(i=0;i<maxlen;i++) {
    if(i==len) {
      len = (len*3/2)+8;
      c = safe_realloc(c,len+1);
    }
    j = getc(f);
    if(j==EOF) { c[i]='\0'; break; }
    c[i] = j;
    if(c[i] == '\n' || c[i] == '\r') {
      j = getc(f);
      if(j!=-1) {
        if((j=='\n' || j=='\r') && j!=c[i]) {
          /* Fine, \n\r or \r\n */
        } else {
          fseek(f,-1L,SEEK_CUR);
        }
      }
      c[i] = '\0';
      break;
    }
  }
  return c; 
}

#define MAXTIME 128
char * iso_localtime(time_t t) {
  struct tm tm;
  char *out;

  tzset();
  out = safe_malloc(MAXTIME);
  if(!t) { time(&t); }
  if(!localtime_r(&t,&tm)) { goto error; }
  if(!strftime(out,MAXTIME,"%Y-%m-%d %H:%M:%S %Z",&tm)) { goto error; }
  return out;
  error:
    snprintf(out,MAXTIME,"Time unavailable");
    out[MAXTIME-1] = '\0';
    return out;
}

int write_all(int fd,char *buf,size_t count) {
  int r,n;

  for(n=0;count && n<1000;) {
    r = write(fd,buf,count);
    if(r<0) {
      if(errno==EAGAIN || errno==EINTR) { n++; continue; }
      return -1;
    }
    if(r==0) { n++; continue; }
    n = 0;
    buf += r;
    count -= r;
  }
  return 0;
}

// XXX not int!
int read_all(int fd,char *buf,size_t count) {
  int t=0,r,n;

  for(n=0;count && n<1000;) {
    r = read(fd,buf,count);
    if(r<0) {
      if(errno==EAGAIN || errno==EINTR) { n++; continue; }
      return -1;
    }
    if(r==0) { return t; }
    n = 0;
    buf += r;
    count -= r;
    t += r;
  }
  return 0;
}

#define GULP 1024
int read_file(char *filename,char **out) {
  int fd,r;
  struct strbuf buf;
  char *space;

  strbuf_init(&buf,0);
  fd = open(filename,O_RDONLY);
  if(fd<0) { return -1; }
  while(1) {
    space = strbuf_extend(&buf,GULP);
    r = read_all(fd,space,GULP);
    strbuf_retract(&buf,GULP-r);
    if(!r) { break; }
  }
  *out = strbuf_str(&buf);
  close(fd);
  return 0; 
}

int write_file(char *filename,char *out) {
  int fd;

  fd = open(filename,O_WRONLY|O_CREAT|O_TRUNC,0666);
  if(fd<0) { return -1; }
  if(write_all(fd,out,strlen(out))) { close(fd); return -1; }
  close(fd); 
  return 0;
}

#define MAX_SYMLINK (100*1024*1024)
char * safe_readlink(char *path) {
  char *out;
  int size=128,n;

  while(size<MAX_SYMLINK) {
    out = safe_malloc(size);
    n = readlink(path,out,size);
    if(n<size) { out[n] = '\0'; return out; }
    free(out);
    size *= 2;
  }
  die("Overlong symlink");
}

/* Note will need porting off linux */
char * self_path(void) {
  return safe_readlink("/proc/self/exe");
}

void type_free(void *target,void *priv) { free(target); }

struct refcb {
  ref_fn cb;
  void *data;
  struct refcb *next;
};

static void run_cbs(struct refcb *cb) {
  struct refcb *cbn;

  for(;cb;cb=cbn) {
    cbn = cb->next;
    cb->cb(cb->data);
    free(cb);
  }
}

void ref_on_free(struct ref *r,ref_fn cb,void *data) {
  struct refcb *rcb;

  rcb = safe_malloc(sizeof(struct refcb));
  rcb->cb = cb;
  rcb->data = data;
  rcb->next = r->on_free;
  r->on_free = rcb;
}

void ref_on_release(struct ref *r,ref_fn cb,void *data) {
  struct refcb *rcb;

  rcb = safe_malloc(sizeof(struct refcb));
  rcb->cb = cb;
  rcb->data = data;
  rcb->next = r->on_release;
  r->on_release = rcb;
}

void ref_create(struct ref *r) {
  r->strong = 1;
  r->weak = 0;
  r->on_release = 0;
  r->on_free = 0;
  r->frees = 0;
}

void ref_add_unalloc(struct ref *r,void *v) {
  if(!r->frees) {
    r->frees = array_create(type_free,0);
    ref_on_free(r,array_release_v,r->frees);
  }
  array_insert(r->frees,v);
}

void ref_acquire(struct ref *r) {
  if(r->strong)
    r->strong++;
  else
    r->weak++;
}

void ref_acquire_weak(struct ref *r) { r->weak++; }

void ref_release_weak(struct ref *r) {
  r->weak--;
  if(!(r->strong+r->weak)) { run_cbs(r->on_free); }
}

void ref_release(struct ref *r) {
  if(r->strong) {
    r->strong--;
    r->weak++;
    if(!r->strong) { run_cbs(r->on_release); }
  }
  ref_release_weak(r);
}

static void release_i(void *x) { ref_release((struct ref *)x); }

void ref_until_release(struct ref *onto,struct ref *from) {
  ref_acquire(onto);
  ref_on_release(from,release_i,onto);
}

void ref_until_free(struct ref *onto,struct ref *from) {
  ref_acquire(onto);
  ref_on_free(from,release_i,onto);
}

#define TMPXNUM 6
int lock_path(char *path) {
  char *tmpl,*when;
  int len,fd,ret;

  len = strlen(path);
  tmpl = safe_malloc(len+TMPXNUM+1);
  memcpy(tmpl,path,len);
  memset(tmpl+len,'X',TMPXNUM);
  tmpl[len+TMPXNUM] = '\0';
  fd = mkstemp(tmpl);
  if(fd==-1) { return -1; }
  ret = link(tmpl,path);
  if(ret && errno==EEXIST) { ret = 1; }
  // XXX not a lot we can do if these fail, but should include diagnostics
  when = make_string("%"PRId64,microtime());
  write_all(fd,when,strlen(when)+1);
  free(when);
  close(fd);
  unlink(tmpl);
  free(tmpl);
  return ret;
}

int unlock_path(char *path) {
  return unlink(path);  
}

struct fa_data {
  int fd;
  void (*cb)(void *);
  void *priv; 
};

static void * fsync_async_thread(void *data) {
  struct fa_data *fad = (struct fa_data *)data;

  fsync(fad->fd);
  fad->cb(fad->priv);
  free(fad);
  return 0;
}

void fsync_async(int fd,void (*cb)(void *),void *priv) {
  pthread_t thread;
  pthread_attr_t thread_attr;
  struct fa_data *fad;

  fad = safe_malloc(sizeof(struct fa_data));
  fad->fd = fd;
  fad->cb = cb;
  fad->priv = priv;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr,PTHREAD_CREATE_DETACHED);
  pthread_create(&thread,&thread_attr,fsync_async_thread,fad);
  pthread_attr_destroy(&thread_attr); 
}

void dirbasename(char *filename,char **dir,char **base) {
  char *p;

  p = strrchr(filename,'/');
  if(p) {
    if(dir) { *dir = strndup(filename,p-filename); }
    if(base) { *base = strdup(p+1); }
  } else {
    if(dir) { *dir = strdup("."); }
    if(base) { *base = strdup(filename); }
  }
}

int only_create(char *filename) {
  int fd;

  fd = open(filename,O_WRONLY|O_CREAT|O_EXCL);
  if(fd!=-1) {
    close(fd);
    return 0;
  }
  return errno;
}

#ifndef UTIL_MISC_H
#define UTIL_MISC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

typedef void (*type_free_cb)(void *target,void *priv);

#include "array.h"

int64_t microtime(void);

void die(const char *str) __attribute__ ((__noreturn__));
char * gcwd(void);

void * safe_malloc(size_t s);
void * safe_realloc(void *p,size_t s);
char * make_string(const char *fmt,...)
  __attribute__ ((__format__ (__printf__,1,2)));
char * make_string_v(const char *fmt,va_list ap)
  __attribute__ ((__format__ (__printf__,1,0)));

char * safe_readlink(char *);
char * self_path(void);

void fsync_async(int fd,void (*cb)(void *),void *);

#if __GNUC__ >= 3
#define if_rare(pred) if(__builtin_expect((pred),0))
#define if_common(pred) if(__builtin_expect((pred),1))
#else
#define if_rare(pred)   if(pred)
#define if_common(pred) if(pred)
#endif

char * iso_localtime(time_t t);
int write_all(int fd,char *buf,size_t count);
int read_all(int fd,char *buf,size_t count);
int read_file(char *filename,char **out);
int write_file(char *filename,char *out);

struct safe_passwd {
  struct passwd *p;
  char *buf;
};

struct safe_passwd * safe_getpwnam(char *c);
void safe_passwd_free(struct safe_passwd *p);

struct safe_group {
  struct group *g;
  char *buf;
};

struct safe_group * safe_getgrnam(char *c);
void safe_group_free(struct safe_group *g);

char * strdupcatnfree(char *a,...);
char * trim_start(char *target,char *duds,int freep);
char * trim_end(char *target,char *duds,int freep);

char * fgetline(FILE *f,int maxlen);

typedef void (*ref_fn)(void *);

struct refcb;

struct ref {
  int strong,weak;
  void *priv;
  struct refcb *on_release,*on_free;
  struct array *frees;
};

void ref_on_free(struct ref *r,ref_fn cb,void *data);
void ref_on_release(struct ref *r,ref_fn cb,void *data);
void ref_add_unalloc(struct ref *r,void *v);
void ref_create(struct ref *r);
void ref_acquire(struct ref *r);
void ref_acquire_weak(struct ref *r);
void ref_release_weak(struct ref *r);
void ref_release(struct ref *r);
void ref_until_release(struct ref *dst,struct ref *src);
void ref_until_free(struct ref *dst,struct ref *src);

void type_free(void *target,void *priv);

int lock_path(char *path);
int unlock_path(char *path);
void dirbasename(char *filename,char **dir,char **base);
int only_create(char *filename);

#endif

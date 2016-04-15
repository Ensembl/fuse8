#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "hits.h"
#include "interface.h"
#include "source.h"
#include "running.h"
#include "config.h"
#include "util/logging.h"
#include "util/rotate.h"
#include "util/assoc.h"
#include "jpf/jpf.h"

CONFIG_LOGGING(config)

// XXX should probably not live here
struct log_dest {
  int fd;
  char *filename;
};

static struct log_dest *main_log=0,*requests_log=0,*stats_log=0;

static void configure_logging_levels(struct jpf_value *raw) {
  enum log_level level;
  int i;

  if(!raw) { log_debug(("No logging levels section")); return; }
  for(i=0;i<raw->v.assoc.len;i++) {
    level = log_get_level_by_name(raw->v.assoc.v[i]->v.string);
    if(level == LOG_CHECK) {
      log_error(("Bad logging level '%s', using DEBUG",raw->v.assoc.v[i]->v.string));
      level = LOG_DEBUG;
    }
    log_set_level(raw->v.assoc.k[i],level);
  }
}

// XXX proper error handling
static struct log_dest * create_log_dest(struct jpf_value *raw) {
  struct log_dest *out;
  struct jpf_value *v;
  int fd;

  out = safe_malloc(sizeof(struct log_dest));
  out->fd = -1;
  out->filename = 0;
  v = jpfv_lookup(raw,"fd");
  if(v) {
    if(jpfv_int(v,&fd)) { goto error; }
    out->fd = fd;
    return out;
  }
  v = jpfv_lookup(raw,"filename");
  if(v) {
    out->filename = strdup(v->v.string);
    return out;
  }
  log_error(("Missing filename/fd spec"));
  /* fallthrough */
error:
  free(out);
  return 0;
}

static void free_log_dest(struct log_dest *ld) {
  if(!ld) { return; }
  if(ld->filename) { free(ld->filename); }
  free(ld);
}

static int open_log_dest(struct log_dest *ld) {
  int fd;

  if(!ld) { goto error; }
  if(ld->fd!=-1) {
    return ld->fd;
  }
  if(ld->filename) {
    fd = open(ld->filename,O_WRONLY|O_APPEND|O_CREAT,0666);
    if(fd==-1) {
      log_error(("Cannot open '%s'",ld->filename));
      goto error;
    }
    log_debug(("File '%s' fd is %d",ld->filename,fd));
    return fd;
  }
  /* fallthrough */
error:
  log_error(("Cannot open log file"));
  return -1;
}

static int rotate_log_dest(struct running *rr,struct log_dest *ld) {
  if(!ld->filename) {
    log_warn(("Cannot rotate output to file descriptor"));
    return ld->fd;
  }
  if(ld->fd!=-1) {
    if(close(ld->fd)) {
      log_error(("Close of fd=%d failed during log rotation, errno=%d",
                ld->fd,errno));
    }
  }
  ld->fd=-1;
  if(rotate_log(rr->rot,ld->filename)) {
    log_error(("Could not rotate log file '%s'",ld->filename));
  }
  return open_log_dest(ld);
}

void rotate_logs(struct running *rr) {
  struct hits *h;
  int fd;

  h = sl_get_hits(rr->sl);
  if(h && requests_log) {
    fd = rotate_log_dest(rr,requests_log);
    hits_reset_fd(h,fd); 
  }
  if(stats_log) {
    rr->stats_fd = rotate_log_dest(rr,stats_log);
  }
  if(main_log) {
    logging_fd(rotate_log_dest(rr,main_log));
  }
}

static void configure_logging_dest(struct jpf_value *raw) {
  int fd;

  if(!raw) {
    goto error;
    log_debug(("No logging dest section, using stderr"));
    logging_fd(2);
    return;
  }
  main_log = create_log_dest(raw);
  fd = open_log_dest(main_log);
  if(fd==-1) { goto error; }
  logging_fd(fd);
  return;
  error:
    log_error(("Bad logging dest section, defaulting to stderr"));
    logging_fd(2);
}

static void configure_logging(struct running *rr,struct jpf_value *raw) {
  struct jpf_value *v;
  int age;

  if(!raw) { log_debug(("No logging section")); return; }
  log_debug(("Configuring logging"));
  configure_logging_levels(jpfv_lookup(raw,"levels"));
  configure_logging_dest(jpfv_lookup(raw,"dest"));
  v = jpfv_lookup(raw,"max-old");
  if(v) {
    if(!jpfv_int(v,&age)) {
      rotator_max_old(rr->rot,age);
    } else {
      log_error(("Bad max-old: ignoring"));
    }
  }
}

// XXX if log cannot be opened?
static void configure_stats(struct running *rr,struct jpf_value *raw) {
  int val;

  if(!raw) { return; }
  log_debug(("configuring stats"));
  stats_log = create_log_dest(raw);
  rr->stats_fd = open_log_dest(stats_log);
  rr->stat_timer_interval.tv_usec = 0;
  switch(jpfv_int(jpfv_lookup(raw,"interval"),&val)) {
  case -2:
    rr->stat_timer_interval.tv_sec = 60;
    break;
  case -1:
    die("Bad interval");
    break;
  default:
    rr->stat_timer_interval.tv_sec = val;
  }
  log_debug(("stat timer interval is %lds",rr->stat_timer_interval.tv_sec));
}

static void configure_hits(struct running *rr,struct jpf_value *raw) {
  int fd,val;

  if(!raw) { return; }
  log_debug(("configuring hits log"));
  requests_log = create_log_dest(raw);
  fd = open_log_dest(requests_log);
  switch(jpfv_int(jpfv_lookup(raw,"interval"),&val)) {
  case -2: val = 60; break;
  case -1: die("Bad interval"); break;
  default: break;
  }
  log_debug(("hits log fd=%d interval=%ds",fd,val));
  sl_set_hits(rr->sl,hits_new(rr->eb,fd,val));
}

// XXX don't rely on jpf ordering
static void configure_source(struct running *rr,char *name,
                             struct jpf_value *conf) {
  struct jpf_value *type,*v;
  struct source *src;
  src_create_fn creator;
  int timeout;

  type = jpfv_lookup(conf,"type");
  if(!type) {
    log_error(("Missing type key"));
    exit(1);
  }
  creator = assoc_lookup(rr->src_shop,type->v.string);
  if(!creator) {
    log_error(("No such source type '%s'",name));
    exit(1);
  }
  log_debug(("Creating source of type '%s'",name));
  src = creator(rr,conf);
  src_set_name(src,name);
  v = jpfv_lookup(conf,"fail_timeout");
  if(v) {
    if(jpfv_int(v,&timeout)) { die("Bad timeout"); }
    src_set_fails(src,rr->eb,timeout);
  }
  array_insert(rr->src,src);
  sl_add_src(rr->sl,src);
  src_release(src); 
  log_debug(("Created source of type '%s'",name));
}

static void configure_interface(struct running *rr,char *name,
                                struct jpf_value *conf) {
  struct interface *ic;
  ic_create_fn creator;

  creator = assoc_lookup(rr->ic_shop,name);
  if(!creator) {
    log_error(("No such interface type '%s'",name));
    exit(1);
  }
  log_debug(("Creating interface of type '%s'",name));
  ic = creator(rr,conf);
  array_insert(rr->icc,ic);
  log_debug(("Created interface of type '%s'",name));
}

static void configure_sources(struct running *rr,struct jpf_value *raw) {
  int i;

  if(!raw) { log_debug(("No sources section")); return; }
  log_debug(("configuring sources"));
  for(i=0;i<raw->v.assoc.len;i++) {
    configure_source(rr,raw->v.assoc.k[i],raw->v.assoc.v[i]);
  }
  sl_open(rr->sl);
}

static void configure_interfaces(struct running *rr,
                                 struct jpf_value *raw) {
  int i;

  if(!raw) { log_debug(("No interfaces section")); return; }
  log_debug(("configuring interfaces"));
  for(i=0;i<raw->v.assoc.len;i++) {
    configure_interface(rr,raw->v.assoc.k[i],raw->v.assoc.v[i]);
  }
}

// XXX defaults via jcf
int load_config(struct running *rr,char *path) {
  struct lexer lx;
  struct jpf_value *raw,*val;
  char *errors,*pid;

  log_info(("Loading config from '%s'",path));
  jpf_lex_filename(&lx,path);
  errors = jpf_dfparse(&lx,&raw);
  if(errors) {
    log_error(("Could not parse config:\n%s",errors));
    free(errors);
    config_finished();
    return 1;
  }
  configure_logging(rr,jpfv_lookup(raw,"logging"));
  configure_stats(rr,jpfv_lookup(raw,"stats"));
  configure_hits(rr,jpfv_lookup(raw,"hits"));
  configure_sources(rr,jpfv_lookup(raw,"sources"));
  configure_interfaces(rr,jpfv_lookup(raw,"interfaces"));
  val = jpfv_lookup(raw,"pidfile");
  if(val) {
    pid = make_string("%d",getpid());
    write_file(val->v.string,pid);
    free(pid);
  }
  jpfv_free(raw);
  return 0;
}

void config_finished(void) {
  free_log_dest(stats_log);
  free_log_dest(requests_log);
  free_log_dest(main_log);
  stats_log = 0;
  requests_log = 0;
  main_log = 0;
}

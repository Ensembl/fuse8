#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "hits.h"
#include "interface.h"
#include "source.h"
#include "running.h"
#include "util/logging.h"
#include "util/assoc.h"
#include "jpf/jpf.h"

CONFIG_LOGGING(config)

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
static int extract_fd(struct jpf_value *raw,int flags) {
  struct jpf_value *v;
  int fd;

  v = jpfv_lookup(raw,"fd");
  if(v) {
    if(jpfv_int(v,&fd)) { return -1; }
    return fd;
  }
  v = jpfv_lookup(raw,"filename");
  if(v) {
    fd = open(v->v.string,flags,0666);
    if(fd==-1) {
      log_error(("Cannot open '%s'",v->v.string));
      return -1;
    }
    log_debug(("File '%s' fd is %d",v->v.string,fd));
    return fd;
  }
  log_error(("Missing filename/fd spec"));
  return -1;
}

static void configure_logging_dest(struct jpf_value *raw) {
  int fd;

  if(!raw) {
    goto error;
    log_debug(("No logging dest section, using stderr"));
    logging_fd(2);
    return;
  }
  fd = extract_fd(raw,O_WRONLY|O_APPEND|O_CREAT);
  if(fd==-1) { goto error; }
  logging_fd(fd);
  return;
  error:
    log_error(("Bad logging dest section, defaulting to stderr"));
    logging_fd(2);
}

static void configure_logging(struct jpf_value *raw) {
  if(!raw) { log_debug(("No logging section")); return; }
  log_debug(("Configuring logging"));
  configure_logging_levels(jpfv_lookup(raw,"levels"));
  configure_logging_dest(jpfv_lookup(raw,"dest"));
}

static void configure_stats(struct running *rr,struct jpf_value *raw) {
  int val;

  if(!raw) { return; }
  log_debug(("configuring stats"));
  rr->stats_fd = extract_fd(raw,O_WRONLY|O_APPEND|O_CREAT);
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
  fd = extract_fd(raw,O_WRONLY|O_APPEND|O_CREAT);
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
  struct jpf_value *raw;
  char *errors;

  log_info(("Loading config from '%s'",path));
  jpf_lex_filename(&lx,path);
  errors = jpf_dfparse(&lx,&raw);
  if(errors) {
    log_error(("Could not parse config:\n%s",errors));
    free(errors);
    return 1;
  }
  configure_logging(jpfv_lookup(raw,"logging"));
  configure_stats(rr,jpfv_lookup(raw,"stats"));
  configure_hits(rr,jpfv_lookup(raw,"hits"));
  configure_sources(rr,jpfv_lookup(raw,"sources"));
  configure_interfaces(rr,jpfv_lookup(raw,"interfaces"));
  jpfv_free(raw);
  return 0;
}

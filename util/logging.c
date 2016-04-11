#include <string.h>
#include <errno.h>
#include "misc.h"
#include "assoc.h"
#include "array.h"
#include "logging.h"
#include "strbuf.h"

static char * (level_str[]) =
  {"CHECK","RECHECK","DEBUG","INFO","WARN","ERROR",0};

struct defer {
  char *msg,*type;
  enum log_level level;
  struct defer *next;
};

static struct assoc * levels = 0;
static struct array * caches = 0;
static struct defer * defer = 0;
static struct defer ** defer_last = &defer;
static int log_fd = -1;

// XXX thread safe
// XXX logging support for die

CONFIG_LOGGING(logging);

static void log_init(void) {
  levels = assoc_create(type_free,0,type_free,0);
  caches = array_create(0,0);
}

enum log_level log_get_level_by_name(char *name) {
  int i;

  for(i=0;level_str[i];i++) {
    if(!strcmp(name,level_str[i])) { return i; }
  }
  log_error(("No such logging level '%s'",name));
  return LOG_CHECK;
}

static enum log_level get_level(char *type) {
  enum log_level *levelp;

  levelp = assoc_lookup(levels,type);
  if(levelp) { return *levelp; }
  levelp = assoc_lookup(levels,"");
  if(levelp) { return *levelp; }
  return LOG_INFO;
}

static void send(char *type,enum log_level level,char *str) {
  struct defer *d;

  if(log_fd == -1) {
    d = safe_malloc(sizeof(struct defer));
    d->msg = strdup(str);
    d->type = strdup(type);
    d->level = level;
    d->next = 0;
    *defer_last = d;
    defer_last = &(d->next);
  } else {
    if(write_all(log_fd,str,strlen(str))) {
      /* Do our best */
      fprintf(stderr,"Log file failed (%d)!\n%s",errno,str);
      log_fd = 2;
    }
  }
}

static void replay(void) {
  struct defer *d;
  enum log_level level;

  while(defer) {
    level = get_level(defer->type);
    if(defer->level>=level) {
      send(defer->type,defer->level,defer->msg);
    }
    free(defer->type);
    free(defer->msg);
    d = defer->next;
    free(defer);
    defer = d;
  }
}

void logging_fd(int fd) {
  if(!levels) { log_init(); }
  log_debug(("logging fd set to %d",fd));
  if(log_fd == -1) {
    log_info(("logging fd established: replaying logs"));
    log_fd = fd;
    replay();
  } else {
    log_fd = fd;
  }
}

void logging_done(void) {
  if(!levels) { log_init(); }
  if(log_fd==-1) {
    log_warn(("No logging fd set before exit: sent to stderr"));
    logging_fd(2);
  }
  if(levels) { assoc_release(levels); }
  if(caches) { array_release(caches); }
}

static void invalidate(void) {
  int i;

  for(i=0;i<array_length(caches);i++) {
    *((enum log_level *)array_index(caches,i)) = LOG_RECHECK;
  }
}

void log_set_level(char *type,enum log_level level) {
  enum log_level *levelp;

  log_info(("Setting log level to %s for %s",
           level_str[level],*type?type:"DEFAULT"));
  if(!levels) { log_init(); }
  levelp = safe_malloc(sizeof(enum log_level));
  *levelp = level;
  assoc_set(levels,strdup(type),levelp);
  invalidate();
}

enum log_level log_level(char *type,enum log_level *file_level) {
  if(!levels) { log_init(); }
  if(log_fd==-1) { return LOG_CHECK; } /* Early stages, force on */
  if(*file_level == LOG_CHECK) { array_insert(caches,file_level); }
  if(*file_level == LOG_CHECK || *file_level == LOG_RECHECK) {
    *file_level = get_level(type);
  }
  return *file_level; 
}

void log_message(char *type,enum log_level *file_level,
                 enum log_level msg_level,char *msg,char *file,int line) {
  char *text,*time;

  if(!levels) { log_init(); }
  if(msg_level<log_level(type,file_level)) { return; }
  time = iso_localtime(0);
  if(msg_level == LOG_DEBUG) {
    text = make_string("[%s] DEBUG (%s:%s,%d) %s\n",
                       time,type,file,line,msg);
  } else {
    text = make_string("[%s] %s %s\n",time,level_str[msg_level],msg);
  }
  free(time);
  send(type,msg_level,text);
  free(text);
}

int get_logging_fd(void) { return log_fd; }

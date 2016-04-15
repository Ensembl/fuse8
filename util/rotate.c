#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "compressor.h"
#include "logging.h"
#include "rotate.h"
#include "misc.h"
#include "assoc.h"

CONFIG_LOGGING(rotate)

struct rotator {
  struct ref r;
  struct compressor *c;
  int max_old;
};

#define MATCH_IGNORE (-1)
#define MATCH_DELETE (-2)

static int matching_file(struct rotator *rr,char *pattern,
                         char *filename,char **new_filename) {
  char *more;
  unsigned long num;

  if(strncmp(pattern,filename,strlen(pattern)))
    return MATCH_IGNORE; /* doesn't match basename */
  filename += strlen(pattern);
  if(!*filename) {
    /* exact match */
    *new_filename = make_string("%s.0",pattern);
    return 0;
  } else {
    /* archive? */
    if(*(filename++) != '.')
      return MATCH_IGNORE; /* isn't followed by dot */
    if(!*filename)
      return MATCH_IGNORE; /* dot is last character */
    num = strtoul(filename,&more,10);
    if(filename==more)
      return MATCH_IGNORE; /* not numbers after dot */
    if(rr->max_old>=0 && num>=rr->max_old)
      return MATCH_DELETE; /* delete it */
    *new_filename = make_string("%s.%lu%s",pattern,num+1,more);
    return num+1;
  }
}

static int rename_file(struct rotator *rr,char *dirname,
                       char * oldname,char *newname) {
  char *from,*to;

  to = make_string("%s/%s",dirname,newname);
  if(only_create(to)) {
    log_error(("%s already exists",to));
    free(to);
    return 1;
  }
  from = make_string("%s/%s",dirname,oldname);
  if(rename(from,to)) {
    log_error(("During log rotation cannot rename %s -> %s : %d\n",
              from,to,errno));
    return 1;
  }
  log_debug(("renamed %s -> %s",from,to));
  if(strlen(to) < 3 || strcmp(to+strlen(to)-3,".gz")) {
    log_debug(("compressing %s",to));
    compressor_add(rr->c,to);
  }
  free(from);
  free(to);
  return 0;
}

static int sort_names(const void *a,const void *b,void *c) {
  struct assoc *orders = (struct assoc *)c;
  int *aa,*bb;

  aa = assoc_lookup(orders,*(char **)a);
  bb = assoc_lookup(orders,*(char **)b);
  if(!aa) { return 1; }
  if(!bb) { return -1; }
  if(*aa<*bb) { return 1; }
  if(*aa>*bb) { return -1; }
  return 0;
}

int rotate_log(struct rotator *rr,char *filename) {
  char *dir,*base,*oldname,*newname;
  DIR *dirh;
  struct dirent des,*de;
  struct array *names;
  struct assoc *changes,*orders;
  int order,i,n;

  dirbasename(filename,&dir,&base);
  log_debug(("Rotating in '%s'",dir));
  dirh = opendir(dir);
  if(!dirh) {
    log_error(("Could not rotate log file"));
    return 1;
  }
  names = array_create(type_free,0);
  changes = assoc_create(0,0,type_free,0);
  orders = assoc_create(0,0,type_free,0);
  while(1) {
    if(readdir_r(dirh,&des,&de)) {
      log_error(("Problem reading directory during log rotation"));
      break;
    }
    if(!de) { break; }
    order = matching_file(rr,filename,de->d_name,&newname);
    if(order == MATCH_IGNORE) { continue; }
    if(order == MATCH_DELETE) {
      log_debug(("delete %s",de->d_name));
      unlink(de->d_name);
    } else {
      oldname = strdup(de->d_name);
      array_insert(names,oldname);
      assoc_set(changes,oldname,newname);
      assoc_set(orders,oldname,make_int(order));
    }
  }
  if(closedir(dirh)) {
    log_error(("Could not closedir during log rotation!"));
    /* But continue: what else to do? */
  }
  array_sort(names,sort_names,orders);
  n = array_length(names);
  for(i=0;i<n;i++) {
    oldname = array_index(names,i);
    newname = assoc_lookup(changes,oldname);
    log_warn(("name=%s -> %s",oldname,newname));
    rename_file(rr,dir,oldname,newname);
  }
  assoc_release(changes);
  assoc_release(orders);
  array_release(names);
  free(dir);
  free(base);
  return 0;
}

static void rr_release(void *data) {
  struct rotator *rr = (struct rotator *)data;

  compressor_release(rr->c);
}

static void rr_free(void *data) {
  struct rotator *rr = (struct rotator *)data;

  free(rr);
}


struct rotator * rotator_create() {
  struct rotator *rr;
  struct background *b;

  rr = safe_malloc(sizeof(struct rotator));
  ref_create(&(rr->r));
  ref_on_release(&(rr->r),rr_release,rr);
  ref_on_free(&(rr->r),rr_free,rr);
  rr->c = compressor_create();
  rr->max_old = -1;
  b = compressor_background(rr->c);
  background_start(b);
  return rr;
}

void rotator_release(struct rotator *rr) {
  struct background *b;

  log_debug(("rotate released"));
  b = compressor_background(rr->c);
  background_finish(b);
  ref_release(&(rr->r));
}

void rotator_max_old(struct rotator *rr,int max) {
  log_debug(("setting max old log fix to %d",max));
  rr->max_old = max;
}

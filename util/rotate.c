#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "logging.h"
#include "rotate.h"
#include "misc.h"
#include "assoc.h"

CONFIG_LOGGING(rotate)

static char * matching_file(char *pattern,char *filename) {
  char *more;
  unsigned long num;

  if(strncmp(pattern,filename,strlen(pattern)))
    return 0; /* doesn't match basename */
  filename += strlen(pattern);
  if(!*filename) {
    /* exact match */
    return make_string("%s.0",pattern);
  } else {
    /* archive? */
    if(*(filename++) != '.')
      return 0; /* isn't followed by dot */
    if(!*filename)
      return 0; /* dot is last character */
    num = strtoul(filename,&more,10);
    if(filename==more)
      return 0; /* not numbers after dot */
    return make_string("%s.%lu%s",pattern,num+1,more);
  }
}

static int rename_files(char * dirname,struct assoc *changes) {
  struct assoc_iter e;
  char *from,*to;
  int n = 0;

  associ_start(changes,&e);
  while(associ_next(&e)) {
    to = make_string("%s/%s",dirname,(char *)associ_value(&e));
    if(only_create(to)) {
      log_debug(("cannot -> %s yet",to));
      free(to);
      continue;
    }
    from = make_string("%s/%s",dirname,associ_key(&e));
    if(rename(from,to)) {
      log_error(("During log rotation cannot rename %s -> %s : %d\n",
                from,to,errno));
      continue;
    }
    log_debug(("renamed %s -> %s",from,to));
    assoc_set(changes,associ_key(&e),0);
    n++;
    free(from);
    free(to);
  }
  return n;
}

int rotate_log(char *filename) {
  char *dir,*base,*newname;
  DIR *dirh;
  struct dirent des,*de;
  struct assoc *changes;

  dirbasename(filename,&dir,&base);
  log_debug(("Rotating in '%s'",dir));
  dirh = opendir(dir);
  if(!dirh) {
    log_error(("Could not rotate log file"));
    return 1;
  }
  changes = assoc_create(type_free,0,type_free,0);
  while(1) {
    if(readdir_r(dirh,&des,&de)) {
      log_error(("Problem reading directory during log rotation"));
      break;
    }
    if(!de) { break; }
    newname = matching_file(filename,de->d_name);
    if(!newname) { continue; }
    log_warn(("file=%s -> %s\n",de->d_name,newname)); 
    assoc_set(changes,strdup(de->d_name),newname);
  }
  if(closedir(dirh)) {
    log_error(("Could not closedir during log rotation!"));
    /* But continue: what else to do? */
  }
  while(rename_files(dir,changes))
    ;
  if(assoc_len(changes)) {
    log_error(("Rotation failed: filename loop. Impossible?"));
  } 
  assoc_release(changes);
  free(dir);
  free(base);
  return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include "util/path.h"
#include "util/logging.h"
#include "running.h"

CONFIG_LOGGING(main);

// XXX should be specifiable in build system
/* Where to look for config */
char * (conf_dirs[]) = {
  "", /* ie exe-relative */
  "/usr/local",
  "/etc",
  "/etc/fuse8",
  0
};

char * (conf_fns[]) = {
  "config.jpf",
  0
};

int main(int argc,char **argv) {
  int c;
  char *conf_file = 0,**dir,**fn,*self,*rdir;

  while((c = getopt(argc,argv,"c:"))!=-1) {
    switch(c) {
    case 'c':
      conf_file = optarg; 
      break;
    case '?':
      if(optopt == 'c') {
        fprintf(stderr,"Option -%c requires an argument.\n",optopt);
      } else if(isprint(optopt)) {
        fprintf(stderr,"Unknown option `-%c'.\n",optopt);
      } else {
        fprintf(stderr,"Unknown option character `\\x%x'.\n",optopt);
      }
      return 1;
    default:
      abort();
    }
  }
  if(!conf_file) {
    /* guess */
    self = self_path();
    for(dir=conf_dirs;*dir;dir++) {
      if(**dir) {
        rdir = strdup(*dir);
      } else {
        path_separate(self,&rdir,0);
      }
      for(fn=conf_fns;*fn;fn++) {
        conf_file = make_string("%s/%s",rdir,*fn);
        log_debug(("Looking for '%s'",conf_file));
        if(path_exists(conf_file)) {
          run(conf_file);
          free(rdir);
          free(self);
          free(conf_file);
          logging_done();
          return 0;
        }
        free(conf_file);
      }
      free(rdir);
    }
    free(self);
    log_error(("Could not find config file at usual places"));
    logging_done();
    return 1;
  } else {
    run(conf_file);
  }
  logging_done();
  return 0;
}

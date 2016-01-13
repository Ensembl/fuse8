#include <string.h>
#include <unistd.h>
#include "path.h"
#include "misc.h"

// XXX safe strdup
// XXX split this monster

void path_separate(char *in,char **dir_out,char **file_out) {
  char *full,*sep,*dir,*filename;

  full = strdup(in);
  /* strip trailing /s */
  while(1) {
    sep = strrchr(full,'/');
    if(!sep || *(sep+1)!='\0') { break; }
    *sep = '\0';
  }
  sep = strrchr(full,'/');
  if(!sep) {
    filename = full;
    dir = strdup("");
  } else {
    *sep = '\0';
    filename = sep+1;
    dir = strdup(full);
  }
  if(dir_out) { *dir_out = strdup(dir); }
  if(file_out) { *file_out = strdup(filename); }
  free(dir);
  free(full);
}

void to_dir_file(char *in,char **dir_out,char **file_out) {
  char *full,*sep,*dir,*filename,*cwd;

  full = strdup(in);
  /* strip trailing /s */
  while(1) {
    sep = strrchr(full,'/');
    if(!sep || *(sep+1)!='\0') { break; }
    *sep = '\0';
  }
  sep = strrchr(full,'/');
  if(!sep) {
    filename = full;
    dir = strdup("");
  } else {
    *sep = '\0';
    filename = sep+1;
    dir = strdup(full);
  }
  if(*dir!='/') {
    cwd = gcwd();
    dir = strdupcatnfree(cwd,"/",dir,0,cwd,dir,0);
  }
  sep = strrchr(dir,'/');
  if(!sep || *(sep+1)!='\0') {
    dir = strdupcatnfree(dir,"/",0,dir,0);
  }
  if(dir_out) { *dir_out = strdup(dir); }
  if(file_out) { *file_out = strdup(filename); }
  free(dir);
  free(full);
}

int path_exists(char *in) {
 return !access(in,F_OK);
}

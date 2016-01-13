#include <stdio.h>
#include <unistd.h>

#include "running.h"

int main(int argc,char **argv) {
  int c;
  char *conf_file = 0;

  while((c = getopt(argc,argv,"c:"))!=-1) {
    switch(c) {
    case 'c':
      conf_file = optarg; 
      break;
    case '?':
      if(optopt == 'c') {
        fprintf (stderr,"Option -%c requires an argument.\n",optopt);
      } else if (isprint (optopt)) {
        fprintf (stderr,"Unknown option `-%c'.\n",optopt);
      } else {
        fprintf (stderr,"Unknown option character `\\x%x'.\n",optopt);
      }
      return 1;
    default:
      abort();
    }
  }

  run(conf_file);
  return 0;
}

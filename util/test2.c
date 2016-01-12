#include <stdio.h>
#include <unistd.h>
#include "misc.h"

int main() {
  int r;

  r = lock_path("/tmp/x");
  fprintf(stderr,"r=%d\n",r);
  r = lock_path("/tmp/x");
  fprintf(stderr,"r=%d\n",r);
  sleep(60);
  unlock_path("/tmp/x");
  return 0;
}

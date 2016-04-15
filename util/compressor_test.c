#include <unistd.h>
#include "compressor.h"
#include "background.h"
#include "file.h"
#include "misc.h"
#include "logging.h"

#define SIZE (700*1024)

int main() {
  struct compressor *cc;
  struct background *cc_bgd;
  unsigned char *rnd1,*rnd2;

  log_set_level("",LOG_DEBUG);
  logging_fd(2);
  cc = compressor_create();
  cc_bgd = compressor_background(cc);

  rnd1 = malloc(SIZE);
  rnd2 = malloc(SIZE);

  make_random(rnd1,SIZE);
  make_random(rnd2,SIZE);

  if(unlink("ct-a.gz")) { end(); }
  if(unlink("ct-b.gz")) { end(); }
  if(file_put_contents("ct-a",rnd1,SIZE)) { end(); }
  if(file_put_contents("ct-b",rnd2,SIZE)) { end(); }

  compressor_add(cc,"ct-a");
  background_start(cc_bgd);
  compressor_add(cc,"ct-b");

  background_finish(cc_bgd);
  compressor_release(cc);

  if(unlink("ct-a")) { end(); }
  if(unlink("ct-b")) { end(); }
  free(rnd1);
  free(rnd2);

  logging_done();
  return 0;
}

#include <stdlib.h>
#include <stdint.h>

#include "misc.h"
#include "strbuf.h"
#include "ranges.h"

/* [1,10],[20,30]
 * [4,27]
 * [0,4096),[31744,65536)
 * [0,62464)
 * (empty)
 */

int main() {
  struct ranges a,b,d;
  char *c;

  ranges_init(&a);
  ranges_add(&a,1,10);
  ranges_add(&a,20,30);
  ranges_add(&a,10,16);
  ranges_add(&a,12,21);
  ranges_remove(&a,10,20);
  c = ranges_print(&a);
  printf("%s\n",c);
  free(c);
  ranges_init(&b);
  ranges_add(&b,0,5);
  ranges_add(&b,7,21);
  ranges_add(&b,25,30);
  ranges_difference(&a,&b);
  ranges_init(&d);
  ranges_add(&d,4,5);
  ranges_add(&d,7,23);
  ranges_add(&d,25,27);
  ranges_merge(&a,&d);
  c = ranges_print(&a);
  printf("%s\n",c);
  free(c);
  ranges_free(&a);
  ranges_free(&b);
  ranges_free(&d);
  /**/
  ranges_init(&a);
  ranges_add(&a,10,1356);
  ranges_add(&a,2048,4096);
  ranges_add(&a,32700,65530);
  ranges_blockify_expand(&a,1024);
  c = ranges_print(&a);
  printf("%s\n",c);
  free(c);
  ranges_free(&a);
  /**/
  ranges_init(&a);
  ranges_add(&a,0,63350);
  ranges_blockify_reduce(&a,1024);
  c = ranges_print(&a);
  printf("%s\n",c);
  free(c);
  ranges_free(&a);
  /**/
  ranges_init(&a);
  ranges_add(&a,0,1023);
  ranges_blockify_reduce(&a,1024);
  c = ranges_print(&a);
  printf("%s\n",c);
  free(c);
  ranges_free(&a);
}

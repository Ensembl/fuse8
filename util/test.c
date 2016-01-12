#include <stdlib.h>
#include "misc.h"
#include "assoc.h"
#include "array.h"

void test_assoc(void) {
  struct assoc *a;
  struct assoc_iter e;
  int i;
  char *k,*v,*k2,*v2;

  a = assoc_create(type_free,0,type_free,0);
  for(i=0;i<100;i++) {
    k = make_string("key-%d",i);
    v = make_string("value-%d",i);
    assoc_set(a,k,v);
  }
  for(i=0;i<50;i++) {
    k = make_string("key-%d",i);
    assoc_set(a,k,0);
    free(k);
  }
  for(i=0;i<100;i++) {
    k = make_string("key-%d",i);
    v = assoc_lookup(a,k);
    free(k);
    if(i<50 && v) { die("assoc test failed 1\n"); }
    if(i>=50 && !v) { die("assoc test failed 2\n"); }
  }
  for(i=0;i<50;i++) {
    k = make_string("key-%d",i);
    v = make_string("value2-%d",i);
    assoc_set(a,k,v);
  }
  for(i=0;i<50;i++) {
    k = make_string("key-%d",i);
    v = make_string("value-%d",i);
    assoc_set(a,k,v); 
  }
  associ_start(a,&e);
  i = 0;
  while(associ_next(&e)) {
    k = associ_key(&e);
    v = associ_value(&e);
    if(strncmp("key-",k,4)) { die("assoc test failed 3\n"); }
    v2 = make_string("value-%s",k+4);
    if(strcmp(v2,v)) { die("assoc test failed 4\n"); }
    free(v2);
  }
  assoc_release(a);
}

void test_array(void) {
  struct array *a;
  int i;
  char *msg;

  a = array_create(type_free,0);
  for(i=0;i<50;i++) {
    msg = make_string("idx-%d",i);
    array_insert(a,msg);
  }
  for(i=0;i<49;i++) {
    if(i) {
      array_set_nf(a,i,array_index(a,i+1));
    } else {
      array_set(a,i,array_index(a,i+1));
    }
  }
  array_remove_nf(a);
  if(array_length(a)!=49) { die("array test failed 1"); }
  for(i=0;i<49;i++) {
    msg = make_string("idx-%d",i+1);
    if(strcmp(msg,array_index(a,i))) { die("array test failed 2"); }
    free(msg);
  }
  array_release(a);
}

int main() {
  test_assoc();
  test_array();
  return 0;
}

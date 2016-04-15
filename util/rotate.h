#ifndef UTIL_ROTATE_H
#define UTIL_ROTATE_H

struct rotator;

struct rotator * rotator_create();
void rotator_release(struct rotator *r);
int rotate_log(struct rotator *r,char *filename);
void rotator_max_old(struct rotator *r,int max);

#endif

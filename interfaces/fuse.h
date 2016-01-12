#ifndef IF_FUSE_H
#define IF_FUSE_H

#include "../jpf/jpf.h"
#include "../interface.h"
#include "../sourcelist.h"

struct interface * ic_fuse_create(struct sourcelist *sl,char *path);
struct interface * ic_fuse_make(struct running *rr,struct jpf_value *conf);

#endif

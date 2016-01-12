#ifndef INTERFACE_H
#define INTERFACE_H

#include "util/misc.h"
#include "types.h"

struct interface * ic_create(void);
void ic_acquire(struct interface *ic);
void ic_release(struct interface *ic);
void ic_quit(struct interface *ic);
struct ref * ic_ref(struct interface *ic);
void ic_collect(struct interface *ic,int64_t length);
void ic_global_stats(struct interface *ic,struct jpf_value *out);

#endif

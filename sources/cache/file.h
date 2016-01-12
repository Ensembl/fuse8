#ifndef SOURCES_CACHEFILE2_H
#define SOURCES_CACHEFILE2_H

#include "../../source.h"
#include "../../jpf/jpf.h"

struct source * source_cachefile2_make(struct running *rr,
                                       struct jpf_value *conf);

#endif

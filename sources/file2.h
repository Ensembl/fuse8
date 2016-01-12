#ifndef SOURCES_FILE2_H
#define SOURCES_FILE2_H

#include "../running.h"
#include "../jpf/jpf.h"
#include "../syncsource.h"

struct source * source_file2_make(struct running *rr,
                                  struct jpf_value *conf);

#endif

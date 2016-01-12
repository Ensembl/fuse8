#ifndef SOURCES_HTTP_H
#define SOURCES_HTTP_H

#include <event2/event.h>
#include "../../source.h"

struct source * source_http_make(struct running *rr,struct jpf_value *conf);

#endif

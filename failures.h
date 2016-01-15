#ifndef FAILURES_H
#define FAILURES_H

#include <event2/event.h>

struct failures;

void failures_free(struct failures *f);
struct failures * failures_new(struct event_base *eb,uint64_t timeout);
void failures_fail(struct failures *f,char *path);
int failures_check(struct failures *f,char *path);

#endif

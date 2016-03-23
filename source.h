#ifndef SOURCE_H
#define SOURCE_H

#include "jpf/jpf.h"
#include "types.h"
#include "sourcelist.h"

#include <event2/event.h>

/* Sources which are not inherently asynchronous should implement
 * a thread pool and queue using syncsource.
 */

struct source * src_create(char *type);
void src_open(struct source *src);
void src_release(struct source *src);
void src_acquire(struct source *src);
struct ref * src_ref(struct source *src);
void src_set_name(struct source *src,char *name);

/* Internal use */
struct sourcelist * src_sl(struct source *src);
struct source * src_get_next(struct source *src);
void src_collect(struct source *src,int64_t len);
void src_collect_rtime(struct source *src,int64_t rtime);
void src_collect_wtime(struct source *src,int64_t wtime);
void src_collect_error(struct source *src);

void src_global_stats(struct source *src,struct jpf_value *out);

void src_set_fails(struct source *src,struct event_base *eb,
                   int64_t timeout);

int src_path_ok(struct source *src,char *path);
void src_set_failed(struct source *src,char *path);
char * src_type(struct source *src);

#endif

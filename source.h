#ifndef SOURCE_H
#define SOURCE_H

#include "jpf/jpf.h"
#include "types.h"
#include "sourcelist.h"

/* Sources which are not inherently asynchronous should implement
 * a thread pool and queue using syncsource.
 */

struct source * src_create(void);
void src_release(struct source *src);
void src_acquire(struct source *src);
struct ref * src_ref(struct source *src);
void src_set_name(struct source *src,char *name);

/* Only for sources */
void src_close_finished(struct source *src);

/* Internal use */
struct source * src_get_next(struct source *src);
void src_collect(struct source *src,int64_t len);
void src_collect_rtime(struct source *src,int64_t rtime);
void src_collect_wtime(struct source *src,int64_t wtime);
void src_collect_error(struct source *src);

void src_global_stats(struct source *src,struct jpf_value *out);

#endif

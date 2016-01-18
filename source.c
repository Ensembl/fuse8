#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "util/misc.h"
#include "util/logging.h"
#include "jpf/jpf.h"

#include "types.h"
#include "sourcelist.h"
#include "failures.h"

CONFIG_LOGGING(source)

static void src_ref_free(void *data) {
  struct source * src = (struct source *)data;

  log_debug(("source free"));
  sl_release_weak(src->sl);
  free(src->name);
  free(src);
}

static void src_ref_release(void *data) {
  struct source * src = (struct source *)data;

  log_debug(("source release %s",src->name));
  *(src->prev) = src->next;
  if(src->next) { src->next->prev = src->prev; }
  if(src->close) { src->close(src); }
  if(src->fails) { failures_free(src->fails); }
}

void src_open(struct source *src) {
  if(src->open) { src->open(src); }
}

struct source * src_create(char *type) {
  struct source *src;

  src = safe_malloc(sizeof(struct source));
  ref_create(&(src->r));
  ref_on_release(&(src->r),src_ref_release,src);
  ref_on_free(&(src->r),src_ref_free,src);
  src->type = type;
  src->fails = 0;
  src->open = 0;
  src->close = 0;
  src->read = 0;
  src->write = 0;
  src->stat = 0;
  src->stats = 0;
  src->lookup = 0;
  src->readdir = 0;
  src->readlink = 0;
  src->bytes = 0;
  src->hits = 0;
  src->writes = 0;
  src->name = strdup("anon");
  src->r_time = src->w_time = 0;
  src->errors = 0;
  return src;
}

void src_set_fails(struct source *src,struct event_base *eb,
                   int64_t timeout) {
  src->fails = failures_new(eb,timeout);
}

void src_set_name(struct source *src,char *name) {
  free(src->name);
  src->name = strdup(name);
}

struct source * src_get_next(struct source *src) {
  return src->next;
}

void src_collect_rtime(struct source *src,int64_t rtime) {
  src->r_time += rtime;
  log_debug(("%s: rtime=%"PRId64"us",src->name,src->r_time));
}

void src_collect_wtime(struct source *src,int64_t wtime) {
  src->w_time += wtime;
  src->writes++;
  log_debug(("%s: wtime=%"PRId64"us",src->name,src->w_time));
}

void src_collect_error(struct source *src) { src->errors++; }

void src_collect(struct source *src,int64_t length) {
  src->hits++;
  src->bytes += length;
  log_debug(("%s: num=%"PRId64" bytes=%"PRId64,
            src->name,src->hits,src->bytes));
}

struct sourcelist * src_sl(struct source *src) { return src->sl; }

void src_global_stats(struct source *src,struct jpf_value *out) {
  jpfv_assoc_add(out,"hits_total",jpfv_number_int(src->hits));
  jpfv_assoc_add(out,"writes_total",jpfv_number_int(src->writes));
  jpfv_assoc_add(out,"bytes_total",jpfv_number_int(src->bytes));
  jpfv_assoc_add(out,"rtime_secs",jpfv_number(src->r_time/1000000.0));
  jpfv_assoc_add(out,"wtime_secs",jpfv_number(src->w_time/1000000.0));
  jpfv_assoc_add(out,"errors_total",jpfv_number_int(src->errors));
}

void src_release(struct source *src) { ref_release(&(src->r)); }
void src_acquire(struct source *src) { ref_acquire(&(src->r)); }
struct ref * src_ref(struct source *src) { return &(src->r); }

int src_path_ok(struct source *src,char *path) {
  if(!src->fails) { return 1; }
  return failures_check(src->fails,path);
}

int src_set_failed(struct source *src,char *path) {
  if(!src->fails) { return; }
  failures_fail(src->fails,path);
}

char * src_type(struct source *src) { return src->type; }

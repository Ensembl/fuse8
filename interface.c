#include <stdio.h>
#include "util/logging.h"
#include "util/misc.h"
#include "interface.h"
#include "request.h"

CONFIG_LOGGING(interface)

static void ic_ref_free(void *data) {
  struct interface *ic = (struct interface *)data;

  log_debug(("intefrace free"));
  free(ic);
}

static void ic_ref_release(void *data) {
  struct interface *ic = (struct interface *)data;

  log_debug(("intefrace release"));
  if(ic->close) { ic->close(ic); }
}

struct interface * ic_create(void) {
  struct interface *ic;

  ic = safe_malloc(sizeof(struct interface));
  ic->bytes = 0;
  ic->hits = 0;
  ic->errors = 0;
  ref_create(&(ic->r));
  ref_on_release(&(ic->r),ic_ref_release,ic);
  ref_on_free(&(ic->r),ic_ref_free,ic);
  return ic;
}

void ic_collect(struct interface *ic,int64_t length) {
  if(length==-1) {
    log_debug(("%s: failed request",ic->name));
    ic->errors++;
  } else {
    ic->hits++;
    ic->bytes += length;
    log_debug(("%s: num=%"PRId64" bytes=%"PRId64,
              ic->name,ic->hits,ic->bytes));
  }
}

void ic_global_stats(struct interface *ic,struct jpf_value *out) {
  jpfv_assoc_add(out,"hits_total",jpfv_number_int(ic->hits));
  jpfv_assoc_add(out,"errors_total",jpfv_number_int(ic->errors));
  jpfv_assoc_add(out,"bytes_total",jpfv_number_int(ic->bytes));
}

void ic_quit(struct interface *ic) { ic->quit(ic); }

void ic_acquire(struct interface *ic) { ref_acquire(&(ic->r)); }
void ic_release(struct interface *ic) { ref_release(&(ic->r)); }
struct ref * ic_ref(struct interface *ic) { return &(ic->r); }

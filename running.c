#include <stdio.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "running.h"
#include "jpf/jpf.h"
#include "config.h"
#include "util/misc.h"
#include "util/strbuf.h"
#include "util/array.h"
#include "util/assoc.h"
#include "util/path.h"
#include "util/logging.h"
#include "util/rotate.h"
#include "sourcelist.h"
#include "syncsource.h"
#include "syncif.h"
#include "source.h"
#include "interface.h"
#include "request.h"
#include "sources/http/http.h"
#include "sources/file2.h"
#include "sources/cache/file.h"
#include "sources/cache/mmap.h"
#include "sources/meta.h"
#include "interfaces/fuse.h"

CONFIG_LOGGING(running);

// XXX stats to sourcelist for sources
// XXX int numbers in jpf
// XXX nl in stats emission for clarity
// XXX leaks when mount fails
// XXX periodic hang-up-and-reopen
static void stat_timer_tick(evutil_socket_t fd,short what,void *arg) {
  struct running *rr = (struct running *)arg;
  struct source *src;
  struct interface *ic;
  struct jpf_value *out,*out_srcs,*out_src,*out_ics,*out_ic,*out_mem;
  struct jpf_callbacks jpf_emitter_cb;
  struct jpf_emitter jpf_emitter;
  char *time_str;
  int i;

  if(rr->stats_fd==-1) { return; }
  log_debug(("main stat collection loop"));
  out_srcs = jpfv_important_assoc(0);
  for(i=0;i<array_length(rr->src);i++) {
    src = (struct source *)array_index(rr->src,i);
    out_src = jpfv_assoc();
    src_global_stats(src,out_src);
    if(src->stats) { src->stats(src,out_src); }
    jpfv_assoc_add(out_srcs,src->name,out_src);
  }
  out_ics = jpfv_important_assoc(0);
  for(i=0;i<array_length(rr->icc);i++) {
    ic = (struct interface *)array_index(rr->icc,i);
    out_ic = jpfv_assoc();
    ic_global_stats(ic,out_ic);
    jpfv_assoc_add(out_ics,ic->name,out_ic);
  }
  out = jpfv_important_assoc(0);
  time_str= iso_localtime(0);
  jpfv_assoc_add(out,"time",jpfv_string(time_str));
  free(time_str);
  jpfv_assoc_add(out,"sources",out_srcs);
  jpfv_assoc_add(out,"interfaces",out_ics);
  out_mem = jpfv_important_array(1);
  jpfv_array_add(out_mem,out);
  jpf_emit_fd(&jpf_emitter_cb,&jpf_emitter,rr->stats_fd);
  jpf_emit_df(out_mem,&jpf_emitter_cb,&jpf_emitter);
  jpf_emit_done(&jpf_emitter);
  jpfv_free(out_mem);
}

static void do_exit(void *eb) {
  log_info(("All interfaces have exited: stopping event loop"));
  event_base_loopexit((struct event_base *)eb,0);
}

static void sigkill_self(evutil_socket_t fd,short what,void *arg) {
  log_warn(("Did not quit nicely, sending SIGKILL to self"));
  kill(getpid(),SIGKILL);
}

// XXX cond for regular exit
// XXX -9 after delay
#define SIGKILL_DELAY 90 /* sec */
static void user_quit(evutil_socket_t fd,short what,void *arg) {
  struct running *rr = (struct running *)arg;
  int i;
  struct array *icc;
  struct timeval sigkill_delay = { SIGKILL_DELAY, 0 };

  icc = rr->icc;
  array_acquire(icc);
  log_info(("Sending quit to interfaces (%d)",array_length(icc)));
  for(i=0;i<array_length(icc);i++) {
    ic_quit((struct interface *)array_index(icc,i));
  }
  array_release(icc);
  event_del(rr->stat_timer);
  event_add(rr->sigkill_timer,&sigkill_delay);
}

static void hupev(evutil_socket_t fd,short what,void *arg) {
  struct running *rr = (struct running *)arg;

  rotate_logs(rr);
}

static void interfaces_quit(void *arg) {
  struct running *rr = (struct running *)arg;
  struct array *icc;

  log_debug(("All interfaces quit"));
  /* Release the interfaces */
  icc = rr->icc;
  rr->icc = array_create(0,0);
  array_release(icc);
  ref_release(&(rr->need_loop)); /* Ifs don't need event loop any more */
  sl_release(rr->sl); /* Don't need sources any more */
}

void run_src_register(struct running *rr,char *type,src_create_fn fn) {
  assoc_set(rr->src_shop,type,fn);
}

void run_ic_register(struct running *rr,char *type,ic_create_fn fn) {
  assoc_set(rr->ic_shop,type,fn);
}

static void ic_done(void *data,void *priv) {
  struct interface *ic = (struct interface *)data;

  log_debug(("release"));
  ic_release(ic);
}

void setup_running(struct running *rr) {
  rr->stats_fd = -1;
  rr->have_quit = 0;
  rr->eb = event_base_new();
  rr->edb = evdns_base_new(rr->eb,1);
  rr->sq = sq_create(rr->eb);
  rr->sl = sl_create();
  rr->si = syncif_create(rr->eb);
  rr->icc = array_create(ic_done,0);
  rr->src = array_create(0,0);
  rr->src_shop = assoc_create(0,0,0,0);
  rr->ic_shop = assoc_create(0,0,0,0);
  rr->rot = rotator_create();
  ref_create(&(rr->need_loop));
  ref_create(&(rr->ic_running));
  /* Need to process syncqueue closing events before exiting mainloop */
  ref_on_free(&(rr->need_loop),do_exit,rr->eb);
  ref_until_free(&(rr->need_loop),sq_ref(rr->sq));
  ref_until_free(&(rr->need_loop),sl_ref(rr->sl));
  /* keep a ref to need_loop for all interfaces: released in ic_running */
  ref_on_release(&(rr->ic_running),interfaces_quit,rr);
}

void register_interface_types(struct running *rr) {
  run_ic_register(rr,"fuse",ic_fuse_make);
}

void register_source_types(struct running *rr) {
  run_src_register(rr,"cachefile",source_cachefile2_make);
  run_src_register(rr,"cachemmap",source_cachemmap2_make);
  run_src_register(rr,"meta",source_meta_make);
  run_src_register(rr,"http",source_http_make);
  run_src_register(rr,"file",source_file2_make);
}

void run_config(struct running *rr,char *conf_file) {
  char *path,*dir;

  // XXX support options, multiple locations
  if(!conf_file) {
    log_debug(("Looking for config file"));
    path = self_path();
    path_separate(path,&dir,0);
    free(path);
    path = make_string("%s/config.jpf",dir);
    free(dir);
  } else {
    path = strdup(conf_file);
  }
  log_info(("Reading config from '%s'",path));
  if(load_config(rr,path)) {
    log_error(("Error reading config file"));
    logging_done();
    exit(1);
  }
  free(path);
}

void start_stats_timer(struct running *rr) {
  rr->stat_timer = event_new(rr->eb,-1,EV_PERSIST,stat_timer_tick,rr);
  event_add(rr->stat_timer,&(rr->stat_timer_interval));
}

void closedown(struct running *rr) {
  log_debug(("closedown"));
  rotator_release(rr->rot);
  array_release(rr->src);
  array_release(rr->icc);
  assoc_release(rr->src_shop);
  assoc_release(rr->ic_shop);
  si_release(rr->si);
  event_del(rr->stat_timer);
  event_free(rr->stat_timer);
  event_del(rr->sigkill_timer);
  event_free(rr->sigkill_timer);
  evdns_base_free(rr->edb,1);
  event_base_free(rr->eb);
}

void run(char *conf_file) {
  struct running rr;
  struct event *sig1_ev,*sig2_ev,*sig_hup;

  evthread_use_pthreads();
  setup_running(&rr);
  register_interface_types(&rr);
  register_source_types(&rr);
  run_config(&rr,conf_file);
  start_stats_timer(&rr);
  
  ref_release(&(rr.ic_running));

  event_add(sq_consumer(rr.sq),0);
  event_add(si_consumer(rr.si),0);
  sq_release(rr.sq);
  evsignal_add(sig1_ev=evsignal_new(rr.eb,SIGINT,user_quit,&rr),0);
  evsignal_add(sig2_ev=evsignal_new(rr.eb,SIGTERM,user_quit,&rr),0);
  evsignal_add(sig_hup=evsignal_new(rr.eb,SIGHUP,hupev,&rr),0);
  rr.sigkill_timer = event_new(rr.eb,-1,EV_PERSIST,sigkill_self,&rr);
  log_info(("Starting event loop"));
  event_base_loop(rr.eb,0);
  log_info(("Event loop finished"));
  event_del(sig1_ev);
  event_del(sig2_ev);
  event_del(sig_hup);
  event_free(sig1_ev);
  event_free(sig2_ev);
  event_free(sig_hup);
  closedown(&rr);
  log_info(("Bye!"));
  config_finished();
}

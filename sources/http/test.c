#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <event2/dns.h>
#include "client.h"
#include "../../util/logging.h"

static void do_exit(evutil_socket_t fd,short what,void *eb) {
  event_base_loopexit((struct event_base *)eb,0);
}

static void done(int success,char *data,int64_t len,int eof,void *priv,
                 struct http_stats *stats) {
  char *out;

  if(!success) {
    fprintf(stderr,"ERROR: %s\n",data);
    return;
  }
  fprintf(stderr,"done success=%d len=%ld '%d'\n",success,len,data[4]);
}

char * url = "http://ftp.ensembl.org/pub/data_files/homo_sapiens/GRCh38/dna_methylation_feature/dna_methylation_feature/Fibrobl_5mC_ENCODE_Husdonalpha_RRBS_FDR_1e-4/wgEncodeHaibMethylRrbsFibroblDukeRawDataRep.bb";

static void req(evutil_socket_t fd,short what,void *priv) {
  struct httpclient *cli = (struct httpclient *)priv;

  http_request(cli,url,0,20,done,0);
}

int main() {
  struct httpclient *cli;
  struct event_base *eb;
  struct evdns_base *edb;
  struct event *exit_ev,*ev,*ev2;
  struct timeval three_sec = {3,0};
  struct timeval ten_sec = {10,0};

  logging_fd(2);
  log_set_level("",LOG_DEBUG);
  eb = event_base_new();
  edb = evdns_base_new(eb,1);
  cli = httpclient_create(eb,edb);
  exit_ev = evsignal_new(eb,SIGINT,do_exit,eb);
  event_add(exit_ev,0);

  http_request(cli,url,0,20,done,0);
  http_request(cli,url,0,20,done,0);
  http_request(cli,url,0,20,done,0);
  http_request(cli,url,0,20,done,0);
  http_request(cli,url,0,20,done,0);
  http_request(cli,url,0,20,done,0);
  ev = evtimer_new(eb,req,cli);
  evtimer_add(ev,&three_sec);
  ev2 = evtimer_new(eb,req,cli);
  evtimer_add(ev2,&ten_sec);


  event_base_loop(eb,0);
  httpclient_finish(cli);
  event_free(exit_ev);
  event_free(ev);
  event_free(ev2);
  evdns_base_free(edb,1);
  event_base_free(eb);
  fprintf(stderr,"exit\n");
  logging_done();
  return 0;
}

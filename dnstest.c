#include <string.h>
#include <event2/event.h>
#include <event2/dns.h>

#include "util/dns.h"

typedef void (*dns_cb)(const char *address,void *);

void done(const char *address,void *priv) {
  fprintf(stderr,"address=%s\n",address);
}

int main(void) {
  struct event_base *eb;
  struct evdns_base *edb;
  char *out;

  eb = event_base_new();
  edb = evdns_base_new(eb,1);
  dns_resolve(edb,"ip6-localhost",done,0);
  evdns_base_free(edb,1);
  event_base_free(eb);
  return 0;
}

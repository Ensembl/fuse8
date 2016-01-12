#ifndef UTIL_DNS_H
#define UTIL_DNS_H

#include <event2/dns.h>

#include "misc.h"

typedef void (*dns_cb)(const char *address,void *);

void dns_resolve(struct evdns_base *edb,const char *hostname,
                 dns_cb cb,void *priv);

#endif

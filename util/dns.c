#include <string.h>
#include <event2/event.h>
#include <event2/dns.h>

#include "dns.h"
#include "misc.h"

struct dnsreq {
  dns_cb cb;
  void *priv;
};

#define ADDRMAX 128
static char * addrinfo_to_string(struct evutil_addrinfo *ai) {
  char *buf,*out;
  const char *s=0;
  struct sockaddr_in *sin;
  struct sockaddr_in6 *sin6;

  buf = safe_malloc(ADDRMAX);
  if(ai->ai_family == AF_INET) {
    sin = (struct sockaddr_in *)ai->ai_addr;
    s = evutil_inet_ntop(AF_INET,&sin->sin_addr,buf,ADDRMAX);
  } else if (ai->ai_family == AF_INET6) {
    sin6 = (struct sockaddr_in6 *)ai->ai_addr;
    s = evutil_inet_ntop(AF_INET6,&sin6->sin6_addr,buf,ADDRMAX);
  }
  out = strdup(s);
  free(buf);
  return out;
}

static void resolve_cb(int errcode,struct evutil_addrinfo *addr,void *priv) {
  struct dnsreq *dr = (struct dnsreq *)priv;
  struct evutil_addrinfo *ai;
  char *s;
  int n;

  n = 0;
  for(ai=addr;ai;ai=ai->ai_next) {
    s = addrinfo_to_string(ai);
    if(s)
      n++;
    free(s);
  }
  if(!n) {
    // XXX log it
    fprintf(stderr,"dns error='%s'\n",evutil_gai_strerror(errcode));
    dr->cb(0,dr->priv);
  } else {
    n = rand()%n;
    for(ai=addr;ai;ai=ai->ai_next) {
      s = addrinfo_to_string(ai);
      if(s && !n--)
        dr->cb(s,dr->priv);
      free(s);
    }
  }
  if(addr) { evutil_freeaddrinfo(addr); }
  free(dr);
}

// XXX cancel timeout
void dns_resolve(struct evdns_base *edb,const char *hostname,
                 dns_cb cb,void *priv) {
  struct evutil_addrinfo hints;
  struct dnsreq *dr;

  memset(&hints,0,sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = EVUTIL_AI_CANONNAME;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  dr = safe_malloc(sizeof(struct dnsreq));
  dr->cb = cb;
  dr->priv = priv;
  evdns_getaddrinfo(edb,hostname,0,&hints,resolve_cb,dr);
}

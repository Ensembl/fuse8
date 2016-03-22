#ifndef SYNCIF_H
#define SYNCIF_H

struct syncif;

void si_read(struct syncif *si,struct sourcelist *sl,char *spec,
             int64_t version,int64_t offset,int64_t length,
             req_fn done,void *priv);

struct syncif * syncif_create(struct event_base *eb);
struct event * si_consumer(struct syncif *si);
void si_release(struct syncif *si);

#endif

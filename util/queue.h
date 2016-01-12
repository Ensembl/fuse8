#ifndef UTIL_QUEUE_H
#define UTIL_QUEUE_H

struct queue;

struct queue * queue_create(type_free_cb freev,void *priv);
void queue_add(struct queue *q,void *data);
void * queue_remove(struct queue *q);
int queue_length(struct queue *q);
void queue_release(struct queue *q);

#endif

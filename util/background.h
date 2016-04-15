#ifndef BACKGROUND_H
#define BACKGROUND_H

struct background;

typedef void (*bgd_fn)(struct background *bb,
                       void *data,int dispose,void *payload);

struct background * background_create(bgd_fn bgd,void *payload);
void background_release(struct background *bb);
void background_add(struct background *bb,void *data);
void background_start(struct background *bb);
void background_stop(struct background *bb);
void background_finish(struct background *bb);
void background_yield(struct background *bb);

#endif

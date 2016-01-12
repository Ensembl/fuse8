#ifndef REQUEST_H
#define REQUEST_H

#include "types.h"

struct request * rq_create(struct sourcelist *sl,
                           char *spec,int64_t offset,int64_t length,req_fn done,void *priv);
void rq_acquire(struct request *rq);
void rq_release(struct request *rq);
void rq_run(struct request *rq);
void rq_run_next(struct request *rq);
struct chunk * rq_chunk(struct source *sc,char *data,
                        int64_t offset,int64_t length,int eof,
                        struct chunk *next);
void rq_found_data(struct request *rq,struct chunk *c); 
void rq_run_next_write(struct request *rq);
void rq_error(struct request *rq,int failed_errno);

#endif

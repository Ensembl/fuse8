#ifndef UTIL_RANGES_H
#define UTIL_RANGES_H

/* ALL RANGES ARE SEMI-OPEN */

/* Only here to allow stack-allocation, don't peek */
struct range;
struct ranges {
  struct range *r;
};
struct rangei {
  struct ranges *rr;
  struct range *r;
};

void ranges_init(struct ranges *rr);
void ranges_free(struct ranges *rr);
void ranges_copy(struct ranges *dst,struct ranges *src);
void ranges_add(struct ranges *rr,int64_t a,int64_t b);
void ranges_remove(struct ranges *rr,int64_t a,int64_t b);
void ranges_start(struct ranges *rr,struct rangei *ri);
int ranges_next(struct rangei *ri,int64_t *a,int64_t *b);
int ranges_empty(struct ranges *rr);
void ranges_merge(struct ranges *a,struct ranges *b);
void ranges_difference(struct ranges *a,struct ranges *b);
void ranges_blockify_expand(struct ranges *a,int size);
void ranges_blockify_reduce(struct ranges *a,int size);
char * ranges_print(struct ranges *rr);
int ranges_num(struct ranges *rr);

#endif

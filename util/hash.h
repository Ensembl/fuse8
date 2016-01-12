#ifndef UTIL_HASH_H
#define UTIL_HASH_H

struct hash;

unsigned char * hash_data(struct hash *h);
unsigned int hash_len(struct hash *h);
void write_hash(char *msg,unsigned char *ptr,unsigned int len);
struct hash * make_hash(char *msg);
uint64_t hash_mod(struct hash *h,int n);
int hash_cmp(struct hash *h,void *mem,int len);
void free_hash(struct hash *h);
char * print_hex(unsigned char *val,unsigned int len);
char * print_hash(struct hash *h);
uint32_t data_hash(const char *key, uint32_t len);
uint32_t str_hash(const char *key);

#endif

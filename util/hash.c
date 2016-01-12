#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <openssl/evp.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include "misc.h"
#include "hash.h"

struct hash {
  unsigned char val[EVP_MAX_MD_SIZE];
  unsigned int len;
};

unsigned char * hash_data(struct hash *h) { return h->val; }
unsigned int hash_len(struct hash *h) { return h->len; }

static void do_hash(char *msg,unsigned char *ptr,
                    unsigned int len,unsigned int *wlen) {
  EVP_MD_CTX ctx;
  unsigned char *out;
  unsigned int num;

  EVP_MD_CTX_init(&ctx);
  EVP_DigestInit_ex(&ctx,EVP_sha512(),0);
  EVP_DigestUpdate(&ctx,msg,strlen(msg));
  if(len) {
    out = safe_malloc(EVP_MAX_MD_SIZE);
    EVP_DigestFinal(&ctx,out,&num);
    memcpy(ptr,out,num<len?num:len);
    if(num<len) { memset(ptr+num,0,len-num); }
    free(out);
  } else {
    EVP_DigestFinal(&ctx,ptr,wlen);
  }
}

void write_hash(char *msg,unsigned char *ptr,unsigned int len) {
  if(!len) { return; }
  do_hash(msg,ptr,len,0);
}

struct hash * make_hash(char *msg) {
  struct hash *out;

  out = safe_malloc(sizeof(struct hash));
  out->len = 0;
  do_hash(msg,out->val,0,&(out->len));
  return out;
}

uint64_t hash_mod(struct hash *h,int n) {
  unsigned int val = 0;
  int i;

  for(i=0;i<8 && i<h->len;i++)
    val = (val<<8)+h->val[i];
  return val%n;
}

int hash_cmp(struct hash *h,void *mem,int len) {
  if(len>h->len) { len = h->len; }
  return memcmp(h->val,mem,len);
}

void free_hash(struct hash *h) {
  free(h);
}

char * print_hex(unsigned char *val,unsigned int len) {
  int i;
  char *out;

  out = malloc(len*2+1);
  for(i=0;i<len;i++) {
    snprintf(out+2*i,3,"%2.2x",*(val+i));
  }
  return out;
}

char * print_hash(struct hash *h) {
  return print_hex(h->val,h->len);
}

/* murmur3_32 : seed = 0x2342feed */
#define ROT32(x, y) ((x << y) | (x >> (32 - y)))
uint32_t data_hash(const char *key, uint32_t len) {
  static const uint32_t c1 = 0xcc9e2d51;
  static const uint32_t c2 = 0x1b873593;
  static const uint32_t r1 = 15;
  static const uint32_t r2 = 13;
  static const uint32_t m = 5;
  static const uint32_t n = 0xe6546b64;
  uint32_t hash = 0x2342feed;
  const int nblocks = len / 4;
  const uint32_t *blocks = (const uint32_t *) key;
  int i;
  uint32_t k;

  for (i = 0; i < nblocks; i++) {
    k = blocks[i];
    k *= c1;
    k = ROT32(k, r1);
    k *= c2;
    hash ^= k;
    hash = ROT32(hash, r2) * m + n;
  }

  const uint8_t *tail = (const uint8_t *) (key + nblocks * 4);
  uint32_t k1 = 0;

  switch (len & 3) {
  case 3: k1 ^= tail[2] << 16;
  case 2: k1 ^= tail[1] << 8;
  case 1:
    k1 ^= tail[0];
    k1 *= c1;
    k1 = ROT32(k1, r1);
    k1 *= c2;
    hash ^= k1;
  }

  hash ^= len;
  hash ^= (hash >> 16);
  hash *= 0x85ebca6b;
  hash ^= (hash >> 13);
  hash *= 0xc2b2ae35;
  hash ^= (hash >> 16);

  return hash;
}

uint32_t str_hash(const char *key) {
  return data_hash(key,strlen(key));
}


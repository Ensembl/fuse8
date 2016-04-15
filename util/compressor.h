#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include "background.h"

struct compressor;

struct compressor * compressor_create(void);
void compressor_release(struct compressor *cc);

struct background * compressor_background(struct compressor *cc);

void compressor_add(struct compressor *cc,char *filename);

#endif

#ifndef UTIL_FILE_H
#define UTIL_FILE_H

int file_put_string(char *filename,char *contents);
int file_put_contents(char *filename,unsigned char *contents,int len);

#endif

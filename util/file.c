#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "misc.h"

int file_put_contents(char *filename,unsigned char *contents,int len) {
  int fd;

  fd = open(filename,O_WRONLY|O_CREAT,0666);
  if(fd==-1) {
    return errno;
  }
  if(write_all(fd,contents,len)) {
    return errno;
  }
  if(close(fd)) {
    return errno;
  }
  return 0;
}

int file_put_string(char *filename,char *contents) {
  return file_put_contents(filename,(unsigned char *)contents,strlen(contents));
}

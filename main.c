#include <stdio.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <pthread.h>
#include <signal.h>

#include "running.h"

int main(int argc,char **argv) {
  run();
  return 0;
}

#ifndef CONFIG_H
#define CONFIG_H

#include "running.h"

int load_config(struct running *rr,char *path);
void config_finished(void);
void rotate_logs(struct running *rr);

#endif

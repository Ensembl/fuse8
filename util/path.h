#ifndef UTIL_PATH_H
#define UTIL_PATH_H

void path_separate(char *in,char **dir_out,char **file_out);
void to_dir_file(char *in,char **dir_out,char **file_out);
int path_exists(char *in);

#endif


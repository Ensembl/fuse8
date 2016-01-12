#ifndef UTIL_LOGGING_H
#define UTIL_LOGGING_H

#include "misc.h"

enum log_level { LOG_CHECK, LOG_RECHECK, /* both internal use only */
                 LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

#define CONFIG_LOGGING(type) \
  static __attribute__((unused)) enum log_level logging_level = LOG_CHECK; \
  static __attribute__((unused)) char *logging_type = #type; 

#define LOG_DO(level,args) do { \
  if_rare(level >= logging_level) { \
    char *logging_msg = make_string args; \
    log_message(logging_type,&logging_level,level,logging_msg,__FILE__,__LINE__); \
    free(logging_msg); \
  } \
} while(0)
#define LOG_CHECK(level) (level >= logging_level)

#define log_debug(msg) LOG_DO(LOG_DEBUG,msg)
#define log_info(msg) LOG_DO(LOG_INFO,msg)
#define log_warn(msg) LOG_DO(LOG_WARN,msg)
#define log_error(msg) LOG_DO(LOG_ERROR,msg)

#define log_do_debug LOG_CHECK(LOG_DEBUG)
#define log_do_info  LOG_CHECK(LOG_INFO)
#define log_do_warn  LOG_CHECK(LOG_WARN)
#define log_do_error LOG_CHECK(LOG_ERROR)

enum log_level log_get_level_by_name(char *name);
void log_set_level(char *type,enum log_level level);
void logging_fd(int fd);
void logging_done(void);

/* Use the macros instead of these two, or weird stuff will happen to you */
void log_message(char *type,enum log_level *file_level,
   	         enum log_level msg_level,char *msg,char *file,int line);
enum log_level log_level(char *type,enum log_level *file_level);

#endif

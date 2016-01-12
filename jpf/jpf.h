#ifndef JPF_H
#define JPF_H

#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <inttypes.h>
#include "util.h"

/* USED IN THE LEXER */

// XXX rename to jpf_
/* Only here to allow allocation on stack. Don't be naughty and peek */
struct token;
struct lexer {
  FILE *in;
  jmp_buf fatal;
  char *pre_error;
  /* limits */
  size_t maxalloc,alloced;
  int maxstack;
  /* callbacks */
  struct jpf_callbacks *callbacks;
  void (*done)(struct lexer *);
  void *priv;
  /* lexer state */
  int seen_space,need_more,atom_top,can_push;
  int col,curcol,line,startcol,colkill;
  /* token stack */
  struct token *stack;
  int size,num;
  /* strings */
  struct jpf_strbuf str;
  /* heredocs */
  char *hmark;
  int hindent;
};

enum style {
  JPF_QUOTED, JPF_UNQUOTED, JPF_HEREDOC, JPF_BEST, JPF_INT, JPF_IMPORTANT,
  JPF_VIMPORTANT
};

/* Use directly for initial assignment */
struct jpf_callbacks {
  void (*start)(void *);
  void (*end)(void *);
  void (*push_array)(void *);
  void (*push_assoc)(void *);
  void (*push_null)(void *);
  void (*push_boolean)(int,void *);
  void (*push_number)(double,enum style,void *);
  void (*push_string)(char *,enum style,void *);
  void (*error)(char *,int,int,void *);
  void (*start_member)(enum style,void *);
  void (*end_member)(void *);
  void (*start_key)(char *key,enum style,int,void *);
  void (*end_key)(char *key,void *);
};

void jpf_lex_file(struct lexer *lx,FILE *f);
void jpf_lex_filename(struct lexer *lx,char *fn);
void jpf_lex_fd(struct lexer *lx,int fd);
void jpf_lex_str(struct lexer *lx,char *str);

void jpf_max_memory(struct lexer *lx,size_t buffers,int stack);
void jpf_lex_go(struct lexer *lx,struct jpf_callbacks *cb,void *priv);

/* USED IN THE EMITTER */

/* Don't peek. Here to allow stack allocation. */
struct jpf_emitter {
  struct jpf_strbuf buf;
  int nl,empty;
  /* emission */
  void (*done)(struct jpf_emitter *);
  char **outstr;
  FILE *outf;
  char *failed;
  /* indent stack */
  int len,size;
  int *stack;
};

void jpf_emit_str(struct jpf_callbacks *cb,struct jpf_emitter *em,char **out);
void jpf_emit_file(struct jpf_callbacks *cb,struct jpf_emitter *em,FILE *out);
void jpf_emit_filename(struct jpf_callbacks *cb,struct jpf_emitter *em,
                       char *filename);
void jpf_emit_fd(struct jpf_callbacks *cb,struct jpf_emitter *em,int fd);
char * jpf_emit_failed(struct jpf_emitter *em);
void jpf_emit_done(struct jpf_emitter *em);

/* USED IN THE DEFAULT PARSER */

struct jpf_value;

char * jpf_dfparse(struct lexer *lx,struct jpf_value **out);
void jpf_emit_df(struct jpf_value *v,struct jpf_callbacks *cb,void *priv);

enum jpf_type {
  JPFV_STRING, JPFV_NUMBER, JPFV_TRUE, JPFV_FALSE, JPFV_NULL,
  JPFV_ARRAY, JPFV_ASSOC
};

struct jpf_value * jpfv_alloc(enum jpf_type type,enum style style);
struct jpf_value * jpfv_number(double n);
struct jpf_value * jpfv_number_int(double n);
struct jpf_value * jpfv_string(char *c);
struct jpf_value * jpfv_array(void);
struct jpf_value * jpfv_important_array(int very);
struct jpf_value * jpfv_assoc(void);
struct jpf_value * jpfv_important_assoc(int very);
void jpfv_array_add(struct jpf_value *v,struct jpf_value *member);
void jpfv_assoc_add(struct jpf_value *v,char *key,struct jpf_value *member);
void jpfv_free(struct jpf_value *v);
struct jpf_value * jpfv_lookup(struct jpf_value *v,char *key);
int jpfv_int(struct jpf_value *v,int *out);
int jpfv_int64(struct jpf_value *v,int64_t *out);
int jpfv_bool(struct jpf_value *v);

struct jpf_value;
/* Feel free to look at these for traversal, but please only update via
 * supplied functions. BTW, len is what you want, not size.
 */
struct jpf_array {
  int size,len;
  struct jpf_value **v;
};

struct jpf_assoc {
  int size,len;
  char **k;
  struct jpf_value **v;
};

struct jpf_value {
  enum jpf_type type;
  enum style style;
  union {
    char *string;
    double number;
    struct jpf_array array;
    struct jpf_assoc assoc;
  } v;
};

#endif

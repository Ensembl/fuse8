#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "jpf.h"
#include "util.h"

#define READ_BLOCK 4096
int read_file(char *fn,char **out) {
  struct jpf_strbuf s;
  int fd,r=1,e;

  jpf_strbuf_init(&s,0);
  fd = open(fn,O_RDONLY);
  if(fd<0) { jpf_strbuf_free(&s); return errno; }
  while(r>0) {
    r = read(fd,jpf_strbuf_extend(&s,READ_BLOCK),READ_BLOCK);
    if(r<0) { e = errno; close(fd); jpf_strbuf_free(&s); return e; }
    jpf_strbuf_retract(&s,READ_BLOCK-r);  
  }
  if(close(fd)) { jpf_strbuf_free(&s); return errno; }
  *out = jpf_strbuf_str(&s);
  return 0;
}

void str_compare(char *got,char *wanted,char *more) {
  int i,line=1;

  if(!strcmp(got,wanted)) { return; }
  for(i=0;*(got+i) && *(wanted+i);i++) {
    if(*(got+i) != *(wanted+i)) { break; }
    if(*(wanted+i)=='\n') { line++; }
  }
  fprintf(stderr,"DIFFER ON line %d (%d vs %d)\nGOT:\n",line,
          (unsigned char)got[i],(unsigned char)wanted[i]);
  fprintf(stderr,"%s",got);
  if(more) { fprintf(stderr,"MORE:\n%s",more); }
  exit(1);
}

void start(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"START\n");
}
void end(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"END\n");
}
void push_array(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"ARRAY\n");
}
void push_assoc(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"ASSOC\n");
}
void push_null(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"NULL\n");
}
void push_boolean(int v,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"BOOL %d\n",v);
}
void push_number(double v,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"NUM %lf\n",v);
}
void push_string(char *c,enum style style,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"STRING '%s' (%d)\n",c,style);
}
void error(char *c,int line,int col,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"ERROR '%s' line %d col %d\n",c,line,col);
}
void end_member(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"ENDMEMBER\n");
}
void start_member(void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"STARTMEMBER\n");
}
void start_key(char *key,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"STARTKEY '%s'\n",key);
}
void end_key(char *key,void *priv) {
  jpf_strbuf_add((struct jpf_strbuf *)priv,"ENDKEY '%s'\n",key);
}

void set_callbacks(struct jpf_callbacks *cb) {
  *cb = (struct jpf_callbacks) {
    .push_array = push_array,
    .push_assoc = push_assoc,
    .push_null = push_null,
    .push_boolean = push_boolean,
    .push_number = push_number,
    .push_string = push_string,
    .error = error,
    .start_member = start_member,
    .end_member = end_member,
    .start_key = start_key,
    .end_key = end_key,
    .start = start,
    .end = end
  };
}

void test_pair(char *jpf,char *cmp) {
  char *c;
  struct jpf_strbuf t;
  struct lexer lx;
  struct jpf_callbacks cb;

  if(read_file(cmp,&c)) { perror("Reading cmp file"); exit(255); }
  set_callbacks(&cb);
  fprintf(stderr,"Comparing '%s' to '%s'\n",jpf,cmp);
  jpf_strbuf_init(&t,0); 
  jpf_lex_filename(&lx,jpf);
  jpf_max_memory(&lx,100*1024*1024,100*1000);
  jpf_lex_go(&lx,&cb,&t);
  str_compare(jpf_strbuf_str(&t),c,0);
  jpf_strbuf_free(&t);
  free(c);
}

void test_pair_ert(char *jpf,char *cmp) {
  char *c,*error;
  struct jpf_strbuf t;
  struct lexer lx,lx2;
  struct jpf_callbacks cb,cb2;
  struct jpf_emitter em;
  char *tmp;

  if(read_file(cmp,&c)) { perror("Reading cmp file"); exit(255); }
  fprintf(stderr,"Event round-tripping '%s' to '%s'\n",jpf,cmp);
  jpf_lex_filename(&lx,jpf);
  jpf_max_memory(&lx,100*1024*1024,100*1000);
  jpf_emit_str(&cb,&em,&tmp);
  jpf_lex_go(&lx,&cb,&em);
  error = jpf_emit_failed(&em);
  if(error) {
    fprintf(stderr,"Emit failed: '%s'\n",error);
    exit(255);
  }
  jpf_lex_str(&lx2,tmp);
  set_callbacks(&cb2);
  jpf_strbuf_init(&t,0); 
  jpf_lex_go(&lx2,&cb2,&t);
  str_compare(jpf_strbuf_str(&t),c,tmp);
  jpf_emit_done(&em);
  jpf_strbuf_free(&t);
  free(c);
}

void test_pair_srt(char *jpf,char *cmp) {
  char *c,*errors,*tmp;
  struct jpf_strbuf t;
  struct lexer lx,lx2;
  struct jpf_callbacks cb,cb2;
  struct jpf_emitter em;
  struct jpf_value *val;

  if(read_file(cmp,&c)) { perror("Reading cmp file"); exit(255); }
  fprintf(stderr,"Full round-tripping '%s' to '%s'\n",jpf,cmp);
  jpf_lex_filename(&lx,jpf);
  jpf_max_memory(&lx,100*1024*1024,100*1000);
  errors = jpf_dfparse(&lx,&val);
  if(errors) {
    fprintf(stderr,"ERRORS:\n%s",errors);
    exit(255);
  }
  jpf_emit_str(&cb,&em,&tmp);
  jpf_emit_df(val,&cb,&em);
  jpf_lex_str(&lx2,tmp);
  set_callbacks(&cb2);
  jpf_strbuf_init(&t,0); 
  jpf_lex_go(&lx2,&cb2,&t);
  str_compare(jpf_strbuf_str(&t),c,tmp);
  jpf_emit_done(&em);
  jpfv_free(val);
  jpf_strbuf_free(&t);
  free(c);
}

// TODO emit comparer

void test_pair_fd(char *jpf,char *cmp) {
  int fd;
  char *c;
  struct jpf_strbuf t;
  struct lexer lx;
  struct jpf_callbacks cb;

  set_callbacks(&cb);
  fprintf(stderr,"Comparing '%s' to '%s'\n",jpf,cmp);
  jpf_strbuf_init(&t,0);
  fd = open(jpf,O_RDONLY);
  if(fd==-1) { perror("Opening jpf file"); exit(255); }
  jpf_lex_fd(&lx,fd);
  jpf_max_memory(&lx,100*1024*1024,100*1000);
  jpf_lex_go(&lx,&cb,&t);
  if(read_file(cmp,&c)) { perror("Reading cmp file"); exit(255); }
  str_compare(jpf_strbuf_str(&t),c,0);
  jpf_strbuf_free(&t);
  free(c);
  close(fd);
}

#define BIG 1000
#define BIGGER 65536
#define VERYBIG 1000000
char * bigstr(char *c,int num) {
  char *s;
  int i,len;

  len = strlen(c);
  s = jpf_safe_malloc(len*num+1);
  for(i=0;i<num;i++) {
    strcpy(s+i*len,c); 
  }
  return s;
}

void exhaust(char *msg,char *rep,int num,int stk,size_t mem,char *err) {
  struct lexer lx;
  struct jpf_callbacks cb;
  struct jpf_strbuf t;
  char *c;

  fprintf(stderr,"Forcing '%s'\n",msg);
  set_callbacks(&cb);
  jpf_strbuf_init(&t,0);
  c = bigstr(rep,num);
  jpf_lex_str(&lx,c);
  jpf_max_memory(&lx,mem,stk);
  jpf_lex_go(&lx,&cb,&t);
  if(!strstr(jpf_strbuf_str(&t),err)) {
    fprintf(stderr,"Cannot force overflow error '%s'\n",msg);
    exit(1);
  }
  jpf_strbuf_free(&t);
  free(c);
}

void exhaust_stack(void) {
  exhaust("stack exhaustion","- ",BIG,BIG/2,0,"ERROR 'Stack overflow'");
}

void exhaust_memory(void) {
  exhaust("string memory exhaustion","x",VERYBIG,0,VERYBIG/2,"ERROR 'Out of memory'");
}

void exhaust_keystr(void) {
  char * key = "abcdefghijklmnopqrstuvwxyz"
               "abcdefghijklmnopqrstuvwxyz"
               "abcdefghijklmnopqrstuvwxyz"
               "abcdefghijklmnopqrstuvwxyz"
               "abcdefghijklmnopqrstuvwxyz: ";

  exhaust("key string space",key,BIG,0,BIGGER,"ERROR 'Exceeded maximum memory with keys'");
}

void exhaust_flex(void) {
  exhaust("flex buffers","# #",BIGGER,0,BIGGER,"ERROR 'flex internal buffer full: pathological input'");
}

// XXX systematic rename
// XXX coverage analysis
// XXX test srt errors
int main(void) {
  /* Simple compare-to-known test cases */
  test_pair_fd("test/general.jpf","test/general.cmp");
  test_pair("test/general.jpf","test/general.cmp");
  test_pair_ert("test/general.jpf","test/general.cmp");
  test_pair_srt("test/general.jpf","test/general.cmp");
  test_pair("test/quoted.jpf","test/quoted.cmp");
  test_pair_ert("test/quoted.jpf","test/quoted-ert.cmp");
  test_pair_srt("test/quoted-srt.jpf","test/quoted-srt.cmp");
  test_pair("test/unquoted.jpf","test/unquoted.cmp");
  test_pair_ert("test/unquoted.jpf","test/unquoted.cmp");
  test_pair_srt("test/unquoted.jpf","test/unquoted-srt.cmp");
  test_pair("test/empty.jpf","test/empty.cmp");
  test_pair_ert("test/empty.jpf","test/empty.cmp");
  test_pair_srt("test/empty.jpf","test/empty.cmp");
  test_pair("test/only-comment.jpf","test/only-comment.cmp");
  test_pair_ert("test/only-comment.jpf","test/only-comment.cmp");
  test_pair_srt("test/only-comment.jpf","test/only-comment.cmp");
  test_pair("test/spaces.jpf","test/spaces.cmp");
  test_pair_ert("test/spaces.jpf","test/spaces.cmp");
  test_pair_srt("test/spaces.jpf","test/spaces.cmp");
  test_pair("test/numbers.jpf","test/numbers.cmp");
  test_pair_ert("test/numbers.jpf","test/numbers-ert.cmp");
  test_pair_srt("test/numbers-srt.jpf","test/numbers-srt.cmp");
  test_pair("test/special.jpf","test/special.cmp");
  test_pair_ert("test/special.jpf","test/special.cmp");
  test_pair_srt("test/special.jpf","test/special.cmp");
  test_pair("test/general-dos.jpf","test/general-dos.cmp");
  test_pair("test/heredoc.jpf","test/heredoc.cmp");
  test_pair_ert("test/heredoc.jpf","test/heredoc-ert.cmp");
  test_pair_srt("test/heredoc-srt.jpf","test/heredoc-srt.cmp");
  test_pair("test/heredoc-headeof.jpf","test/heredoc-headeof.cmp");
  test_pair("test/heredoc-hteof.jpf","test/heredoc-hteof.cmp");
  test_pair("test/comments.jpf","test/comments.cmp");
  test_pair_ert("test/comments.jpf","test/comments.cmp");
  test_pair_srt("test/comments.jpf","test/comments.cmp");
  test_pair("test/colon.jpf","test/colon.cmp");
  test_pair_ert("test/colon.jpf","test/colon.cmp");
  test_pair_srt("test/colon.jpf","test/colon-srt.cmp");
  test_pair("test/newlines.jpf","test/newlines.cmp");
  test_pair_ert("test/newlines.jpf","test/newlines.cmp");
  test_pair_srt("test/newlines.jpf","test/newlines-srt.cmp");
  test_pair("test/just-earray.jpf","test/just-earray.cmp");
  test_pair_ert("test/just-earray.jpf","test/just-earray.cmp");
  test_pair_srt("test/just-earray.jpf","test/just-earray.cmp");
  test_pair("test/just-eassoc.jpf","test/just-eassoc.cmp");
  test_pair_ert("test/just-eassoc.jpf","test/just-eassoc.cmp");
  test_pair_srt("test/just-eassoc.jpf","test/just-eassoc.cmp");
  test_pair("test/just-null.jpf","test/just-null.cmp");
  test_pair_ert("test/just-null.jpf","test/just-null.cmp");
  test_pair_srt("test/just-null.jpf","test/just-null.cmp");
  test_pair("test/just-true.jpf","test/just-true.cmp");
  test_pair_ert("test/just-true.jpf","test/just-true.cmp");
  test_pair_srt("test/just-true.jpf","test/just-true.cmp");
  test_pair("test/just-false.jpf","test/just-false.cmp");
  test_pair_ert("test/just-false.jpf","test/just-false.cmp");
  test_pair_srt("test/just-false.jpf","test/just-false.cmp");
  test_pair("test/just-number.jpf","test/just-number.cmp");
  test_pair_ert("test/just-number.jpf","test/just-number.cmp");
  test_pair_srt("test/just-number.jpf","test/just-number.cmp");
  test_pair("test/just-quoted.jpf","test/just-quoted.cmp");
  test_pair_ert("test/just-quoted.jpf","test/just-quoted.cmp");
  test_pair_srt("test/just-quoted.jpf","test/just-quoted-srt.cmp");
  test_pair("test/quoted-comment.jpf","test/quoted-comment.cmp");
  test_pair_ert("test/quoted-comment.jpf","test/quoted-comment.cmp");
  test_pair_srt("test/quoted-comment.jpf","test/quoted-comment-srt.cmp");
  test_pair("test/just-heredoc.jpf","test/just-heredoc.cmp");
  test_pair_ert("test/just-heredoc.jpf","test/just-heredoc.cmp");
  test_pair_srt("test/just-heredoc.jpf","test/just-heredoc-srt.cmp");
  test_pair("test/blank.jpf","test/blank.cmp");
  test_pair_ert("test/blank.jpf","test/blank.cmp");
  test_pair_srt("test/blank.jpf","test/blank.cmp");
  test_pair("test/heredoc-missing.jpf","test/heredoc-missing.cmp");
  test_pair("test/all-indent.jpf","test/all-indent.cmp");
  test_pair_ert("test/all-indent.jpf","test/all-indent.cmp");
  test_pair_srt("test/all-indent.jpf","test/all-indent.cmp");
  test_pair("test/all-indent-back.jpf","test/all-indent-back.cmp");
  test_pair("test/bad-indents.jpf","test/bad-indents.cmp");
  test_pair("test/mixed-types.jpf","test/mixed-types.cmp");
  test_pair("test/duplicate-keys.jpf","test/duplicate-keys.cmp");
  test_pair("test/duplicate-keys-root.jpf","test/duplicate-keys-root.cmp");
  test_pair("test/heredoc-after.jpf","test/heredoc-after.cmp");
  test_pair("test/misc-errors.jpf","test/misc-errors.cmp");
  test_pair("test/long.jpf","test/long.cmp");
  test_pair_ert("test/long.jpf","test/long.cmp");
  test_pair_srt("test/long.jpf","test/long-srt.cmp");
  test_pair("test/ctrl-quoted.jpf","test/ctrl-quoted.cmp");
  test_pair_ert("test/ctrl-quoted.jpf","test/ctrl-quoted.cmp");
  test_pair_srt("test/ctrl-quoted.jpf","test/ctrl-quoted-srt.cmp");
  test_pair("test/ctrl-unquoted.jpf","test/ctrl-unquoted.cmp");
  test_pair("test/ctrl-heredoc.jpf","test/ctrl-heredoc.cmp");
  test_pair("test/nl.jpf","test/nl.cmp");
  test_pair_ert("test/nl.jpf","test/nl.cmp");
  test_pair_srt("test/nl.jpf","test/nl-srt.cmp");
  test_pair("test/bad-colon.jpf","test/bad-colon.cmp");
  test_pair("test/null.jpf","test/null.cmp");
  test_pair("test/missing.jpf","test/missing.cmp");
  test_pair_ert("test/unquoted-punc.jpf","test/unquoted-punc.cmp");
  test_pair_srt("test/unquoted-punc.jpf","test/unquoted-punc.cmp");
  /* Exhaustion */
  exhaust_stack();
  exhaust_memory();
  exhaust_keystr();
  exhaust_flex();
  return 0;
}

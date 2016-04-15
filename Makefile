#
# 'make depend' uses makedepend to automatically generate dependencies 
#               (dependencies are added to end of Makefile)
# 'make'        build executable file 'mycc'
# 'make clean'  removes all .o and executable files
#

CC = gcc
CFLAGS = -std=gnu99 -g -Wall -D_FILE_OFFSET_BITS=64
INCLUDES = -I.
LFLAGS = 
LIBS = -levent -levent_pthreads -lcrypto -lpthread -lm -lfuse -lz
SRCS = running.c request.c source.c sourcelist.c interface.c syncsource.c util/misc.c sources/file2.c sources/meta.c jpf/util.c jpf/parse.c jpf/emit.c util/assoc.c util/array.c util/path.c interfaces/fuse.c util/strbuf.c sources/http/client.c sources/http/http.c util/logging.c config.c util/ranges.c util/hash.c util/event.c util/queue.c syncif.c util/dns.c sources/http/connection.c sources/cache/file.c sources/cache/cache.c sources/cache/mmap.c failures.c hits.c util/rotate.c util/compressor.c util/background.c
OBJS = $(SRCS:.c=.o) jpf/jpflex.yy.o main.o
MAIN = fuse8

.PHONY: depend clean version.c

all: $(MAIN)

$(MAIN): $(OBJS) version.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) version.o $(LFLAGS) $(LIBS)

version.c: version.h
	perl -pe 's/@(.*?)@/$$x=qx($$1); chomp $$x; $$x/e' <version.c.tmpl >version.c

%.o: %.c %.d
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.d : %.c
	$(CC) $(CFLAGS) -MM -MF"$@" -MT"$@" "$<"

jpf/%.yy.c: jpf/%.l
	cd jpf; flex ../$<

clean:
	$(RM) $(OBJS) $(OBJS:.o=.d) jpf/jpflex.yy.c *~ $(MAIN)

-include $(OBJS:.o=.d)

CC = gcc -std=gnu99
CFLAGS  = -g -Wall
LIBS = 
INCLUDES = 
LFLAGS = -lm
SRCS = jpflex.l util.c parse.c emit.c test.c
OBJS = util.o jpflex.yy.o parse.o emit.o test.o
MAIN = jpftest

.PHONY: clean

all: $(MAIN)

$(MAIN): $(OBJS) 
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@
	$(CC) -MM $(CFLAGS) $*.c > $*.d

jpflex.yy.c: jpflex.l
	flex jpflex.l

clean:
	$(RM) *.o *~ $(MAIN) jpflex.yy.c

-include $(OBJS:.o=.d)


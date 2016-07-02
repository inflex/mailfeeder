#
# Wrappers Makefile

#binary files
CC=gcc
PRF=-O0
PROFILE=
CFLAGS= -Wall -Werror -g -I.
OBJS=logger.o mta-connect.o pldstr.o
LIBS=

CFLAGS += $(PROFILE)

all: mailfeeder


.c.o:
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f mailfeeder *.o

install: mailfeeder
	cp mailfeeder /usr/local/bin

mailfeeder: mailfeeder.c  $(OBJS)
	$(CC) $(CFLAGS) mailfeeder.c $(OBJS) -DUSE_HSTRERROR=1 -o mailfeeder $(LIBS)

libc5: mailfeeder.c $(OBJS)
	$(CC) $(CFLAGS) mailfeeder.c $(OBJS) -DUSE_HSTRERROR=0 -o mailfeeder $(LIBS)

solaris: mailfeeder.c $(OBJS)
	OSTYPE="SOLARIS"
	$(CC) $(CFLAGS) -DSOLARIS mailfeeder.c $(OBJS) -DUSE_STRERROR=1 -o mailfeeder -lnsl -lsocket

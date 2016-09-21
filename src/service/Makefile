# Redis Makefile
# Copyright (C) 2009 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file

OPTIMIZATION?=-O3
CFLAGS?= -std=c99 -finline-functions -rdynamic -pedantic $(OPTIMIZATION) -Wall $(ARCH) $(PROF)
CCLINK?= -pthread -ldl
CCOPT= $(CFLAGS) $(CCLINK) $(ARCH) $(PROF)
DEBUG?= -g -rdynamic -ggdb

#INSTALL_TOP= /usr/local
INSTALL_TOP=  ../..
INSTALL_BIN= $(INSTALL_TOP)/bin
INSTALL= cp -p

OBJ = ae.o anet.o redis.o

PRGNAME = uuid-server

all: uuid-server

# Deps (use make dep to generate this)
ae.o: ae.c ae.h
ae_epoll.o: ae_epoll.c
anet.o: anet.c anet.h
redis.o: redis.c ae.h anet.h

uuid-server: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ)

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(PRGNAME) *.o *.gcda *.gcno *.gcov *.so

dep:
	$(CC) -MM *.c

install: all
	$(INSTALL) $(PRGNAME) $(INSTALL_BIN)

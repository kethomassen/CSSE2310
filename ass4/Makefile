CC = gcc
CFLAGS = -Wall -pedantic --std=gnu99 -pthread 
DEBUG = -g
TARGETS = gopher zazu rafiki

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
	CFLAGS += -Ilib/include
	LIB = -Llib -la4
endif

ifeq ($(UNAME), Linux)
	CFLAGS += -I/local/courses/csse2310/include
	LIB = -L/local/courses/csse2310/lib -la4
endif

.DEFAULT: all
.PHONY: all debug cleano clean

all: $(TARGETS)

debug: CFLAGS += $(DEBUG)
debug: $(TARGETS)

shared.o: shared.c shared.h
	$(CC) $(CFLAGS) $(LIB) -c shared.c -o shared.o

zazu: player.c shared.o
	$(CC) $(CFLAGS) $(LIB) shared.o player.c -o zazu

rafiki: server.c shared.o
	$(CC) $(CFLAGS) $(LIB) shared.o server.c -o rafiki

gopher: scores.c shared.o
	$(CC) $(CFLAGS) $(LIB) shared.o scores.c -o gopher

cleano:
	rm -f *.o

clean: cleano
	rm -f $(TARGETS)
	rm -rf *.dSYM


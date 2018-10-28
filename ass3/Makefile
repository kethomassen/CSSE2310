CC = gcc
CFLAGS = -Wall -pedantic --std=gnu99
DEBUG = -g
TARGETS = austerity shenzi banzai ed

.DEFAULT: all
.PHONY: all debug cleano clean

all: $(TARGETS)

debug: CFLAGS += $(DEBUG)
debug: $(TARGETS)

util.o: lib/util.c lib/util.h
	$(CC) $(CFLAGS) -c lib/util.c -o util.o

game.o: lib/game.c lib/game.h
	$(CC) $(CFLAGS) -c lib/game.c -o game.o

player.o: player.c player.h
	$(CC) $(CFLAGS) -c player.c -o player.o

austerity: hub.c util.o game.o
	$(CC) $(CFLAGS) util.o game.o hub.c -o austerity

shenzi: player.o players/shenzi.c util.o game.o
	$(CC) $(CFLAGS) game.o util.o player.o players/shenzi.c -o shenzi

banzai: player.o players/banzai.c util.o game.o
	$(CC) $(CFLAGS) game.o util.o player.o players/banzai.c -o banzai

ed: player.o players/ed.c util.o game.o
	$(CC) $(CFLAGS) game.o util.o players/ed.c player.o -o ed

cleano:
	rm -f *.o

clean: cleano
	rm -f $(TARGETS)
	rm -rf *.dSYM

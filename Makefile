# Battleship - console and raylib builds
#
#   make        -> console version  (./battleship)
#   make gui    -> raylib version   (./main)
#   make clean
#
# If raylib is not installed system-wide, point at it:
#   make gui RAYLIB_INC=-I/path/to/raylib/src RAYLIB_LIB=-L/path/to/raylib/src
#
# The GUI reuses every game-logic file. ui.c / input.c are still linked
# because battleship.c's console handlers reference them, but the GUI
# never calls them.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2

CORE    = battleship.c board.c ai.c score.c utils.c
CONSOLE = main.c ui.c input.c $(CORE)
GUI     = gui.c sound.c ui.c input.c $(CORE)

RAYLIB_INC  ?=
RAYLIB_LIB  ?=
RAYLIB_LIBS  = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

all: battleship

battleship: $(CONSOLE) battleship.h
	$(CC) $(CFLAGS) $(CONSOLE) -o $@

gui: main

main: $(GUI) battleship.h
	$(CC) $(CFLAGS) $(RAYLIB_INC) $(GUI) -o $@ $(RAYLIB_LIB) $(RAYLIB_LIBS)

clean:
	rm -f battleship main

.PHONY: all gui clean

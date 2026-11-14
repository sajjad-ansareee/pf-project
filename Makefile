# Battleship - you must say which version to build:
#
#   make terminal   -> terminal version        (run with ./terminal)
#   make gui        -> raylib GUI + sound      (run with ./gui)
#   make clean      -> delete both executables
#
# Plain "make" builds nothing and prints an error asking you to choose.
#
# If raylib is not installed system-wide, point at it:
#   make gui RAYLIB_INC=-I/path/to/raylib/src RAYLIB_LIB=-L/path/to/raylib/src
#
# The GUI reuses every game-logic file. ui.c / input.c are still linked
# because battleship.c's terminal handlers reference them, but the GUI
# never calls them.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2

CORE         = battleship.c board.c ai.c score.c utils.c
TERMINAL_SRC = main.c ui.c input.c $(CORE)
GUI_SRC      = gui.c sound.c ui.c input.c $(CORE)

RAYLIB_INC  ?=
RAYLIB_LIB  ?=
RAYLIB_LIBS  = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

.DEFAULT_GOAL := usage

usage:
	@echo "Error: choose which version to build." >&2
	@echo "  make terminal   builds the terminal version -> run ./terminal" >&2
	@echo "  make gui        builds the GUI version      -> run ./gui" >&2
	@echo "  make clean      removes both executables" >&2
	@exit 1

terminal: $(TERMINAL_SRC) battleship.h
	$(CC) $(CFLAGS) $(TERMINAL_SRC) -o $@

gui: $(GUI_SRC) sound.h battleship.h
	$(CC) $(CFLAGS) $(RAYLIB_INC) $(GUI_SRC) -o $@ $(RAYLIB_LIB) $(RAYLIB_LIBS)

clean:
	rm -f terminal gui

.PHONY: usage clean
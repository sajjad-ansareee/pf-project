# Battleship in C

A classic **Battleship** game for one player against the computer, written in plain C.
It comes in two versions that share the same game code:

| Version | Interface | Sound | Build command | Run |
|---|---|---|---|---|
| **Terminal** | text, ANSI colours | no | `make terminal` | `./terminal` |
| **GUI** | graphical window ([raylib](https://www.raylib.com/)) | yes | `make gui` | `./gui` |

Made as a Programming Fundamentals term project.

## Features

- Full Battleship rules on a 10x10 grid with the standard 5-ship fleet.
- **Smart computer opponent**: it hunts using probability, then locks onto a ship once it hits it, and keeps going until the ship is sunk.
- Score system and a **top-3 leaderboard** saved to a file.
- GUI: ships drawn with a different colour and shape for each size, mouse controls, hit / miss / sunk animations, and a wait after each enemy shot so you can see where it fired.
- Sound effects (GUI) for fire, miss, hit, ship destroyed, win, lose and draw. They are generated in code, so there are no audio files to download.
- Boards are dynamically allocated 2D arrays, freed on exit.

## How to play

**Goal:** sink all 5 of the enemy's ships before the computer sinks yours.

**The fleet** (each side has one of each):

| Ship | Size |
|---|---|
| Aircraft Carrier | 5 |
| Battleship | 4 |
| Submarine | 3 |
| Cruiser | 3 |
| Destroyer | 2 |

**Rules**

1. **Place your fleet.** Ships go horizontally or vertically (never diagonally), must stay inside the grid, and cannot overlap. You can place them by hand or use **Random**. The computer places its fleet randomly in secret.
2. **Take turns firing.** You call a square such as `B5` (row letter A-J, column number 1-10). You fire first.
3. **Read the result.** The game tells you **miss**, **hit**, or **sunk** (when every square of a ship has been hit, and it names the ship).
4. **No repeats.** You cannot shoot the same square twice.
5. **The first side to sink the whole enemy fleet wins.** Both boards are then revealed.

**Scoring:** you start with **100 points** and lose **1 point per shot you fire**. Fewer shots means a higher score. If you win and your score is in the top 3, you enter your name for the leaderboard.

### Controls: terminal version

- Menus: type the number of an option and press Enter.
- **Fleet formation** menu: `1` place manually, `2` random, `3` reset, `4` confirm and start, `5` back.
- Manual placement: type the start square (e.g. `C3`), then `H` or `V`. The ship extends **right** (H) or **down** (V) from that square.
- Battle: type a target square, e.g. `B5`.

### Controls: GUI version

- **Menus and buttons:** left-click.
- **Placing ships:** click a ship in the list on the right, then click your grid. A green preview means the spot is legal, red means it is not.
- **Rotate:** press **R** or right-click. The ship extends right or down from the square you click.
- **Move a ship:** click an already-placed ship to pick it up again.
- **Firing:** click a square on the **Enemy Waters** grid (right side).
- **Waiting:** after the computer fires, you must wait about 1 second ("Wait 0.8s" in the centre panel) while its shot is highlighted on your board.
- **Mute:** press **M**.
- There is no Esc-to-quit, so you cannot leave a battle by accident. Close the window to quit.

**Reading the screen:** ships are told apart by size and colour (Carrier purple, Battleship orange, Submarine teal, Cruiser yellow, Destroyer silver). A red burst is a hit and a white ring is a miss. Sunk ships turn grey, and the small ship icons under each board get a red cross when that ship is sunk.

## Building

You need a C compiler (`gcc` or `cc`) and `make`. The terminal version needs nothing else.

### Which `make` command builds what

You must say which version you want. Plain `make` builds nothing and prints an error.

| Command | What it does | Run it with |
|---|---|---|
| `make terminal` | Builds the **terminal** version | `./terminal` |
| `make gui` | Builds the **GUI version with sound** (needs raylib) | `./gui` |
| `make clean` | Deletes the `terminal` and `gui` executables | none |
| `make` (nothing else) | **Error**: asks you to choose `terminal` or `gui` | none |

What plain `make` prints:

```
Error: choose which version to build.
  make terminal   builds the terminal version -> run ./terminal
  make gui        builds the GUI version      -> run ./gui
  make clean      removes both executables
```

Notes:

- **Sound is part of the GUI build.** There is no separate sound build. `sound.c` is compiled into `make gui`, and the terminal version has no sound.
- The two builds are independent. Building one does not affect the other, and you can have both executables at once.
- If you change a `.c` or `.h` file, run the same `make` command again to rebuild.
- Older versions of this project built executables named `battleship` and `main`. They are no longer produced, so you can delete them by hand.


## How the computer plays

- **Hunt mode:** with no unresolved hits, it counts how many ways each surviving ship could fit through every square and fires at the square covered by the most. Ties are broken randomly.
- **Target mode:** after a hit, it fires only next to the hit. Once two hits line up, it locks the direction and works along that line. Squares are ranked by how many legal ship placements pass through them, with placements that explain more hits counting much more.
- **After a sink:** only that ship's squares are cleared. If hits from another ship remain, it keeps targeting them. Otherwise it goes back to hunting.
- It never reads your board. It only learns from hit / miss / sunk, like a human opponent.

In a simulation of 5,000 games against random fleets it sank a whole fleet in about **45 shots on average**, compared with about **95** for a purely random shooter.

## Project structure

| File | Purpose |
|---|---|
| `battleship.h` | Shared header: constants, types and every function prototype |
| `board.c` | Ships, boards, placement rules, random fleets, shooting and sunk detection |
| `ai.c` | The computer player (hunt / target logic) |
| `score.c` | Score tracking and the top-3 leaderboard file |
| `utils.c` | Random-number helpers |
| `battleship.c` | Game state machine and battle / formation flow (shared by both versions) |
| `main.c` | Entry point of the **terminal** version |
| `input.c`, `ui.c` | Terminal input and screen drawing |
| `gui.c` | The **GUI** version: all screens, mouse input and animations (raylib) |
| `sound.c`, `sound.h` | Sound effects, generated in code (raylib audio) |
| `Makefile` | Build rules: `make terminal` and `make gui` |
| `leaderboard.txt` | Created at runtime in the folder you run the game from |

The game logic (`board.c`, `ai.c`, `score.c`, `battleship.c`) is shared, so the rules behave identically in both versions. The GUI is built with `ui.c` and `input.c` for linking reasons only. It never calls them.

## Leaderboard file

Scores are stored in `leaderboard.txt` in the folder you run the game from. Each line is `<score> <name>`, best first, up to 3 lines. Delete the file to reset the leaderboard.
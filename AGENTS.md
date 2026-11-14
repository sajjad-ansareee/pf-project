# AGENTS.md

Guidance for AI coding agents working on this repository.

## Project overview

Battleship (Player vs Computer) written in plain C11. There are two front ends
that share the same game logic:

- **Terminal version** – console UI (`main.c`, `ui.c`, `input.c`).
- **GUI version** – raylib front end with procedurally generated sound
  (`gui.c`, `sound.c`).

All files live in the repository root; there are no subdirectories.

## Build and run

A version must always be chosen. Plain `make` builds nothing and exits with an
error.

```sh
make terminal   # builds ./terminal
./terminal

make gui        # builds ./gui (needs raylib)
./gui

make clean      # removes ./terminal and ./gui
```

If raylib is not installed system-wide:

```sh
make gui RAYLIB_INC=-I/path/to/raylib/src RAYLIB_LIB=-L/path/to/raylib/src
```

Compiler flags: `-std=c11 -Wall -Wextra -O2`. Code must compile with no
warnings under these flags. For a stricter check, also try
`-Wconversion -Wshadow`.

`battleship` and `main` in the root are stale binaries from an older Makefile;
do not rely on them and do not commit new binaries.

## File map

| File            | Responsibility                                                    |
|-----------------|-------------------------------------------------------------------|
| `battleship.h`  | All shared types, constants and prototypes (the single API spec)  |
| `battleship.c`  | Game state machine, turn flow, fleet formation flow               |
| `board.c`       | Board allocation, ship placement, shooting, sunk detection        |
| `ai.c`          | Computer player: HUNT (probability density) and TARGET modes      |
| `score.c`       | Score tracking and top-3 leaderboard file I/O                     |
| `utils.c`       | Random number helpers (`seed_random`, `random_int`)               |
| `ui.c`          | Terminal output – the only terminal code allowed to print         |
| `input.c`       | Terminal input – the only code allowed to read stdin              |
| `main.c`        | Terminal entry point                                              |
| `gui.c`         | raylib front end (screens do input + drawing per frame)           |
| `sound.c/.h`    | Sound effects synthesised at start-up (no audio files)            |
| `leaderboard.txt` | Persisted top-3 leaderboard (`score name` per line)             |

The GUI still links `ui.c` and `input.c` because `battleship.c`'s terminal
handlers reference them, but it never calls them.

## Design rules (from `battleship.h`)

- Every function does exactly one job; keep functions small.
- Boards are dynamically allocated 2D arrays (`board_create` / `board_destroy`).
  Free everything you allocate; destroy functions must be safe on `NULL`.
- Game logic never prints and never reads input. Only `ui_*` / `input_*`
  (terminal) or `gui.c` (GUI) touch I/O.
- The AI never reads the human's board directly; it only learns from
  `ShotOutcome` values.
- State transitions go through `game_set_state()`. Flow:
  `MAIN_MENU -> FLEET_FORMATION -> BATTLE -> GAME_OVER -> (NAME_ENTRY) -> LEADERBOARD -> MAIN_MENU ... EXIT`.
- `sound.c` uses its own RNG so the game's `rand()` sequence is not disturbed.
  If no audio device exists, sound functions must silently do nothing.

## Code style

- C11, 4-space indentation, braces on their own line for functions and on the
  same line for control statements.
- Function names are `snake_case` with a module prefix (`board_`, `ai_`,
  `score_`, `leaderboard_`, `ui_`, `input_`, `battle_`, `formation_`, `sound_`).
- Types are `PascalCase`; enum constants and macros are `UPPER_SNAKE_CASE`;
  struct fields are `camelCase`.
- Each file starts with a `/* ===...=== */` banner comment describing it;
  sections are separated with the same banner style.
- Declare new public functions in `battleship.h` (or `sound.h`) with a short
  trailing comment, following the existing grouping.

## Workflow for changes

1. Keep changes minimal and consistent with the rules above.
2. Build both versions (`make terminal` and `make gui`) and make sure there are
   no warnings.
3. There is no automated test suite; verify behaviour by running the
   affected executable.
4. Do not commit build outputs (`terminal`, `gui`, `battleship`, `main`).

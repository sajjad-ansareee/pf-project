/* ============================================================================
 *  battleship.h  -  Battleship (Player vs Computer), plain C, console UI
 *
 *  DESIGN RULES
 *    - Every function does exactly one job.
 *    - Boards are dynamically allocated 2D arrays (Board_Create / Board_Destroy).
 *    - Game logic never prints and never reads input. Only ui_* / input_*
 *      functions touch stdin/stdout.
 *    - The AI never reads the human's board directly. It only sees ShotOutcome
 *      values, exactly like a real opponent hearing "hit / miss / sunk".
 *
 *  CORE WORKFLOW
 *    STATE_MAIN_MENU -> STATE_FLEET_FORMATION -> STATE_BATTLE
 *        -> STATE_GAME_OVER (reveal both boards) -> STATE_NAME_ENTRY
 *        (only if the human won and the score qualifies) -> STATE_LEADERBOARD
 *        -> STATE_MAIN_MENU ... STATE_EXIT
 * ========================================================================== */

#ifndef BATTLESHIP_H
#define BATTLESHIP_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 *  1. CONSTANTS
 * ========================================================================== */

#define BOARD_SIZE          10
#define TOTAL_CELLS         (BOARD_SIZE * BOARD_SIZE)
#define FLEET_SIZE          5
#define MAX_SHIP_LENGTH     5
#define STARTING_SCORE      100
#define LEADERBOARD_SLOTS   3
#define MAX_NAME_LENGTH     20
#define LEADERBOARD_FILE    "leaderboard.txt"
#define MAX_AI_CANDIDATES   TOTAL_CELLS

/* ============================================================================
 *  2. BASIC TYPES
 * ========================================================================== */

/* Zero-based. row 0..9 maps to letters A..J, col 0..9 maps to digits 1..10. */
typedef struct {
    int row;
    int col;
} Coord;

typedef enum {
    ORIENT_HORIZONTAL,
    ORIENT_VERTICAL,
    ORIENT_UNKNOWN          /* used by the AI before it has two hits in a line */
} Orientation;

typedef enum {
    SHIP_CARRIER,           /* 5 */
    SHIP_BATTLESHIP,        /* 4 */
    SHIP_SUBMARINE,         /* 3 */
    SHIP_CRUISER,           /* 3 */
    SHIP_DESTROYER,         /* 2 */
    SHIP_NONE = -1
} ShipType;

/* State of one cell on a board. */
typedef enum {
    CELL_WATER,             /* nothing here, not yet shot   */
    CELL_SHIP,              /* ship segment, not yet shot   */
    CELL_MISS,              /* shot, water                  */
    CELL_HIT                /* shot, ship segment           */
} CellState;

/* What the AI "knows" about a cell of the human's board. */
typedef enum {
    KNOWN_UNKNOWN,          /* never shot                   */
    KNOWN_MISS,
    KNOWN_HIT,              /* hit, ship not yet sunk       */
    KNOWN_SUNK              /* hit and part of a sunk ship  */
} KnowledgeState;

typedef enum {
    SHOT_INVALID,           /* out of bounds or already shot (turn NOT consumed) */
    SHOT_MISS,
    SHOT_HIT,
    SHOT_SUNK
} ShotResult;

typedef enum {
    PLAYER_HUMAN,
    PLAYER_COMPUTER
} PlayerId;

/* ============================================================================
 *  3. SHIPS AND BOARDS
 * ========================================================================== */

typedef struct {
    ShipType    type;
    int         length;
    int         hits;                   /* segments hit so far          */
    bool        placed;
    Coord       start;
    Orientation orientation;
} Ship;

typedef struct {
    CellState **cells;                  /* [BOARD_SIZE][BOARD_SIZE]     */
    int       **shipIndex;              /* fleet index at a cell, -1 if water */
    Ship        fleet[FLEET_SIZE];
} Board;

/* Everything the caller (and the AI) learns from one shot. */
typedef struct {
    ShotResult result;
    Coord      target;
    ShipType   sunkType;                /* valid only if result == SHOT_SUNK */
    Coord      sunkCells[MAX_SHIP_LENGTH];
    int        sunkLength;              /* valid only if result == SHOT_SUNK */
} ShotOutcome;

/* ---- Board lifecycle (memory management) ---- */
Board *board_create(void);                              /* allocates both 2D arrays, returns NULL on failure */
void   board_destroy(Board *board);                     /* frees everything, safe on NULL                    */
void   board_clear(Board *board);                       /* resets cells to WATER, shipIndex to -1, fleet to unplaced */

/* ---- Fleet setup data ---- */
void   fleet_init(Ship fleet[FLEET_SIZE]);              /* sets types/lengths, hits = 0, placed = false */
int    ship_length_for_type(ShipType type);
const char *ship_name_for_type(ShipType type);

/* ---- Coordinate helpers ---- */
bool   coord_is_in_bounds(Coord c);
bool   coord_equals(Coord a, Coord b);
Coord  coord_offset(Coord c, int dRow, int dCol);       /* no bounds check, pure arithmetic */
bool   coord_parse(const char *text, Coord *out);       /* "B5" / "j10" -> Coord, false if malformed */
void   coord_format(Coord c, char buffer[4]);           /* Coord -> "B5" */

/* ---- Placement (one rule per function) ---- */
void   ship_compute_cells(Coord start, Orientation o, int length, Coord out[MAX_SHIP_LENGTH]);
bool   placement_fits_in_bounds(Coord start, Orientation o, int length);
bool   placement_is_free(const Board *board, Coord start, Orientation o, int length);
bool   placement_is_valid(const Board *board, Coord start, Orientation o, int length);
bool   board_place_ship(Board *board, int fleetIndex, Coord start, Orientation o);
bool   board_remove_ship(Board *board, int fleetIndex); /* lets the human reposition during formation */
bool   board_fleet_is_complete(const Board *board);     /* all 5 ships placed */

/* ---- Random fleet generation (computer, plus an "auto place" option for the human) ---- */
Orientation random_orientation(void);
Coord  random_coord(void);
bool   board_place_ship_randomly(Board *board, int fleetIndex);   /* retries until a valid spot is found */
void   board_place_fleet_randomly(Board *board);

/* ---- Shooting (applies rules, mutates the defender's board) ---- */
bool        board_is_already_shot(const Board *board, Coord target);
ShotResult  board_classify_shot(const Board *board, Coord target);     /* read-only: what WOULD happen */
void        board_mark_shot(Board *board, Coord target, ShotResult r);
ShotOutcome board_receive_shot(Board *board, Coord target);            /* classify + mark + sunk detection */
bool        ship_is_sunk(const Ship *ship);
void        ship_collect_cells(const Ship *ship, Coord out[MAX_SHIP_LENGTH]);
bool        board_all_ships_sunk(const Board *board);
int         board_count_ships_remaining(const Board *board);

/* ============================================================================
 *  4. THE COMPUTER PLAYER (INTELLIGENT SHOT SELECTION)
 *
 *  Strategy, in order of priority:
 *
 *  TARGET MODE (there is at least one unresolved hit):
 *     a) Exactly one unresolved hit  -> candidates are its 4 neighbours.
 *     b) Two or more hits in a line  -> LOCK orientation, only shoot along
 *        that line, at either END of the hit run.
 *     c) One end blocked (miss / edge / already shot) -> go to the other end.
 *     d) Ship sunk -> remove ONLY that ship's cells from the unresolved list.
 *        If hits from a different ship remain, stay in TARGET MODE on them.
 *        Otherwise return to HUNT MODE.
 *
 *  HUNT MODE (no unresolved hits):
 *     Probability-density search. For every remaining (unsunk) ship length,
 *     count how many legal placements cover each unknown cell. Shoot the
 *     cell with the highest count (ties broken randomly). Cells that cannot
 *     fit the smallest remaining ship are never worth shooting, and the
 *     checkerboard parity naturally emerges from this method.
 *     Falls back to a plain random unknown cell if the map is all zeros.
 * ========================================================================== */

typedef enum {
    AI_MODE_HUNT,
    AI_MODE_TARGET
} AiMode;

typedef struct {
    KnowledgeState **view;                          /* AI's picture of the human board [10][10]       */
    AiMode           mode;

    bool             shipAlive[FLEET_SIZE];         /* which enemy ships the AI believes are afloat   */

    Coord            activeHits[TOTAL_CELLS];       /* hits belonging to ships NOT yet sunk           */
    int              activeHitCount;

    Orientation      lockedOrientation;             /* ORIENT_UNKNOWN until 2+ aligned hits           */

    Coord            candidates[MAX_AI_CANDIDATES]; /* next-shot candidates in TARGET mode            */
    int              candidateCount;

    int              probability[BOARD_SIZE][BOARD_SIZE];   /* scratch map used in HUNT mode          */
} AiPlayer;

/* ---- Lifecycle ---- */
AiPlayer *ai_create(void);                          /* allocates view, runs ai_reset */
void      ai_destroy(AiPlayer *ai);
void      ai_reset(AiPlayer *ai);                   /* new game: all UNKNOWN, all ships alive, HUNT mode */

/* ---- Top-level decision (the only function the game loop calls to get a move) ---- */
Coord     ai_choose_next_shot(AiPlayer *ai);
void      ai_register_outcome(AiPlayer *ai, const ShotOutcome *outcome);   /* feed the result back in */

/* ---- Mode selection ---- */
AiMode    ai_decide_mode(const AiPlayer *ai);       /* TARGET if activeHitCount > 0, else HUNT */
void      ai_update_mode(AiPlayer *ai);

/* ---- HUNT mode: probability density ---- */
Coord     ai_choose_hunt_shot(AiPlayer *ai);
void      ai_clear_probability_map(AiPlayer *ai);
void      ai_build_probability_map(AiPlayer *ai);   /* loops over alive ships and both orientations */
bool      ai_placement_is_possible(const AiPlayer *ai, Coord start, Orientation o, int length);
void      ai_add_placement_to_map(AiPlayer *ai, Coord start, Orientation o, int length);
int       ai_smallest_alive_ship_length(const AiPlayer *ai);
Coord     ai_pick_best_from_map(const AiPlayer *ai);        /* max value, random tie-break */
Coord     ai_pick_random_unknown(const AiPlayer *ai);       /* last-resort fallback        */

/* ---- TARGET mode: follow up on hits ---- */
Coord     ai_choose_target_shot(AiPlayer *ai);
void      ai_rebuild_candidates(AiPlayer *ai);              /* recomputes candidate list from activeHits */
void      ai_add_neighbour_candidates(AiPlayer *ai, Coord hit);        /* up/down/left/right of a lone hit   */
void      ai_add_line_end_candidates(AiPlayer *ai);                    /* both ends of the locked hit run    */
void      ai_add_candidate_if_valid(AiPlayer *ai, Coord c);            /* in bounds, UNKNOWN, not duplicate  */
bool      ai_candidate_exists(const AiPlayer *ai, Coord c);
Coord     ai_pick_candidate(AiPlayer *ai);                  /* prefers candidates that extend the longest run */
void      ai_remove_candidate(AiPlayer *ai, Coord c);

/* ---- Orientation reasoning ---- */
void        ai_update_orientation(AiPlayer *ai);            /* locks H/V when two hits share a row or column */
Orientation ai_detect_orientation(const Coord *hits, int count);
void        ai_get_hit_run_bounds(const AiPlayer *ai, Coord *first, Coord *last);  /* extremes along the locked line */

/* ---- Learning from results ---- */
void      ai_record_miss(AiPlayer *ai, Coord c);
void      ai_record_hit(AiPlayer *ai, Coord c);
void      ai_record_sunk(AiPlayer *ai, const ShotOutcome *outcome);     /* marks cells SUNK, drops them from activeHits, kills the ship */
void      ai_remove_active_hit(AiPlayer *ai, Coord c);
bool      ai_is_unknown(const AiPlayer *ai, Coord c);

/* ============================================================================
 *  5. SCORING
 * ========================================================================== */

typedef struct {
    int score;              /* starts at STARTING_SCORE            */
    int shotsFired;
} ScoreTracker;

void  score_init(ScoreTracker *s);
void  score_register_shot(ScoreTracker *s);   /* shotsFired++, score-- (never below 0) */
int   score_get(const ScoreTracker *s);

/* ============================================================================
 *  6. LEADERBOARD (FILE HANDLING)
 * ========================================================================== */

typedef struct {
    char name[MAX_NAME_LENGTH + 1];
    int  score;
} LeaderboardEntry;

typedef struct {
    LeaderboardEntry entries[LEADERBOARD_SLOTS];
    int              count;
} Leaderboard;

void  leaderboard_init(Leaderboard *lb);                          /* empty */
bool  leaderboard_load(Leaderboard *lb, const char *path);        /* missing file = empty board, not an error */
bool  leaderboard_save(const Leaderboard *lb, const char *path);
bool  leaderboard_qualifies(const Leaderboard *lb, int score);    /* room left OR beats the lowest entry */
void  leaderboard_insert(Leaderboard *lb, const char *name, int score);
void  leaderboard_sort(Leaderboard *lb);                          /* descending by score */

/* ============================================================================
 *  7. GAME STATE MACHINE
 * ========================================================================== */

typedef enum {
    STATE_MAIN_MENU,
    STATE_FLEET_FORMATION,
    STATE_BATTLE,
    STATE_GAME_OVER,        /* reveals both boards, announces the winner */
    STATE_NAME_ENTRY,       /* only reached when the human wins with a qualifying score */
    STATE_LEADERBOARD,
    STATE_EXIT
} GameState;

typedef struct {
    GameState    state;
    Board       *humanBoard;
    Board       *computerBoard;
    AiPlayer    *ai;
    ScoreTracker score;
    Leaderboard  leaderboard;
    PlayerId     currentTurn;
    PlayerId     winner;
    bool         hasWinner;
    char         statusMessage[80];     /* last event line shown by the UI, e.g. "You sank the Cruiser!" */
} Game;

/* ---- Lifecycle ---- */
bool  game_init(Game *game);                    /* allocates boards + AI, loads leaderboard, state = MAIN_MENU */
void  game_shutdown(Game *game);                /* frees everything allocated by game_init / game_start_new */
void  game_start_new(Game *game);               /* resets boards, AI, score, winner for a fresh match */
void  game_set_state(Game *game, GameState next);

/* ---- Main loop: dispatches to exactly one handler per state ---- */
void  game_run(Game *game);
void  game_step(Game *game);

/* ---- One handler per state. Each returns by calling game_set_state. ---- */
void  handle_main_menu(Game *game);
void  handle_fleet_formation(Game *game);
void  handle_battle(Game *game);
void  handle_game_over(Game *game);
void  handle_name_entry(Game *game);
void  handle_leaderboard(Game *game);

/* ---- Battle turn flow (small steps, so each is testable) ---- */
ShotOutcome  battle_human_turn(Game *game);     /* asks for a coordinate until a legal one is entered */
ShotOutcome  battle_computer_turn(Game *game);  /* ai_choose_next_shot -> receive_shot -> ai_register_outcome */
void         battle_apply_human_shot(Game *game, Coord target, ShotOutcome *out);
void         battle_apply_computer_shot(Game *game, Coord target, ShotOutcome *out);
void         battle_switch_turn(Game *game);
bool         battle_is_over(const Game *game);
void         battle_declare_winner(Game *game);
void         battle_build_status_message(Game *game, PlayerId shooter, const ShotOutcome *outcome);

/* ---- Post-game decisions ---- */
GameState    next_state_after_game_over(const Game *game);   /* NAME_ENTRY if human won and qualifies, else LEADERBOARD */

/* ============================================================================
 *  8. FLEET FORMATION FLOW (human)
 * ========================================================================== */

typedef enum {
    FORMATION_MANUAL,
    FORMATION_RANDOM,
    FORMATION_RESET,
    FORMATION_CONFIRM,
    FORMATION_BACK
} FormationChoice;

FormationChoice formation_prompt_choice(void);
bool  formation_place_one_manually(Game *game, int fleetIndex);   /* prompts coord + orientation, validates, places */
void  formation_place_all_manually(Game *game);                   /* loops formation_place_one_manually */
void  formation_reset(Game *game);                                 /* board_clear on the human board */
bool  formation_can_confirm(const Game *game);                     /* board_fleet_is_complete */

/* ============================================================================
 *  9. INPUT (the only functions allowed to read stdin)
 * ========================================================================== */

bool  input_read_line(char *buffer, size_t size);                  /* safe fgets wrapper, strips newline */
int   input_read_menu_choice(int minChoice, int maxChoice);
Coord input_read_coord(const char *prompt);                        /* loops until coord_parse succeeds */
Orientation input_read_orientation(void);                          /* H / V */
void  input_read_player_name(char name[MAX_NAME_LENGTH + 1]);      /* trims, rejects empty, truncates */
void  input_wait_for_enter(void);

/* ============================================================================
 *  10. UI (the only functions allowed to print)
 * ========================================================================== */

void  ui_clear_screen(void);
void  ui_print_main_menu(void);
void  ui_print_formation_screen(const Board *humanBoard);
void  ui_print_battle_screen(const Board *humanBoard, const Board *computerBoard,
                             const ScoreTracker *score, const char *statusMessage);
void  ui_print_reveal_screen(const Board *humanBoard, const Board *computerBoard, PlayerId winner);
void  ui_print_name_prompt(int score);
void  ui_print_leaderboard(const Leaderboard *lb);
void  ui_print_message(const char *message);

/* ---- Board rendering building blocks ---- */
void  ui_print_column_header(void);
void  ui_print_board_row(const Board *board, int row, bool revealShips);
void  ui_print_board(const Board *board, bool revealShips);        /* revealShips = false hides CELL_SHIP */
void  ui_print_two_boards_side_by_side(const Board *left, bool revealLeft,
                                       const Board *right, bool revealRight);
char  ui_symbol_for_cell(CellState state, bool revealShips);       /* '~' water, 'S' ship, 'X' hit, 'o' miss */
void  ui_print_fleet_status(const Board *board);                   /* which ships are afloat */

/* ============================================================================
 *  11. ENTRY POINT SKETCH (for main.c)
 *
 *      int main(void) {
 *          Game game;
 *          seed_random();
 *          if (!game_init(&game)) return 1;
 *          game_run(&game);
 *          game_shutdown(&game);
 *          return 0;
 *      }
 * ========================================================================== */

void  seed_random(void);                        /* calls srand(time(NULL)) exactly once */
int   random_int(int minInclusive, int maxInclusive);

#endif /* BATTLESHIP_H */

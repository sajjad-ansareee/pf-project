/* ============================================================================
 *  game.c  -  Game running: lifecycle, state machine, battle / formation flow
 *
 *  Implements sections 7 and 8 of battleship.h.
 *
 *  This file contains NO printing and NO raw input. It only orchestrates:
 *      board_*   (board.c)        ai_*      (ai.c)
 *      ui_*      (ui.c)           input_*   (input.c)
 *      score_*, leaderboard_*     (score.c / leaderboard.c)
 *
 *  Each game_step() runs exactly ONE iteration of the current state's handler,
 *  so the state machine stays flat and every handler stays small.
 * ========================================================================== */

#include "battleship.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 *  Local constants and helpers
 * ------------------------------------------------------------------------- */

typedef enum {
    MENU_NEW_GAME   = 1,
    MENU_LEADERBOARD = 2,
    MENU_EXIT       = 3
} MenuChoice;

#define MENU_FIRST  MENU_NEW_GAME
#define MENU_LAST   MENU_EXIT

static void set_status(Game *game, const char *text)
{
    snprintf(game->statusMessage, sizeof game->statusMessage, "%s", text);
}

/* Fallback if the AI ever proposes an illegal cell: first cell not yet shot. */
static Coord first_unshot_cell(const Board *board)
{
    Coord c;
    for (c.row = 0; c.row < BOARD_SIZE; c.row++) {
        for (c.col = 0; c.col < BOARD_SIZE; c.col++) {
            if (!board_is_already_shot(board, c)) {
                return c;
            }
        }
    }
    c.row = 0;
    c.col = 0;
    return c;
}

static void describe_shot(char *buffer, size_t size, const char *who, const ShotOutcome *outcome)
{
    char coordText[4];
    coord_format(outcome->target, coordText);

    switch (outcome->result) {
        case SHOT_HIT:
            snprintf(buffer, size, "%s: hit %s", who, coordText);
            break;
        case SHOT_MISS:
            snprintf(buffer, size, "%s: miss %s", who, coordText);
            break;
        case SHOT_SUNK:
            snprintf(buffer, size, "%s: sank %s (%s)",
                     who, ship_name_for_type(outcome->sunkType), coordText);
            break;
        default:
            snprintf(buffer, size, "%s: invalid shot", who);
            break;
    }
}

/* ============================================================================
 *  LIFECYCLE
 * ========================================================================== */

bool game_init(Game *game)
{
    memset(game, 0, sizeof *game);

    game->humanBoard    = board_create();
    game->computerBoard = board_create();
    game->ai            = ai_create();

    if (game->humanBoard == NULL || game->computerBoard == NULL || game->ai == NULL) {
        game_shutdown(game);
        return false;
    }

    leaderboard_init(&game->leaderboard);
    leaderboard_load(&game->leaderboard, LEADERBOARD_FILE);   /* missing file = empty board */

    score_init(&game->score);
    game->currentTurn = PLAYER_HUMAN;
    game->hasWinner   = false;
    set_status(game, "");
    game_set_state(game, STATE_MAIN_MENU);
    return true;
}

void game_shutdown(Game *game)
{
    board_destroy(game->humanBoard);
    board_destroy(game->computerBoard);
    game->humanBoard    = NULL;
    game->computerBoard = NULL;

    if (game->ai != NULL) {
        ai_destroy(game->ai);
        game->ai = NULL;
    }
}

void game_start_new(Game *game)
{
    board_clear(game->humanBoard);
    board_clear(game->computerBoard);
    board_place_fleet_randomly(game->computerBoard);

    ai_reset(game->ai);
    score_init(&game->score);

    game->currentTurn = PLAYER_HUMAN;
    game->winner      = PLAYER_HUMAN;
    game->hasWinner   = false;
    set_status(game, "Fire at will!");
}

void game_set_state(Game *game, GameState next)
{
    game->state = next;
}

/* ============================================================================
 *  MAIN LOOP
 * ========================================================================== */

void game_run(Game *game)
{
    while (game->state != STATE_EXIT) {
        game_step(game);
    }
}

void game_step(Game *game)
{
    switch (game->state) {
        case STATE_MAIN_MENU:       handle_main_menu(game);       break;
        case STATE_FLEET_FORMATION: handle_fleet_formation(game); break;
        case STATE_BATTLE:          handle_battle(game);          break;
        case STATE_GAME_OVER:       handle_game_over(game);       break;
        case STATE_NAME_ENTRY:      handle_name_entry(game);      break;
        case STATE_LEADERBOARD:     handle_leaderboard(game);     break;
        case STATE_EXIT:            break;
        default:                    game_set_state(game, STATE_EXIT); break;   /* corrupted state: bail out safely */
    }
}

/* ============================================================================
 *  STATE HANDLERS
 * ========================================================================== */

void handle_main_menu(Game *game)
{
    ui_clear_screen();
    ui_print_main_menu();                       /* expected options: 1 New Game, 2 Leaderboard, 3 Exit */

    switch ((MenuChoice)input_read_menu_choice(MENU_FIRST, MENU_LAST)) {
        case MENU_NEW_GAME:
            game_start_new(game);
            game_set_state(game, STATE_FLEET_FORMATION);
            break;
        case MENU_LEADERBOARD:
            game_set_state(game, STATE_LEADERBOARD);
            break;
        case MENU_EXIT:
            game_set_state(game, STATE_EXIT);
            break;
    }
}

void handle_fleet_formation(Game *game)
{
    ui_clear_screen();
    ui_print_formation_screen(game->humanBoard);

    switch (formation_prompt_choice()) {
        case FORMATION_MANUAL:
            formation_reset(game);
            formation_place_all_manually(game);
            break;

        case FORMATION_RANDOM:
            formation_reset(game);
            board_place_fleet_randomly(game->humanBoard);
            break;

        case FORMATION_RESET:
            formation_reset(game);
            break;

        case FORMATION_CONFIRM:
            if (formation_can_confirm(game)) {
                game_set_state(game, STATE_BATTLE);
            } else {
                ui_print_message("Place all five ships before confirming.");
                input_wait_for_enter();
            }
            break;

        case FORMATION_BACK:
            game_set_state(game, STATE_MAIN_MENU);
            break;
    }
}

void handle_battle(Game *game)
{
    ui_clear_screen();
    ui_print_battle_screen(game->humanBoard, game->computerBoard,
                           &game->score, game->statusMessage);

    /* --- Human shoots --- */
    battle_human_turn(game);
    if (battle_is_over(game)) {
        battle_declare_winner(game);
        game_set_state(game, STATE_GAME_OVER);
        return;
    }
    battle_switch_turn(game);

    /* --- Computer shoots --- */
    battle_computer_turn(game);
    if (battle_is_over(game)) {
        battle_declare_winner(game);
        game_set_state(game, STATE_GAME_OVER);
        return;
    }
    battle_switch_turn(game);
}

void handle_game_over(Game *game)
{
    ui_clear_screen();
    ui_print_reveal_screen(game->humanBoard, game->computerBoard, game->winner);
    input_wait_for_enter();
    game_set_state(game, next_state_after_game_over(game));
}

void handle_name_entry(Game *game)
{
    char name[MAX_NAME_LENGTH + 1];

    ui_clear_screen();
    ui_print_name_prompt(score_get(&game->score));
    input_read_player_name(name);

    leaderboard_insert(&game->leaderboard, name, score_get(&game->score));
    if (!leaderboard_save(&game->leaderboard, LEADERBOARD_FILE)) {
        ui_print_message("Warning: could not save the leaderboard file.");
    }
    game_set_state(game, STATE_LEADERBOARD);
}

void handle_leaderboard(Game *game)
{
    ui_clear_screen();
    ui_print_leaderboard(&game->leaderboard);
    input_wait_for_enter();
    game_set_state(game, STATE_MAIN_MENU);
}

/* ============================================================================
 *  BATTLE TURN FLOW
 * ========================================================================== */

ShotOutcome battle_human_turn(Game *game)
{
    ShotOutcome outcome;
    Coord target;

    for (;;) {
        target = input_read_coord("Enter target (e.g. B5): ");
        if (board_classify_shot(game->computerBoard, target) != SHOT_INVALID) {
            break;
        }
        ui_print_message("Already shot there (or out of range). Try again.");
    }

    battle_apply_human_shot(game, target, &outcome);
    return outcome;
}

ShotOutcome battle_computer_turn(Game *game)
{
    ShotOutcome outcome;
    Coord target = ai_choose_next_shot(game->ai);

    if (board_classify_shot(game->humanBoard, target) == SHOT_INVALID) {
        target = first_unshot_cell(game->humanBoard);         /* safety net, should never trigger */
    }

    battle_apply_computer_shot(game, target, &outcome);
    return outcome;
}

void battle_apply_human_shot(Game *game, Coord target, ShotOutcome *out)
{
    *out = board_receive_shot(game->computerBoard, target);
    score_register_shot(&game->score);
    battle_build_status_message(game, PLAYER_HUMAN, out);
}

void battle_apply_computer_shot(Game *game, Coord target, ShotOutcome *out)
{
    *out = board_receive_shot(game->humanBoard, target);
    ai_register_outcome(game->ai, out);                       /* the AI learns only from the outcome */
    battle_build_status_message(game, PLAYER_COMPUTER, out);
}

void battle_switch_turn(Game *game)
{
    game->currentTurn = (game->currentTurn == PLAYER_HUMAN) ? PLAYER_COMPUTER : PLAYER_HUMAN;
}

bool battle_is_over(const Game *game)
{
    return board_all_ships_sunk(game->computerBoard) ||
           board_all_ships_sunk(game->humanBoard);
}

void battle_declare_winner(Game *game)
{
    game->winner    = board_all_ships_sunk(game->computerBoard) ? PLAYER_HUMAN : PLAYER_COMPUTER;
    game->hasWinner = true;
}

/* Human message overwrites the line; computer message is appended, e.g.
 *   "You: hit B5 | CPU: miss D7"                                          */
void battle_build_status_message(Game *game, PlayerId shooter, const ShotOutcome *outcome)
{
    char part[48];
    describe_shot(part, sizeof part, shooter == PLAYER_HUMAN ? "You" : "CPU", outcome);

    if (shooter == PLAYER_HUMAN) {
        set_status(game, part);
    } else {
        size_t used = strlen(game->statusMessage);
        snprintf(game->statusMessage + used, sizeof game->statusMessage - used, " | %s", part);
    }
}

/* ============================================================================
 *  POST-GAME DECISION
 * ========================================================================== */

GameState next_state_after_game_over(const Game *game)
{
    if (game->hasWinner &&
        game->winner == PLAYER_HUMAN &&
        leaderboard_qualifies(&game->leaderboard, score_get(&game->score))) {
        return STATE_NAME_ENTRY;
    }
    return STATE_LEADERBOARD;
}

/* ============================================================================
 *  FLEET FORMATION FLOW
 * ========================================================================== */

bool formation_place_one_manually(Game *game, int fleetIndex)
{
    char prompt[64];
    const Ship *ship = &game->humanBoard->fleet[fleetIndex];

    snprintf(prompt, sizeof prompt, "Placing %s (%d cells)",
             ship_name_for_type(ship->type), ship->length);
    ui_print_message(prompt);

    Coord       start = input_read_coord("Start cell (e.g. B5): ");
    Orientation o     = input_read_orientation();

    if (!board_place_ship(game->humanBoard, fleetIndex, start, o)) {
        ui_print_message("Invalid placement (out of bounds or overlapping). Try again.");
        return false;
    }
    return true;
}

void formation_place_all_manually(Game *game)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (game->humanBoard->fleet[i].placed) {
            continue;
        }
        ui_clear_screen();
        ui_print_formation_screen(game->humanBoard);

        while (!formation_place_one_manually(game, i)) {
            /* keep asking until this ship is placed legally */
        }
    }
}

void formation_reset(Game *game)
{
    board_clear(game->humanBoard);
}

bool formation_can_confirm(const Game *game)
{
    return board_fleet_is_complete(game->humanBoard);
}

/* ============================================================================
 *  ui.c  -  All screen output (ANSI terminal)
 * ========================================================================== */

#include "battleship.h"

#include <stdio.h>

#define ANSI_RESET  "\033[0m"
#define ANSI_RED    "\033[31m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_DIM    "\033[2m"

#define BOARD_GAP   "     "

/* ---------------------------------------------------------------------------
 *  Small helpers
 * ------------------------------------------------------------------------- */

static void print_cell(char symbol)
{
    switch (symbol) {
        case 'X': printf(ANSI_RED    "%c " ANSI_RESET, symbol); break;
        case 'o': printf(ANSI_CYAN   "%c " ANSI_RESET, symbol); break;
        case 'S': printf(ANSI_GREEN  "%c " ANSI_RESET, symbol); break;
        default:  printf(ANSI_DIM    "%c " ANSI_RESET, symbol); break;
    }
}

static void print_banner(const char *title)
{
    printf(ANSI_YELLOW "=============================================\n");
    printf("   %s\n", title);
    printf("=============================================" ANSI_RESET "\n\n");
}

static void print_side_titles(const char *left, const char *right)
{
    printf("%-28s%s\n", left, right);
}

/* ============================================================================
 *  GENERAL
 * ========================================================================== */

void ui_clear_screen(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

void ui_print_message(const char *message)
{
    printf("%s\n", message);
}

/* ============================================================================
 *  BOARD RENDERING
 * ========================================================================== */

char ui_symbol_for_cell(CellState state, bool revealShips)
{
    switch (state) {
        case CELL_SHIP: return revealShips ? 'S' : '~';
        case CELL_HIT:  return 'X';
        case CELL_MISS: return 'o';
        default:        return '~';
    }
}

void ui_print_column_header(void)
{
    int col;
    printf("   ");
    for (col = 1; col <= BOARD_SIZE; col++) {
        printf(col < 10 ? "%d " : "%d", col);
    }
}

/* Prints one row WITHOUT a trailing newline so rows can sit side by side. */
void ui_print_board_row(const Board *board, int row, bool revealShips)
{
    int col;
    printf("%c  ", 'A' + row);
    for (col = 0; col < BOARD_SIZE; col++) {
        print_cell(ui_symbol_for_cell(board->cells[row][col], revealShips));
    }
}

void ui_print_board(const Board *board, bool revealShips)
{
    int row;
    ui_print_column_header();
    printf("\n");
    for (row = 0; row < BOARD_SIZE; row++) {
        ui_print_board_row(board, row, revealShips);
        printf("\n");
    }
}

void ui_print_two_boards_side_by_side(const Board *left, bool revealLeft,
                                      const Board *right, bool revealRight)
{
    int row;

    ui_print_column_header();
    printf(BOARD_GAP "  ");
    ui_print_column_header();
    printf("\n");

    for (row = 0; row < BOARD_SIZE; row++) {
        ui_print_board_row(left, row, revealLeft);
        printf(BOARD_GAP);
        ui_print_board_row(right, row, revealRight);
        printf("\n");
    }
}

void ui_print_fleet_status(const Board *board)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        const Ship *ship = &board->fleet[i];
        printf("  %-17s (%d)  ", ship_name_for_type(ship->type), ship->length);
        if (!ship->placed) {
            printf("not placed\n");
        } else if (ship_is_sunk(ship)) {
            printf(ANSI_RED "SUNK" ANSI_RESET "\n");
        } else {
            printf(ANSI_GREEN "afloat" ANSI_RESET "\n");
        }
    }
}

/* ============================================================================
 *  SCREENS
 * ========================================================================== */

void ui_print_main_menu(void)
{
    print_banner("B A T T L E S H I P");
    printf("  1. New Game\n");
    printf("  2. Leaderboard\n");
    printf("  3. Exit\n\n");
}

void ui_print_formation_screen(const Board *humanBoard)
{
    print_banner("FLEET FORMATION");
    ui_print_board(humanBoard, true);
    printf("\n");
    ui_print_fleet_status(humanBoard);
    printf("\n  1. Place ships manually\n");
    printf("  2. Place ships randomly\n");
    printf("  3. Reset board\n");
    printf("  4. Confirm and start battle\n");
    printf("  5. Back to main menu\n\n");
}

void ui_print_battle_screen(const Board *humanBoard, const Board *computerBoard,
                            const ScoreTracker *score, const char *statusMessage)
{
    print_banner("BATTLE");
    print_side_titles("   YOUR FLEET", "   ENEMY WATERS");
    ui_print_two_boards_side_by_side(humanBoard, true, computerBoard, false);

    printf("\n  Enemy fleet:\n");
    ui_print_fleet_status(computerBoard);
    printf("\n  Score: %d   Shots fired: %d\n", score_get(score), score->shotsFired);
    if (statusMessage != NULL && statusMessage[0] != '\0') {
        printf("  Last turn: %s\n", statusMessage);
    }
    printf("\n");
}

void ui_print_reveal_screen(const Board *humanBoard, const Board *computerBoard, PlayerId winner)
{
    print_banner(winner == PLAYER_HUMAN ? "VICTORY! YOU SANK THE ENEMY FLEET"
                                        : "DEFEAT - THE COMPUTER SANK YOUR FLEET");
    print_side_titles("   YOUR FLEET", "   ENEMY FLEET");
    ui_print_two_boards_side_by_side(humanBoard, true, computerBoard, true);
    printf("\n");
}

void ui_print_name_prompt(int score)
{
    print_banner("NEW HIGH SCORE");
    printf("  You made the leaderboard with a score of %d!\n\n", score);
}

void ui_print_leaderboard(const Leaderboard *lb)
{
    int i;

    print_banner("LEADERBOARD - TOP 3");
    for (i = 0; i < LEADERBOARD_SLOTS; i++) {
        if (i < lb->count) {
            printf("  %d. %-20s %3d\n", i + 1, lb->entries[i].name, lb->entries[i].score);
        } else {
            printf("  %d. ---\n", i + 1);
        }
    }
    printf("\n");
}

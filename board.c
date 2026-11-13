/* ============================================================================
 *  board.c  -  Ships, boards, placement, random fleets, shooting
 *
 *  Pure game logic: no printing, no input.
 * ========================================================================== */

#include "battleship.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_RANDOM_PLACEMENT_ATTEMPTS 1000

/* Indexed by ShipType (SHIP_CARRIER .. SHIP_DESTROYER). */
static const struct {
    const char *name;
    int         length;
} SHIP_INFO[FLEET_SIZE] = {
    { "Aircraft Carrier", 5 },
    { "Battleship",       4 },
    { "Submarine",        3 },
    { "Cruiser",          3 },
    { "Destroyer",        2 }
};

static bool fleet_index_is_valid(int index)
{
    return index >= 0 && index < FLEET_SIZE;
}

/* ============================================================================
 *  BOARD LIFECYCLE
 * ========================================================================== */

Board *board_create(void)
{
    Board *board = calloc(1, sizeof *board);
    int row;

    if (board == NULL) {
        return NULL;
    }

    /* calloc so a partial failure can be cleaned up safely by board_destroy */
    board->cells      = calloc(BOARD_SIZE, sizeof *board->cells);
    board->shipIndex  = calloc(BOARD_SIZE, sizeof *board->shipIndex);
    if (board->cells == NULL || board->shipIndex == NULL) {
        board_destroy(board);
        return NULL;
    }

    for (row = 0; row < BOARD_SIZE; row++) {
        board->cells[row]     = malloc(BOARD_SIZE * sizeof **board->cells);
        board->shipIndex[row] = malloc(BOARD_SIZE * sizeof **board->shipIndex);
        if (board->cells[row] == NULL || board->shipIndex[row] == NULL) {
            board_destroy(board);
            return NULL;
        }
    }

    board_clear(board);
    return board;
}

void board_destroy(Board *board)
{
    int row;

    if (board == NULL) {
        return;
    }
    for (row = 0; row < BOARD_SIZE; row++) {
        if (board->cells != NULL)     free(board->cells[row]);
        if (board->shipIndex != NULL) free(board->shipIndex[row]);
    }
    free(board->cells);
    free(board->shipIndex);
    free(board);
}

void board_clear(Board *board)
{
    int row, col;

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            board->cells[row][col]     = CELL_WATER;
            board->shipIndex[row][col] = -1;
        }
    }
    fleet_init(board->fleet);
}

/* ============================================================================
 *  FLEET DATA
 * ========================================================================== */

void fleet_init(Ship fleet[FLEET_SIZE])
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        fleet[i].type        = (ShipType)i;
        fleet[i].length      = SHIP_INFO[i].length;
        fleet[i].hits        = 0;
        fleet[i].placed      = false;
        fleet[i].start.row   = 0;
        fleet[i].start.col   = 0;
        fleet[i].orientation = ORIENT_HORIZONTAL;
    }
}

int ship_length_for_type(ShipType type)
{
    return fleet_index_is_valid((int)type) ? SHIP_INFO[type].length : 0;
}

const char *ship_name_for_type(ShipType type)
{
    return fleet_index_is_valid((int)type) ? SHIP_INFO[type].name : "Unknown";
}

/* ============================================================================
 *  COORDINATES
 * ========================================================================== */

bool coord_is_in_bounds(Coord c)
{
    return c.row >= 0 && c.row < BOARD_SIZE && c.col >= 0 && c.col < BOARD_SIZE;
}

bool coord_equals(Coord a, Coord b)
{
    return a.row == b.row && a.col == b.col;
}

Coord coord_offset(Coord c, int dRow, int dCol)
{
    Coord result;
    result.row = c.row + dRow;
    result.col = c.col + dCol;
    return result;
}

bool coord_parse(const char *text, Coord *out)
{
    Coord c;
    int   digits = 0;

    if (text == NULL || out == NULL) {
        return false;
    }
    while (isspace((unsigned char)*text)) text++;

    if (!isalpha((unsigned char)*text)) {
        return false;
    }
    c.row = toupper((unsigned char)*text) - 'A';
    text++;

    if (!isdigit((unsigned char)*text)) {
        return false;
    }
    c.col = 0;
    while (isdigit((unsigned char)*text)) {
        c.col = c.col * 10 + (*text - '0');
        if (++digits > 3) {
            return false;
        }
        text++;
    }
    c.col -= 1;                                   /* "1".."10" -> 0..9 */

    while (isspace((unsigned char)*text)) text++;
    if (*text != '\0' || !coord_is_in_bounds(c)) {
        return false;
    }

    *out = c;
    return true;
}

void coord_format(Coord c, char buffer[4])
{
    if (!coord_is_in_bounds(c)) {
        buffer[0] = '?'; buffer[1] = '?'; buffer[2] = '\0';
        return;
    }
    buffer[0] = (char)('A' + c.row);
    if (c.col + 1 < 10) {
        buffer[1] = (char)('0' + c.col + 1);
        buffer[2] = '\0';
    } else {
        buffer[1] = '1';
        buffer[2] = '0';
        buffer[3] = '\0';
    }
}

/* ============================================================================
 *  PLACEMENT
 * ========================================================================== */

void ship_compute_cells(Coord start, Orientation o, int length, Coord out[MAX_SHIP_LENGTH])
{
    int i;
    for (i = 0; i < length; i++) {
        out[i] = (o == ORIENT_HORIZONTAL) ? coord_offset(start, 0, i)
                                          : coord_offset(start, i, 0);
    }
}

bool placement_fits_in_bounds(Coord start, Orientation o, int length)
{
    Coord last;
    if (length < 1) {
        return false;
    }
    last = (o == ORIENT_HORIZONTAL) ? coord_offset(start, 0, length - 1)
                                    : coord_offset(start, length - 1, 0);
    return coord_is_in_bounds(start) && coord_is_in_bounds(last);
}

bool placement_is_free(const Board *board, Coord start, Orientation o, int length)
{
    Coord cells[MAX_SHIP_LENGTH];
    int   i;

    if (!placement_fits_in_bounds(start, o, length)) {
        return false;
    }
    ship_compute_cells(start, o, length, cells);
    for (i = 0; i < length; i++) {
        if (board->cells[cells[i].row][cells[i].col] != CELL_WATER) {
            return false;
        }
    }
    return true;
}

bool placement_is_valid(const Board *board, Coord start, Orientation o, int length)
{
    return placement_fits_in_bounds(start, o, length) &&
           placement_is_free(board, start, o, length);
}

bool board_place_ship(Board *board, int fleetIndex, Coord start, Orientation o)
{
    Ship *ship;
    Coord cells[MAX_SHIP_LENGTH];
    int   i;

    if (!fleet_index_is_valid(fleetIndex) || board->fleet[fleetIndex].placed) {
        return false;
    }
    ship = &board->fleet[fleetIndex];
    if (!placement_is_valid(board, start, o, ship->length)) {
        return false;
    }

    ship_compute_cells(start, o, ship->length, cells);
    for (i = 0; i < ship->length; i++) {
        board->cells[cells[i].row][cells[i].col]     = CELL_SHIP;
        board->shipIndex[cells[i].row][cells[i].col] = fleetIndex;
    }
    ship->start       = start;
    ship->orientation = o;
    ship->placed      = true;
    ship->hits        = 0;
    return true;
}

bool board_remove_ship(Board *board, int fleetIndex)
{
    Ship *ship;
    Coord cells[MAX_SHIP_LENGTH];
    int   i;

    if (!fleet_index_is_valid(fleetIndex) || !board->fleet[fleetIndex].placed) {
        return false;
    }
    ship = &board->fleet[fleetIndex];
    ship_collect_cells(ship, cells);
    for (i = 0; i < ship->length; i++) {
        board->cells[cells[i].row][cells[i].col]     = CELL_WATER;
        board->shipIndex[cells[i].row][cells[i].col] = -1;
    }
    ship->placed = false;
    ship->hits   = 0;
    return true;
}

bool board_fleet_is_complete(const Board *board)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (!board->fleet[i].placed) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 *  RANDOM FLEET GENERATION
 * ========================================================================== */

Orientation random_orientation(void)
{
    return random_int(0, 1) == 0 ? ORIENT_HORIZONTAL : ORIENT_VERTICAL;
}

Coord random_coord(void)
{
    Coord c;
    c.row = random_int(0, BOARD_SIZE - 1);
    c.col = random_int(0, BOARD_SIZE - 1);
    return c;
}

bool board_place_ship_randomly(Board *board, int fleetIndex)
{
    int attempt;
    for (attempt = 0; attempt < MAX_RANDOM_PLACEMENT_ATTEMPTS; attempt++) {
        if (board_place_ship(board, fleetIndex, random_coord(), random_orientation())) {
            return true;
        }
    }
    return false;
}

void board_place_fleet_randomly(Board *board)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (!board->fleet[i].placed) {
            board_place_ship_randomly(board, i);
        }
    }
}

/* ============================================================================
 *  SHOOTING
 * ========================================================================== */

bool board_is_already_shot(const Board *board, Coord target)
{
    CellState s;
    if (!coord_is_in_bounds(target)) {
        return false;
    }
    s = board->cells[target.row][target.col];
    return s == CELL_MISS || s == CELL_HIT;
}

ShotResult board_classify_shot(const Board *board, Coord target)
{
    int idx;

    if (!coord_is_in_bounds(target) || board_is_already_shot(board, target)) {
        return SHOT_INVALID;
    }
    if (board->cells[target.row][target.col] != CELL_SHIP) {
        return SHOT_MISS;
    }
    idx = board->shipIndex[target.row][target.col];
    return (board->fleet[idx].hits + 1 >= board->fleet[idx].length) ? SHOT_SUNK : SHOT_HIT;
}

void board_mark_shot(Board *board, Coord target, ShotResult r)
{
    if (r == SHOT_MISS) {
        board->cells[target.row][target.col] = CELL_MISS;
    } else if (r == SHOT_HIT || r == SHOT_SUNK) {
        board->cells[target.row][target.col] = CELL_HIT;
        board->fleet[board->shipIndex[target.row][target.col]].hits++;
    }
}

bool ship_is_sunk(const Ship *ship)
{
    return ship->placed && ship->hits >= ship->length;
}

void ship_collect_cells(const Ship *ship, Coord out[MAX_SHIP_LENGTH])
{
    ship_compute_cells(ship->start, ship->orientation, ship->length, out);
}

ShotOutcome board_receive_shot(Board *board, Coord target)
{
    ShotOutcome outcome;
    int         shipIdx = -1;

    outcome.result     = board_classify_shot(board, target);
    outcome.target     = target;
    outcome.sunkType   = SHIP_NONE;
    outcome.sunkLength = 0;

    if (outcome.result == SHOT_INVALID) {
        return outcome;
    }
    if (outcome.result != SHOT_MISS) {
        shipIdx = board->shipIndex[target.row][target.col];
    }

    board_mark_shot(board, target, outcome.result);

    if (outcome.result == SHOT_SUNK) {
        outcome.sunkType   = board->fleet[shipIdx].type;
        outcome.sunkLength = board->fleet[shipIdx].length;
        ship_collect_cells(&board->fleet[shipIdx], outcome.sunkCells);
    }
    return outcome;
}

bool board_all_ships_sunk(const Board *board)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (!ship_is_sunk(&board->fleet[i])) {
            return false;
        }
    }
    return true;
}

int board_count_ships_remaining(const Board *board)
{
    int i, count = 0;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (board->fleet[i].placed && !ship_is_sunk(&board->fleet[i])) {
            count++;
        }
    }
    return count;
}

/* ============================================================================
 *  ai.c  -  Computer player: hunt / target strategy
 *
 *  HUNT   : no unresolved hits -> shoot where the most ship placements fit
 *           (probability density over every ship still afloat).
 *  TARGET : unresolved hits exist -> shoot only next to them. Once two hits
 *           line up the orientation is locked. Candidate cells are ranked by
 *           how many legal ship placements pass through them, with placements
 *           that explain MORE hits weighted much higher.
 *
 *  The AI only ever sees ShotOutcome values (hit / miss / sunk). It never
 *  reads the human's board.
 * ========================================================================== */

#include "battleship.h"

#include <stdlib.h>

#define NO_COORD_ROW (-1)

static Coord no_coord(void)
{
    Coord c;
    c.row = NO_COORD_ROW;
    c.col = NO_COORD_ROW;
    return c;
}

static bool is_no_coord(Coord c)
{
    return !coord_is_in_bounds(c);
}

/* ============================================================================
 *  LIFECYCLE
 * ========================================================================== */

AiPlayer *ai_create(void)
{
    AiPlayer *ai = calloc(1, sizeof *ai);
    int row;

    if (ai == NULL) {
        return NULL;
    }
    ai->view = calloc(BOARD_SIZE, sizeof *ai->view);
    if (ai->view == NULL) {
        ai_destroy(ai);
        return NULL;
    }
    for (row = 0; row < BOARD_SIZE; row++) {
        ai->view[row] = malloc(BOARD_SIZE * sizeof **ai->view);
        if (ai->view[row] == NULL) {
            ai_destroy(ai);
            return NULL;
        }
    }
    ai_reset(ai);
    return ai;
}

void ai_destroy(AiPlayer *ai)
{
    int row;
    if (ai == NULL) {
        return;
    }
    if (ai->view != NULL) {
        for (row = 0; row < BOARD_SIZE; row++) {
            free(ai->view[row]);
        }
        free(ai->view);
    }
    free(ai);
}

void ai_reset(AiPlayer *ai)
{
    int row, col, i;

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            ai->view[row][col] = KNOWN_UNKNOWN;
        }
    }
    for (i = 0; i < FLEET_SIZE; i++) {
        ai->shipAlive[i] = true;
    }
    ai->mode              = AI_MODE_HUNT;
    ai->activeHitCount    = 0;
    ai->lockedOrientation = ORIENT_UNKNOWN;
    ai->candidateCount    = 0;
    ai_clear_probability_map(ai);
}

/* ============================================================================
 *  TOP-LEVEL DECISION
 * ========================================================================== */

Coord ai_choose_next_shot(AiPlayer *ai)
{
    Coord shot = no_coord();

    ai_update_mode(ai);
    if (ai->mode == AI_MODE_TARGET) {
        shot = ai_choose_target_shot(ai);
    }
    if (is_no_coord(shot)) {                    /* HUNT mode, or TARGET found nothing */
        shot = ai_choose_hunt_shot(ai);
    }
    return shot;
}

void ai_register_outcome(AiPlayer *ai, const ShotOutcome *outcome)
{
    switch (outcome->result) {
        case SHOT_MISS: ai_record_miss(ai, outcome->target);      break;
        case SHOT_HIT:  ai_record_hit(ai, outcome->target);       break;
        case SHOT_SUNK: ai_record_sunk(ai, outcome);              break;
        default:                                                  break;
    }
    ai_update_mode(ai);
    ai_update_orientation(ai);
}

/* ============================================================================
 *  MODE SELECTION
 * ========================================================================== */

AiMode ai_decide_mode(const AiPlayer *ai)
{
    return ai->activeHitCount > 0 ? AI_MODE_TARGET : AI_MODE_HUNT;
}

void ai_update_mode(AiPlayer *ai)
{
    ai->mode = ai_decide_mode(ai);
}

/* ============================================================================
 *  HUNT MODE: PROBABILITY DENSITY
 * ========================================================================== */

Coord ai_choose_hunt_shot(AiPlayer *ai)
{
    Coord best;

    ai_clear_probability_map(ai);
    ai_build_probability_map(ai);
    best = ai_pick_best_from_map(ai);

    return is_no_coord(best) ? ai_pick_random_unknown(ai) : best;
}

void ai_clear_probability_map(AiPlayer *ai)
{
    int row, col;
    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            ai->probability[row][col] = 0;
        }
    }
}

void ai_build_probability_map(AiPlayer *ai)
{
    int   type, row, col, o;
    Coord start;

    for (type = 0; type < FLEET_SIZE; type++) {
        int length;
        if (!ai->shipAlive[type]) {
            continue;
        }
        length = ship_length_for_type((ShipType)type);

        for (row = 0; row < BOARD_SIZE; row++) {
            for (col = 0; col < BOARD_SIZE; col++) {
                start.row = row;
                start.col = col;
                for (o = ORIENT_HORIZONTAL; o <= ORIENT_VERTICAL; o++) {
                    if (ai_placement_is_possible(ai, start, (Orientation)o, length)) {
                        ai_add_placement_to_map(ai, start, (Orientation)o, length);
                    }
                }
            }
        }
    }
}

/* A ship can lie here if it fits and crosses no miss and no already-sunk ship. */
bool ai_placement_is_possible(const AiPlayer *ai, Coord start, Orientation o, int length)
{
    Coord cells[MAX_SHIP_LENGTH];
    int   i;

    if (!placement_fits_in_bounds(start, o, length)) {
        return false;
    }
    ship_compute_cells(start, o, length, cells);
    for (i = 0; i < length; i++) {
        KnowledgeState k = ai->view[cells[i].row][cells[i].col];
        if (k == KNOWN_MISS || k == KNOWN_SUNK) {
            return false;
        }
    }
    return true;
}

/* HUNT: every placement counts 1.
 * TARGET: only placements that cover at least one unresolved hit count, and
 * a placement explaining k hits weighs 10^(k-1), so lines of hits dominate. */
void ai_add_placement_to_map(AiPlayer *ai, Coord start, Orientation o, int length)
{
    Coord cells[MAX_SHIP_LENGTH];
    int   i, hitsCovered = 0, weight = 1;

    ship_compute_cells(start, o, length, cells);
    for (i = 0; i < length; i++) {
        if (ai->view[cells[i].row][cells[i].col] == KNOWN_HIT) {
            hitsCovered++;
        }
    }
    if (ai->activeHitCount > 0) {
        if (hitsCovered == 0) {
            return;
        }
        for (i = 1; i < hitsCovered; i++) {
            weight *= 10;
        }
    }
    for (i = 0; i < length; i++) {
        if (ai->view[cells[i].row][cells[i].col] == KNOWN_UNKNOWN) {
            ai->probability[cells[i].row][cells[i].col] += weight;
        }
    }
}

int ai_smallest_alive_ship_length(const AiPlayer *ai)
{
    int type, smallest = 0;
    for (type = 0; type < FLEET_SIZE; type++) {
        int length = ship_length_for_type((ShipType)type);
        if (ai->shipAlive[type] && (smallest == 0 || length < smallest)) {
            smallest = length;
        }
    }
    return smallest;
}

/* Highest-valued unknown cell; ties broken uniformly at random. */
Coord ai_pick_best_from_map(const AiPlayer *ai)
{
    Coord ties[TOTAL_CELLS];
    int   tieCount = 0, best = 0, row, col;

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            int value = ai->probability[row][col];
            if (ai->view[row][col] != KNOWN_UNKNOWN || value <= 0) {
                continue;
            }
            if (value > best) {
                best     = value;
                tieCount = 0;
            }
            if (value == best) {
                ties[tieCount].row = row;
                ties[tieCount].col = col;
                tieCount++;
            }
        }
    }
    return tieCount == 0 ? no_coord() : ties[random_int(0, tieCount - 1)];
}

Coord ai_pick_random_unknown(const AiPlayer *ai)
{
    Coord unknown[TOTAL_CELLS];
    int   count = 0, row, col;

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            if (ai->view[row][col] == KNOWN_UNKNOWN) {
                unknown[count].row = row;
                unknown[count].col = col;
                count++;
            }
        }
    }
    return count == 0 ? no_coord() : unknown[random_int(0, count - 1)];
}

/* ============================================================================
 *  TARGET MODE: FOLLOW UP ON HITS
 * ========================================================================== */

Coord ai_choose_target_shot(AiPlayer *ai)
{
    Coord shot;

    ai_rebuild_candidates(ai);
    if (ai->candidateCount == 0) {
        return no_coord();
    }
    shot = ai_pick_candidate(ai);
    ai_remove_candidate(ai, shot);
    return shot;
}

void ai_rebuild_candidates(AiPlayer *ai)
{
    int i;

    ai->candidateCount = 0;
    ai_update_orientation(ai);

    if (ai->lockedOrientation != ORIENT_UNKNOWN) {
        ai_add_line_end_candidates(ai);
    }
    /* No orientation yet, or the locked line is exhausted (e.g. two different
     * ships happened to share a row): fall back to all four neighbours. */
    if (ai->candidateCount == 0) {
        for (i = 0; i < ai->activeHitCount; i++) {
            ai_add_neighbour_candidates(ai, ai->activeHits[i]);
        }
    }
}

void ai_add_neighbour_candidates(AiPlayer *ai, Coord hit)
{
    ai_add_candidate_if_valid(ai, coord_offset(hit, -1, 0));
    ai_add_candidate_if_valid(ai, coord_offset(hit,  1, 0));
    ai_add_candidate_if_valid(ai, coord_offset(hit,  0, -1));
    ai_add_candidate_if_valid(ai, coord_offset(hit,  0,  1));
}

/* Along the locked axis only: the cells just beyond each run of hits. */
void ai_add_line_end_candidates(AiPlayer *ai)
{
    int i;
    for (i = 0; i < ai->activeHitCount; i++) {
        Coord hit = ai->activeHits[i];
        if (ai->lockedOrientation == ORIENT_HORIZONTAL) {
            ai_add_candidate_if_valid(ai, coord_offset(hit, 0, -1));
            ai_add_candidate_if_valid(ai, coord_offset(hit, 0,  1));
        } else {
            ai_add_candidate_if_valid(ai, coord_offset(hit, -1, 0));
            ai_add_candidate_if_valid(ai, coord_offset(hit,  1, 0));
        }
    }
}

void ai_add_candidate_if_valid(AiPlayer *ai, Coord c)
{
    if (ai_is_unknown(ai, c) && !ai_candidate_exists(ai, c) &&
        ai->candidateCount < MAX_AI_CANDIDATES) {
        ai->candidates[ai->candidateCount++] = c;
    }
}

bool ai_candidate_exists(const AiPlayer *ai, Coord c)
{
    int i;
    for (i = 0; i < ai->candidateCount; i++) {
        if (coord_equals(ai->candidates[i], c)) {
            return true;
        }
    }
    return false;
}

/* Best candidate by placement weight; random among ties (or among all
 * candidates if no placement explains the hits, which should not happen). */
Coord ai_pick_candidate(AiPlayer *ai)
{
    Coord ties[MAX_AI_CANDIDATES];
    int   tieCount = 0, best = -1, i;

    ai_clear_probability_map(ai);
    ai_build_probability_map(ai);

    for (i = 0; i < ai->candidateCount; i++) {
        Coord c     = ai->candidates[i];
        int   value = ai->probability[c.row][c.col];
        if (value > best) {
            best     = value;
            tieCount = 0;
        }
        if (value == best) {
            ties[tieCount++] = c;
        }
    }
    return ties[random_int(0, tieCount - 1)];
}

void ai_remove_candidate(AiPlayer *ai, Coord c)
{
    int i, j;
    for (i = 0; i < ai->candidateCount; i++) {
        if (coord_equals(ai->candidates[i], c)) {
            for (j = i; j < ai->candidateCount - 1; j++) {
                ai->candidates[j] = ai->candidates[j + 1];
            }
            ai->candidateCount--;
            return;
        }
    }
}

/* ============================================================================
 *  ORIENTATION REASONING
 * ========================================================================== */

void ai_update_orientation(AiPlayer *ai)
{
    ai->lockedOrientation = ai_detect_orientation(ai->activeHits, ai->activeHitCount);
}

/* Two or more hits all in one row -> horizontal; all in one column -> vertical. */
Orientation ai_detect_orientation(const Coord *hits, int count)
{
    bool sameRow = true, sameCol = true;
    int  i;

    if (count < 2) {
        return ORIENT_UNKNOWN;
    }
    for (i = 1; i < count; i++) {
        if (hits[i].row != hits[0].row) sameRow = false;
        if (hits[i].col != hits[0].col) sameCol = false;
    }
    if (sameRow) return ORIENT_HORIZONTAL;
    if (sameCol) return ORIENT_VERTICAL;
    return ORIENT_UNKNOWN;
}

void ai_get_hit_run_bounds(const AiPlayer *ai, Coord *first, Coord *last)
{
    int i;

    *first = no_coord();
    *last  = no_coord();
    if (ai->lockedOrientation == ORIENT_UNKNOWN || ai->activeHitCount == 0) {
        return;
    }
    *first = ai->activeHits[0];
    *last  = ai->activeHits[0];
    for (i = 1; i < ai->activeHitCount; i++) {
        Coord h = ai->activeHits[i];
        if (ai->lockedOrientation == ORIENT_HORIZONTAL) {
            if (h.col < first->col) *first = h;
            if (h.col > last->col)  *last  = h;
        } else {
            if (h.row < first->row) *first = h;
            if (h.row > last->row)  *last  = h;
        }
    }
}

/* ============================================================================
 *  LEARNING FROM RESULTS
 * ========================================================================== */

void ai_record_miss(AiPlayer *ai, Coord c)
{
    ai->view[c.row][c.col] = KNOWN_MISS;
}

void ai_record_hit(AiPlayer *ai, Coord c)
{
    int i;

    ai->view[c.row][c.col] = KNOWN_HIT;
    for (i = 0; i < ai->activeHitCount; i++) {
        if (coord_equals(ai->activeHits[i], c)) {
            return;
        }
    }
    ai->activeHits[ai->activeHitCount++] = c;
}

/* Only the sunk ship's cells leave the unresolved list. Hits that belong to
 * some other ship stay, so the AI keeps targeting them. */
void ai_record_sunk(AiPlayer *ai, const ShotOutcome *outcome)
{
    int i;

    for (i = 0; i < outcome->sunkLength; i++) {
        Coord c = outcome->sunkCells[i];
        ai->view[c.row][c.col] = KNOWN_SUNK;
        ai_remove_active_hit(ai, c);
    }
    if (outcome->sunkType != SHIP_NONE) {
        ai->shipAlive[outcome->sunkType] = false;
    }
}

void ai_remove_active_hit(AiPlayer *ai, Coord c)
{
    int i, j;
    for (i = 0; i < ai->activeHitCount; i++) {
        if (coord_equals(ai->activeHits[i], c)) {
            for (j = i; j < ai->activeHitCount - 1; j++) {
                ai->activeHits[j] = ai->activeHits[j + 1];
            }
            ai->activeHitCount--;
            return;
        }
    }
}

bool ai_is_unknown(const AiPlayer *ai, Coord c)
{
    return coord_is_in_bounds(c) && ai->view[c.row][c.col] == KNOWN_UNKNOWN;
}

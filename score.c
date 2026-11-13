/* ============================================================================
 *  score.c  -  Score tracking and leaderboard (file handling)
 *
 *  File format, one entry per line:   <score> <name>
 * ========================================================================== */

#include "battleship.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 *  SCORE
 * ========================================================================== */

void score_init(ScoreTracker *s)
{
    s->score      = STARTING_SCORE;
    s->shotsFired = 0;
}

void score_register_shot(ScoreTracker *s)
{
    s->shotsFired++;
    if (s->score > 0) {
        s->score--;
    }
}

int score_get(const ScoreTracker *s)
{
    return s->score;
}

/* ============================================================================
 *  LEADERBOARD
 * ========================================================================== */

void leaderboard_init(Leaderboard *lb)
{
    memset(lb, 0, sizeof *lb);
}

/* Stable insertion sort, descending by score (earlier entries win ties). */
void leaderboard_sort(Leaderboard *lb)
{
    int i, j;
    for (i = 1; i < lb->count; i++) {
        LeaderboardEntry key = lb->entries[i];
        for (j = i - 1; j >= 0 && lb->entries[j].score < key.score; j--) {
            lb->entries[j + 1] = lb->entries[j];
        }
        lb->entries[j + 1] = key;
    }
}

bool leaderboard_load(Leaderboard *lb, const char *path)
{
    FILE *file;
    char  line[128];

    leaderboard_init(lb);
    file = fopen(path, "r");
    if (file == NULL) {
        return true;                                  /* no file yet = empty board */
    }

    while (lb->count < LEADERBOARD_SLOTS && fgets(line, sizeof line, file) != NULL) {
        LeaderboardEntry *entry = &lb->entries[lb->count];
        if (sscanf(line, "%d %20[^\n]", &entry->score, entry->name) == 2) {
            lb->count++;                              /* malformed lines are skipped */
        }
    }
    fclose(file);
    leaderboard_sort(lb);
    return true;
}

bool leaderboard_save(const Leaderboard *lb, const char *path)
{
    FILE *file = fopen(path, "w");
    int   i;

    if (file == NULL) {
        return false;
    }
    for (i = 0; i < lb->count; i++) {
        fprintf(file, "%d %s\n", lb->entries[i].score, lb->entries[i].name);
    }
    return fclose(file) == 0;
}

bool leaderboard_qualifies(const Leaderboard *lb, int score)
{
    return lb->count < LEADERBOARD_SLOTS ||
           score > lb->entries[lb->count - 1].score;
}

void leaderboard_insert(Leaderboard *lb, const char *name, int score)
{
    LeaderboardEntry *slot;

    if (!leaderboard_qualifies(lb, score)) {
        return;
    }
    slot = (lb->count < LEADERBOARD_SLOTS) ? &lb->entries[lb->count++]
                                           : &lb->entries[LEADERBOARD_SLOTS - 1];
    snprintf(slot->name, sizeof slot->name, "%s", name);
    slot->score = score;
    leaderboard_sort(lb);
}

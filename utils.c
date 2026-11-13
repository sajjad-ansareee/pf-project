/* ============================================================================
 *  utils.c  -  Random number helpers
 * ========================================================================== */

#include "battleship.h"

#include <stdlib.h>
#include <time.h>

void seed_random(void)
{
    srand((unsigned int)time(NULL));
}

int random_int(int minInclusive, int maxInclusive)
{
    return minInclusive + rand() % (maxInclusive - minInclusive + 1);
}

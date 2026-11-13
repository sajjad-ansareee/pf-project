/* ============================================================================
 *  main.c  -  Entry point
 * ========================================================================== */

#include "battleship.h"

int main(void)
{
    Game game;

    seed_random();

    if (!game_init(&game)) {
        ui_print_message("Failed to initialise the game (out of memory).");
        return 1;
    }

    game_run(&game);
    game_shutdown(&game);
    return 0;
}

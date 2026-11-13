/* ============================================================================
 *  input.c  -  All reading from stdin (plus the prompts that go with it)
 * ========================================================================== */

#include "battleship.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool input_read_line(char *buffer, size_t size)
{
    size_t len;
    int    ch;

    if (fgets(buffer, (int)size, stdin) == NULL) {
        exit(0);                                      /* stdin closed: nothing more can be asked */
    }
    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[--len] = '\0';
    } else {
        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* discard the rest of an over-long line */
        }
    }
    if (len > 0 && buffer[len - 1] == '\r') {
        buffer[len - 1] = '\0';
    }
    return true;
}

int input_read_menu_choice(int minChoice, int maxChoice)
{
    char line[32];
    char message[64];

    for (;;) {
        char *end;
        long  value;

        printf("Choose an option [%d-%d]: ", minChoice, maxChoice);
        fflush(stdout);
        input_read_line(line, sizeof line);

        value = strtol(line, &end, 10);
        while (isspace((unsigned char)*end)) end++;
        if (end != line && *end == '\0' && value >= minChoice && value <= maxChoice) {
            return (int)value;
        }
        snprintf(message, sizeof message, "Please enter a number from %d to %d.", minChoice, maxChoice);
        ui_print_message(message);
    }
}

Coord input_read_coord(const char *prompt)
{
    char  line[32];
    Coord c;

    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        input_read_line(line, sizeof line);
        if (coord_parse(line, &c)) {
            return c;
        }
        ui_print_message("Invalid coordinate. Use a letter A-J then a number 1-10, e.g. B5.");
    }
}

Orientation input_read_orientation(void)
{
    char line[32];
    char *p;

    for (;;) {
        printf("Orientation (H = horizontal, V = vertical): ");
        fflush(stdout);
        input_read_line(line, sizeof line);

        p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == 'h' || *p == 'H') return ORIENT_HORIZONTAL;
        if (*p == 'v' || *p == 'V') return ORIENT_VERTICAL;
        ui_print_message("Please type H or V.");
    }
}

void input_read_player_name(char name[MAX_NAME_LENGTH + 1])
{
    char   line[128];
    char  *start;
    size_t len;

    for (;;) {
        printf("Enter your name: ");
        fflush(stdout);
        input_read_line(line, sizeof line);

        start = line;
        while (isspace((unsigned char)*start)) start++;
        len = strlen(start);
        while (len > 0 && isspace((unsigned char)start[len - 1])) len--;

        if (len > 0) {
            if (len > MAX_NAME_LENGTH) len = MAX_NAME_LENGTH;
            memcpy(name, start, len);
            name[len] = '\0';
            return;
        }
        ui_print_message("Name cannot be empty.");
    }
}

void input_wait_for_enter(void)
{
    char line[8];
    printf("Press Enter to continue...");
    fflush(stdout);
    input_read_line(line, sizeof line);
}

FormationChoice formation_prompt_choice(void)
{
    /* Menu order matches ui_print_formation_screen and the enum order. */
    return (FormationChoice)(input_read_menu_choice(1, 5) - 1);
}

/* ============================================================================
 *  gui.c  -  Battleship, raylib front end
 *
 *  Replaces main.c + ui.c + input.c at RUN time (the console handlers are
 *  simply never called). All game rules still come from the existing code:
 *
 *      board.c       placement, shooting, sunk detection
 *      ai.c          the computer's hunt / target brain
 *      score.c       score and top-3 leaderboard file
 *      battleship.c  game_init / game_start_new / battle_* / next_state_*
 *
 *  Each screen function does its input AND drawing in one pass (immediate
 *  mode), and moves between screens with game_set_state(), exactly like the
 *  console state machine.
 *
 *  Build:  see Makefile  (make gui)
 * ========================================================================== */

#include "battleship.h"
#include "raylib.h"
#include "sound.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 *  LAYOUT AND STYLE
 * ========================================================================== */

#define WIN_W       1100
#define WIN_H       720
#define CELL        40
#define BOARD_PX    (CELL * BOARD_SIZE)
#define LEFT_X      50              /* your board in battle / formation   */
#define RIGHT_X     650             /* enemy board in battle              */
#define BOARD_Y     170

#define MAX_FX      32
#define BANNER_TIME 2.4f
#define ENEMY_DELAY 1.0f        /* pause before the computer fires          */
#define PLAYER_LOCK 1.0f        /* player must wait after the computer fires */
#define PLAYER_LOCK_SUNK 1.5f   /* a little longer when a ship was sunk     */
#define END_DELAY   2.4f

/* One colour per ship type, so ships are told apart by size AND colour. */
static const Color SHIP_COLOR[FLEET_SIZE] = {
    { 126,  87, 194, 255 },         /* Aircraft Carrier (5) purple */
    { 239, 125,  40, 255 },         /* Battleship       (4) orange */
    {  20, 160, 150, 255 },         /* Submarine        (3) teal   */
    { 240, 200,  50, 255 },         /* Cruiser          (3) yellow */
    { 170, 185, 195, 255 }          /* Destroyer        (2) silver */
};

enum { FX_MISS, FX_HIT, FX_SUNK };

typedef struct {
    Vector2 pos;
    float   age;
    float   life;
    int     kind;
    bool    active;
} Effect;

typedef struct {
    int         sel;                    /* ship being placed, -1 = none      */
    Orientation orient;                 /* orientation for placement         */

    bool        enemyTurn;              /* battle phase                      */
    float       enemyTimer;             /* delay before the computer fires   */
    float       endTimer;               /* delay before the game-over screen */
    float       lockTimer;              /* no firing while the enemy shot is shown */

    bool        lastValid;              /* last shot summary (centre panel)  */
    PlayerId    lastWho;
    char        lastCoord[4];
    ShotResult  lastResult;

    bool        markActive;             /* highlight on the enemy's last target */
    Coord       markCell;
    ShotResult  markResult;

    char        bannerTitle[48];
    char        bannerSub[96];
    Color       bannerColor;
    float       bannerTimer;

    char        logHuman[96];
    char        logEnemy[96];

    Effect      fx[MAX_FX];

    char        name[MAX_NAME_LENGTH + 1];
    int         nameLen;

    bool        quit;
    bool        hand;                   /* show the pointing-hand cursor     */
} Gui;

static Game game;
static Gui  gui;

/* ============================================================================
 *  SMALL DRAWING HELPERS
 * ========================================================================== */

static Color shade(Color c, float f)
{
    Color out = { (unsigned char)(c.r * f), (unsigned char)(c.g * f),
                  (unsigned char)(c.b * f), c.a };
    return out;
}

static void text_centered(const char *text, int cx, int y, int size, Color color)
{
    DrawText(text, cx - MeasureText(text, size) / 2, y, size, color);
}

static void text_shadowed(const char *text, int cx, int y, int size, Color color)
{
    text_centered(text, cx + 2, y + 2, size, Fade(BLACK, 0.55f));
    text_centered(text, cx, y, size, color);
}

static void draw_background(void)
{
    float t = (float)GetTime();
    int   i, x;

    DrawRectangleGradientV(0, 0, WIN_W, WIN_H, (Color){ 8, 28, 58, 255 }, (Color){ 14, 70, 120, 255 });
    for (i = 0; i < 7; i++) {
        float y0 = 60.0f + i * 100.0f;
        for (x = 0; x < WIN_W; x += 12) {
            float ya = y0 + 6.0f * sinf(x * 0.012f + t * 0.8f + i);
            float yb = y0 + 6.0f * sinf((x + 12) * 0.012f + t * 0.8f + i);
            DrawLineEx((Vector2){ (float)x, ya }, (Vector2){ (float)x + 12, yb }, 2, Fade(WHITE, 0.05f));
        }
    }
}

static bool button(Rectangle r, const char *label, int size, bool enabled)
{
    Vector2 m     = GetMousePosition();
    bool    hover = CheckCollisionPointRec(m, r);
    Color   bg    = !enabled ? (Color){ 70, 80, 95, 255 }
                  : hover    ? (Color){ 80, 150, 230, 255 }
                             : (Color){ 40, 100, 180, 255 };

    DrawRectangleRounded((Rectangle){ r.x, r.y + 4, r.width, r.height }, 0.3f, 8, Fade(BLACK, 0.4f));
    DrawRectangleRounded(r, 0.3f, 8, bg);
    text_centered(label, (int)(r.x + r.width / 2), (int)(r.y + r.height / 2 - size / 2), size,
                  enabled ? WHITE : (Color){ 150, 160, 170, 255 });

    if (enabled && hover) {
        gui.hand = true;
    }
    if (enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        sound_play(SND_CLICK);
        return true;
    }
    return false;
}

/* ============================================================================
 *  SHIPS
 *
 *  A ship is a rounded hull exactly as long as its size, in its own colour,
 *  with tick marks between cells so the size can be counted, plus deck
 *  details that differ per type.
 * ========================================================================== */

static void draw_ship_px(ShipType type, float x, float y, int length, bool vertical,
                         float cell, bool sunk, float alpha)
{
    float w     = vertical ? cell : cell * length;
    float h     = vertical ? cell * length : cell;
    float m     = cell * 0.08f;
    Color base  = sunk ? (Color){ 105, 110, 118, 255 } : SHIP_COLOR[type];
    Color dark  = shade(base, 0.55f);
    Rectangle hull = { x + m, y + m, w - 2 * m, h - 2 * m };
    int   i;

#define SEG_X(i) (vertical ? x + w / 2 : x + cell * ((i) + 0.5f))
#define SEG_Y(i) (vertical ? y + cell * ((i) + 0.5f) : y + h / 2)

    DrawRectangleRounded((Rectangle){ hull.x - 2, hull.y - 2, hull.width + 4, hull.height + 4 },
                         0.45f, 8, Fade(BLACK, 0.55f * alpha));
    DrawRectangleRounded(hull, 0.45f, 8, Fade(dark, alpha));
    DrawRectangleRounded((Rectangle){ hull.x + 3, hull.y + 3, hull.width - 6, hull.height - 6 },
                         0.45f, 8, Fade(base, alpha));

    for (i = 1; i < length; i++) {              /* one tick per cell boundary */
        if (vertical) {
            DrawLineEx((Vector2){ x + m + 5, y + i * cell }, (Vector2){ x + w - m - 5, y + i * cell },
                       1.5f, Fade(dark, 0.55f * alpha));
        } else {
            DrawLineEx((Vector2){ x + i * cell, y + m + 5 }, (Vector2){ x + i * cell, y + h - m - 5 },
                       1.5f, Fade(dark, 0.55f * alpha));
        }
    }

    switch (type) {
        case SHIP_CARRIER:                      /* runway dashes */
            for (i = 0; i < length; i++) {
                float dx = vertical ? 0 : cell * 0.22f, dy = vertical ? cell * 0.22f : 0;
                DrawLineEx((Vector2){ SEG_X(i) - dx, SEG_Y(i) - dy }, (Vector2){ SEG_X(i) + dx, SEG_Y(i) + dy },
                           cell * 0.09f, Fade(WHITE, 0.75f * alpha));
            }
            break;
        case SHIP_BATTLESHIP:                   /* two big turrets */
            for (i = 1; i <= 2; i++) {
                DrawCircleV((Vector2){ SEG_X(i), SEG_Y(i) }, cell * 0.22f, Fade(dark, alpha));
                DrawCircleV((Vector2){ SEG_X(i), SEG_Y(i) }, cell * 0.10f, Fade(shade(base, 1.2f), alpha));
            }
            break;
        case SHIP_SUBMARINE:                    /* conning tower */
            DrawCircleV((Vector2){ SEG_X(1), SEG_Y(1) }, cell * 0.24f, Fade(dark, alpha));
            DrawCircleV((Vector2){ SEG_X(1), SEG_Y(1) }, cell * 0.12f, Fade(WHITE, 0.6f * alpha));
            break;
        case SHIP_CRUISER:                      /* two gun mounts */
            for (i = 0; i < length; i += 2) {
                float s = cell * 0.32f;
                DrawRectangleRec((Rectangle){ SEG_X(i) - s / 2, SEG_Y(i) - s / 2, s, s }, Fade(dark, alpha));
            }
            break;
        case SHIP_DESTROYER:                    /* small bridge + mast */
            DrawCircleV((Vector2){ SEG_X(0), SEG_Y(0) }, cell * 0.15f, Fade(dark, alpha));
            DrawRectangleRec((Rectangle){ SEG_X(1) - cell * 0.16f, SEG_Y(1) - cell * 0.16f,
                                          cell * 0.32f, cell * 0.32f }, Fade(dark, alpha));
            break;
        default:
            break;
    }
#undef SEG_X
#undef SEG_Y
}

static void draw_ship_on_board(const Ship *ship, int ox, int oy, bool sunk, float alpha)
{
    draw_ship_px(ship->type, (float)(ox + ship->start.col * CELL), (float)(oy + ship->start.row * CELL),
                 ship->length, ship->orientation == ORIENT_VERTICAL, (float)CELL, sunk, alpha);
}

/* Row of small ship icons showing which ships are still afloat. */
static void draw_fleet_strip(const Board *board, int x, int y)
{
    int i;
    for (i = 0; i < FLEET_SIZE; i++) {
        const Ship *ship = &board->fleet[i];
        float       w    = 14.0f * ship->length;
        bool        sunk = ship_is_sunk(ship);

        draw_ship_px(ship->type, (float)x, (float)y, ship->length, false, 14.0f, sunk, 1.0f);
        if (sunk) {
            DrawLineEx((Vector2){ (float)x, (float)y }, (Vector2){ (float)x + w, (float)y + 14 }, 2.5f, RED);
            DrawLineEx((Vector2){ (float)x, (float)y + 14 }, (Vector2){ (float)x + w, (float)y }, 2.5f, RED);
        }
        x += (int)w + 16;
    }
}

/* ============================================================================
 *  BOARDS
 * ========================================================================== */

static Vector2 cell_center(int ox, int oy, Coord c)
{
    Vector2 v = { (float)(ox + c.col * CELL + CELL / 2), (float)(oy + c.row * CELL + CELL / 2) };
    return v;
}

static bool mouse_cell(int ox, int oy, Coord *out)
{
    Vector2 m = GetMousePosition();
    Coord   c;

    if (m.x < ox || m.y < oy) {
        return false;
    }
    c.col = (int)((m.x - ox) / CELL);
    c.row = (int)((m.y - oy) / CELL);
    if (!coord_is_in_bounds(c)) {
        return false;
    }
    *out = c;
    return true;
}

static void draw_marker_hit(Vector2 p)
{
    DrawCircleV(p, 14, (Color){ 120, 10, 10, 255 });
    DrawCircleV(p, 11, (Color){ 225, 40, 30, 255 });
    DrawCircleV(p, 7,  (Color){ 255, 160, 0, 255 });
    DrawCircleV(p, 3,  (Color){ 255, 240, 160, 255 });
}

static void draw_marker_miss(Vector2 p)
{
    DrawCircleLines((int)p.x, (int)p.y, 11, Fade(WHITE, 0.45f));
    DrawCircleV(p, 5, Fade(WHITE, 0.85f));
}

/* reveal  = draw every ship; otherwise only ships that are already sunk.
 * dimSunk = draw sunk ships in grey (battle) or keep their colours (final reveal). */
static void draw_board(const Board *board, int ox, int oy, bool reveal, bool dimSunk)
{
    int  r, c, i;
    char label[4];

    DrawRectangle(ox - 5, oy - 5, BOARD_PX + 10, BOARD_PX + 10, (Color){ 10, 25, 50, 255 });
    for (r = 0; r < BOARD_SIZE; r++) {
        for (c = 0; c < BOARD_SIZE; c++) {
            Color water = ((r + c) % 2 == 0) ? (Color){ 30, 105, 175, 255 } : (Color){ 36, 116, 190, 255 };
            DrawRectangle(ox + c * CELL, oy + r * CELL, CELL, CELL, water);
        }
    }
    for (i = 0; i <= BOARD_SIZE; i++) {
        DrawLine(ox + i * CELL, oy, ox + i * CELL, oy + BOARD_PX, Fade(WHITE, 0.15f));
        DrawLine(ox, oy + i * CELL, ox + BOARD_PX, oy + i * CELL, Fade(WHITE, 0.15f));
    }
    for (i = 0; i < BOARD_SIZE; i++) {
        snprintf(label, sizeof label, "%d", i + 1);
        text_centered(label, ox + i * CELL + CELL / 2, oy - 24, 18, (Color){ 190, 215, 240, 255 });
        snprintf(label, sizeof label, "%c", 'A' + i);
        text_centered(label, ox - 18, oy + i * CELL + CELL / 2 - 9, 18, (Color){ 190, 215, 240, 255 });
    }

    for (i = 0; i < FLEET_SIZE; i++) {
        const Ship *ship = &board->fleet[i];
        if (ship->placed && (reveal || ship_is_sunk(ship))) {
            draw_ship_on_board(ship, ox, oy, dimSunk && ship_is_sunk(ship), 1.0f);
        }
    }
    for (r = 0; r < BOARD_SIZE; r++) {
        for (c = 0; c < BOARD_SIZE; c++) {
            Coord k = { r, c };
            if (board->cells[r][c] == CELL_HIT)       draw_marker_hit(cell_center(ox, oy, k));
            else if (board->cells[r][c] == CELL_MISS) draw_marker_miss(cell_center(ox, oy, k));
        }
    }
}

/* ============================================================================
 *  EFFECTS AND BANNER (the "feedback" layer)
 * ========================================================================== */

static void spawn_effect(int kind, Vector2 pos)
{
    int i;
    for (i = 0; i < MAX_FX; i++) {
        if (!gui.fx[i].active) {
            gui.fx[i].active = true;
            gui.fx[i].kind   = kind;
            gui.fx[i].pos    = pos;
            gui.fx[i].age    = 0;
            gui.fx[i].life   = (kind == FX_SUNK) ? 1.3f : (kind == FX_HIT) ? 0.7f : 0.8f;
            return;
        }
    }
}

static void update_effects(float dt)
{
    int i;
    for (i = 0; i < MAX_FX; i++) {
        if (gui.fx[i].active) {
            gui.fx[i].age += dt;
            if (gui.fx[i].age >= gui.fx[i].life) {
                gui.fx[i].active = false;
            }
        }
    }
    if (gui.bannerTimer > 0) {
        gui.bannerTimer -= dt;
    }
}

static void draw_effects(void)
{
    int i, k;
    for (i = 0; i < MAX_FX; i++) {
        const Effect *e = &gui.fx[i];
        float t, fade;
        int   sparks;

        if (!e->active) continue;
        t    = e->age / e->life;
        fade = 1.0f - t;

        if (e->kind == FX_MISS) {
            DrawCircleLines((int)e->pos.x, (int)e->pos.y, 6 + t * 34, Fade(WHITE, fade));
            DrawCircleLines((int)e->pos.x, (int)e->pos.y, 4 + t * 20, Fade(SKYBLUE, fade));
        } else {
            float big = (e->kind == FX_SUNK) ? 2.0f : 1.0f;
            sparks    = (e->kind == FX_SUNK) ? 12 : 8;
            DrawCircleV(e->pos, (10 + t * 26) * big, Fade(ORANGE, fade * 0.85f));
            DrawCircleV(e->pos, (5 + t * 14) * big,  Fade(YELLOW, fade));
            if (e->kind == FX_SUNK) {
                DrawCircleLines((int)e->pos.x, (int)e->pos.y, 14 + t * 70, Fade(GOLD, fade));
            }
            for (k = 0; k < sparks; k++) {
                float a = k * (2.0f * PI / sparks);
                DrawLineEx((Vector2){ e->pos.x + cosf(a) * (10 + t * 12) * big, e->pos.y + sinf(a) * (10 + t * 12) * big },
                           (Vector2){ e->pos.x + cosf(a) * (16 + t * 30) * big, e->pos.y + sinf(a) * (16 + t * 30) * big },
                           2.5f, Fade(RED, fade));
            }
        }
    }
}

static void set_banner(const char *title, const char *sub, Color color)
{
    snprintf(gui.bannerTitle, sizeof gui.bannerTitle, "%s", title);
    snprintf(gui.bannerSub, sizeof gui.bannerSub, "%s", sub);
    gui.bannerColor = color;
    gui.bannerTimer = BANNER_TIME;
}

static void draw_banner(void)
{
    float a, w;
    int   tw, sw;

    if (gui.bannerTimer <= 0) return;
    a  = gui.bannerTimer < 0.6f ? gui.bannerTimer / 0.6f : 1.0f;
    tw = MeasureText(gui.bannerTitle, 42);
    sw = MeasureText(gui.bannerSub, 20);
    w  = (float)((tw > sw ? tw : sw) + 70);

    DrawRectangleRounded((Rectangle){ WIN_W / 2 - w / 2, 14, w, 86 }, 0.3f, 8, Fade(BLACK, 0.5f * a));
    text_shadowed(gui.bannerTitle, WIN_W / 2, 20, 42, Fade(gui.bannerColor, a));
    text_centered(gui.bannerSub, WIN_W / 2, 69, 20, Fade(WHITE, a));
}

static Color result_color(ShotResult r)
{
    return r == SHOT_HIT ? (Color){ 255, 90, 60, 255 }
         : r == SHOT_SUNK ? GOLD
                          : (Color){ 150, 200, 255, 255 };
}

static const char *result_word(ShotResult r)
{
    return r == SHOT_HIT ? "HIT!" : r == SHOT_SUNK ? "SUNK!" : "MISS";
}

/* Turns a shot outcome into banner text, a log line, an on-board effect and sounds. */
static void announce(PlayerId shooter, const ShotOutcome *o)
{
    bool        me = (shooter == PLAYER_HUMAN);
    int         ox = me ? RIGHT_X : LEFT_X;
    char        coord[4], sub[96], log[96];
    const char *word;
    Vector2     where = cell_center(ox, BOARD_Y, o->target);

    coord_format(o->target, coord);

    switch (o->result) {
        case SHOT_MISS:
            word = "miss";
            snprintf(sub, sizeof sub, me ? "You fired at %s - splash, nothing there." : "Enemy fired at %s and missed.", coord);
            set_banner(me ? "MISS" : "ENEMY MISSED", sub, (Color){ 150, 200, 255, 255 });
            spawn_effect(FX_MISS, where);
            break;
        case SHOT_HIT:
            word = "HIT";
            snprintf(sub, sizeof sub, me ? "Direct hit at %s!" : "Enemy hit your ship at %s!", coord);
            set_banner(me ? "HIT!" : "YOU'RE HIT!", sub, (Color){ 255, 90, 60, 255 });
            spawn_effect(FX_HIT, where);
            break;
        case SHOT_SUNK:
            word = "SUNK";
            snprintf(sub, sizeof sub, me ? "You destroyed the enemy %s!" : "Your %s was destroyed!",
                     ship_name_for_type(o->sunkType));
            set_banner(me ? "SHIP DESTROYED!" : "YOUR SHIP SUNK!", sub, GOLD);
            spawn_effect(FX_SUNK, where);
            break;
        default:
            return;
    }
    gui.lastValid  = true;
    gui.lastWho    = shooter;
    gui.lastResult = o->result;
    snprintf(gui.lastCoord, sizeof gui.lastCoord, "%s", coord);

    sound_play_pitched(SND_FIRE, me ? 1.0f : 0.7f);          /* cannon: deeper for the enemy */
    sound_play_later(o->result == SHOT_SUNK ? SND_SUNK :     /* then the result lands */
                     o->result == SHOT_HIT  ? SND_HIT  : SND_MISS, 0.22f);

    snprintf(log, sizeof log, "%s fired at %s: %s%s%s", me ? "You" : "Enemy", coord, word,
             o->result == SHOT_SUNK ? " - " : "",
             o->result == SHOT_SUNK ? ship_name_for_type(o->sunkType) : "");
    snprintf(me ? gui.logHuman : gui.logEnemy, 96, "%s", log);
}

/* ============================================================================
 *  GAME FLOW GLUE (uses the existing game functions)
 * ========================================================================== */

static void select_next_unplaced(void)
{
    int i;
    gui.sel = -1;
    for (i = 0; i < FLEET_SIZE; i++) {
        if (!game.humanBoard->fleet[i].placed) {
            gui.sel = i;
            return;
        }
    }
}

static void start_new_game(void)
{
    game_start_new(&game);
    gui.orient = ORIENT_HORIZONTAL;
    select_next_unplaced();
    game_set_state(&game, STATE_FLEET_FORMATION);
}

static void begin_battle(void)
{
    memset(gui.fx, 0, sizeof gui.fx);
    gui.logHuman[0] = gui.logEnemy[0] = '\0';
    gui.enemyTurn  = false;
    gui.enemyTimer = 0;
    gui.endTimer   = 0;
    gui.lockTimer  = 0;
    gui.lastValid  = false;
    gui.markActive = false;
    game.currentTurn = PLAYER_HUMAN;
    set_banner("BATTLE STATIONS!", "Click a square on the enemy waters to fire.", (Color){ 120, 230, 140, 255 });
    sound_play(SND_START);
    game_set_state(&game, STATE_BATTLE);
}

/* After either side shoots: finish the game or hand the turn over. */
static void after_shot(PlayerId shooter, const ShotOutcome *o)
{
    if (battle_is_over(&game)) {
        battle_declare_winner(&game);
        gui.endTimer = END_DELAY;
        return;
    }
    battle_switch_turn(&game);
    if (shooter == PLAYER_HUMAN) {
        gui.enemyTurn  = true;
        gui.enemyTimer = ENEMY_DELAY;
        gui.markActive = false;                 /* your shot clears the old enemy mark */
    } else {
        gui.enemyTurn  = false;
        gui.lockTimer  = (o->result == SHOT_SUNK) ? PLAYER_LOCK_SUNK : PLAYER_LOCK;
        gui.markActive = true;                  /* keep the enemy's target highlighted */
        gui.markCell   = o->target;
        gui.markResult = o->result;
    }
}

/* ============================================================================
 *  SCREENS
 * ========================================================================== */

static void screen_main_menu(void)
{
    int    i;
    float  t = (float)GetTime();
    Rectangle b = { WIN_W / 2 - 170, 290, 340, 58 };

    text_shadowed("BATTLESHIP", WIN_W / 2, 90, 96, WHITE);
    text_centered("Player vs Computer", WIN_W / 2, 200, 28, (Color){ 170, 210, 245, 255 });

    if (button(b, "New Game", 26, true)) {
        start_new_game();
    }
    b.y += 78;
    if (button(b, "Leaderboard", 26, true)) {
        game_set_state(&game, STATE_LEADERBOARD);
    }
    b.y += 78;
    if (button(b, "Exit", 26, true)) {
        gui.quit = true;
    }

    /* Fleet legend: every ship type, drawn to scale. */
    for (i = 0; i < FLEET_SIZE; i++) {
        float x = 90.0f + i * 195.0f;
        float y = 590.0f + 4.0f * sinf(t * 1.5f + i);
        char  cap[48];
        draw_ship_px((ShipType)i, x, y, ship_length_for_type((ShipType)i), false, 34.0f, false, 1.0f);
        snprintf(cap, sizeof cap, "%s (%d)", ship_name_for_type((ShipType)i), ship_length_for_type((ShipType)i));
        DrawText(cap, (int)x, 640, 16, (Color){ 190, 215, 240, 255 });
    }
}

static void screen_formation(void)
{
    Board *b = game.humanBoard;
    Coord  hc;
    int    i;
    bool   over;
    char   text[64];

    text_shadowed("FLEET FORMATION", WIN_W / 2, 24, 44, WHITE);
    text_centered("Pick a ship, then click your grid to place it.  R or right-click rotates.  Click a placed ship to pick it up.",
                  WIN_W / 2, 92, 18, (Color){ 190, 215, 240, 255 });

    DrawText("YOUR WATERS", LEFT_X, BOARD_Y - 52, 20, WHITE);
    draw_board(b, LEFT_X, BOARD_Y, true, true);

    if (IsKeyPressed(KEY_R) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        gui.orient = (gui.orient == ORIENT_HORIZONTAL) ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;
        sound_play(SND_CLICK);
    }

    /* ---- hover preview and placement on the grid ---- */
    over = mouse_cell(LEFT_X, BOARD_Y, &hc);
    if (over) {
        int idx = b->shipIndex[hc.row][hc.col];
        if (idx >= 0) {                                         /* pick a placed ship back up */
            gui.hand = true;
            DrawRectangleLinesEx((Rectangle){ (float)(LEFT_X + hc.col * CELL), (float)(BOARD_Y + hc.row * CELL),
                                              CELL, CELL }, 3, WHITE);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                sound_play(SND_CLICK);
                gui.orient = b->fleet[idx].orientation;
                board_remove_ship(b, idx);
                gui.sel = idx;
            }
        } else if (gui.sel >= 0) {
            int   len   = b->fleet[gui.sel].length;
            bool  valid = placement_is_valid(b, hc, gui.orient, len);
            Coord cells[MAX_SHIP_LENGTH];

            ship_compute_cells(hc, gui.orient, len, cells);
            if (valid) {
                draw_ship_px((ShipType)gui.sel, (float)(LEFT_X + hc.col * CELL), (float)(BOARD_Y + hc.row * CELL),
                             len, gui.orient == ORIENT_VERTICAL, (float)CELL, false, 0.7f);
            }
            for (i = 0; i < len; i++) {
                if (coord_is_in_bounds(cells[i])) {
                    DrawRectangle(LEFT_X + cells[i].col * CELL, BOARD_Y + cells[i].row * CELL, CELL, CELL,
                                  valid ? Fade(GREEN, 0.25f) : Fade(RED, 0.45f));
                }
            }
            if (valid) {
                gui.hand = true;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    board_place_ship(b, gui.sel, hc, gui.orient);
                    select_next_unplaced();
                    sound_play(SND_PLACE);
                }
            } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                sound_play(SND_ERROR);                  /* illegal spot */
            }
        }
    }

    /* ---- ship palette ---- */
    DrawText("YOUR FLEET", 540, 126, 20, WHITE);
    for (i = 0; i < FLEET_SIZE; i++) {
        const Ship *ship = &b->fleet[i];
        Rectangle   row  = { 540, 160.0f + i * 62.0f, 500, 54 };
        bool        hov  = CheckCollisionPointRec(GetMousePosition(), row);

        DrawRectangleRounded(row, 0.2f, 8, hov ? Fade(WHITE, 0.16f) : Fade(WHITE, 0.08f));
        if (i == gui.sel) {
            DrawRectangleLinesEx(row, 3, (Color){ 120, 230, 140, 255 });
        }
        draw_ship_px((ShipType)i, 556, row.y + 9, ship->length, false, 36, false, ship->placed ? 0.4f : 1.0f);
        DrawText(ship_name_for_type((ShipType)i), 780, (int)row.y + 7, 22, WHITE);
        snprintf(text, sizeof text, ship->placed ? "%d cells - placed (click to move)" : "%d cells - click to select",
                 ship->length);
        DrawText(text, 780, (int)row.y + 32, 15, (Color){ 190, 215, 240, 255 });

        if (hov) {
            gui.hand = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                sound_play(SND_CLICK);
                if (ship->placed) {
                    gui.orient = ship->orientation;
                    board_remove_ship(b, i);
                }
                gui.sel = i;
            }
        }
    }

    snprintf(text, sizeof text, "Orientation: %s", gui.orient == ORIENT_HORIZONTAL ? "Horizontal" : "Vertical");
    DrawText(text, 540, 478, 18, (Color){ 190, 215, 240, 255 });

    if (button((Rectangle){ 540, 510, 160, 44 }, "Rotate (R)", 20, true)) {
        gui.orient = (gui.orient == ORIENT_HORIZONTAL) ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;
    }
    if (button((Rectangle){ 710, 510, 160, 44 }, "Random", 20, true)) {
        formation_reset(&game);
        board_place_fleet_randomly(b);
        select_next_unplaced();
    }
    if (button((Rectangle){ 880, 510, 160, 44 }, "Reset", 20, true)) {
        formation_reset(&game);
        select_next_unplaced();
    }
    if (button((Rectangle){ 540, 580, 310, 56 }, "Start Battle", 26, formation_can_confirm(&game))) {
        begin_battle();
    }
    if (button((Rectangle){ 870, 580, 170, 56 }, "Back", 24, true)) {
        game_set_state(&game, STATE_MAIN_MENU);
    }
    text_centered(formation_can_confirm(&game) ? "Fleet ready, Admiral!" : "Place all 5 ships to start the battle.",
                  790, 650, 18, formation_can_confirm(&game) ? (Color){ 120, 230, 140, 255 } : (Color){ 255, 210, 120, 255 });
}

/* Pulsing frame on the square the enemy just shot at, in the colour of the result. */
static void draw_enemy_mark(void)
{
    float pulse;
    Color c;
    Rectangle r;

    if (!gui.markActive) return;
    pulse = 0.5f + 0.5f * sinf((float)GetTime() * 8.0f);
    c     = result_color(gui.markResult);
    r     = (Rectangle){ (float)(LEFT_X + gui.markCell.col * CELL), (float)(BOARD_Y + gui.markCell.row * CELL), CELL, CELL };

    DrawRectangleRec(r, Fade(c, 0.12f + 0.12f * pulse));
    DrawRectangleLinesEx((Rectangle){ r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, 3, Fade(c, 0.55f + 0.45f * pulse));
}

static void draw_score_panel(void)
{
    Color dim = { 190, 215, 240, 255 };
    char  text[48];
    int   cx = (LEFT_X + BOARD_PX + RIGHT_X) / 2;
    int   y  = BOARD_Y;

    DrawRectangleRounded((Rectangle){ (float)cx - 65, (float)y + 30, 130, 330 }, 0.12f, 8, Fade(BLACK, 0.3f));

    text_centered("SCORE", cx, y + 42, 16, dim);
    snprintf(text, sizeof text, "%d", score_get(&game.score));
    text_shadowed(text, cx, y + 60, 44, WHITE);
    text_centered("SHOTS", cx, y + 118, 16, dim);
    snprintf(text, sizeof text, "%d", game.score.shotsFired);
    text_shadowed(text, cx, y + 136, 34, WHITE);

    /* ---- last shot: who fired, where, and what happened ---- */
    DrawLine(cx - 50, y + 186, cx + 50, y + 186, Fade(WHITE, 0.2f));
    text_centered("LAST SHOT", cx, y + 194, 16, dim);
    if (gui.lastValid) {
        snprintf(text, sizeof text, "%s %s", gui.lastWho == PLAYER_HUMAN ? "You" : "Enemy", gui.lastCoord);
        text_centered(text, cx, y + 214, 18, WHITE);
        text_shadowed(result_word(gui.lastResult), cx, y + 236, 26, result_color(gui.lastResult));
    } else {
        text_centered("-", cx, y + 220, 20, dim);
    }

    /* ---- whose turn ---- */
    DrawLine(cx - 50, y + 274, cx + 50, y + 274, Fade(WHITE, 0.2f));
    if (gui.endTimer > 0) {
        text_centered("BATTLE OVER", cx, y + 290, 18, GOLD);
    } else if (gui.enemyTurn) {
        int dots = (int)(GetTime() * 3) % 4;
        snprintf(text, sizeof text, "AIMING%.*s", dots, "...");
        text_centered("ENEMY", cx, y + 286, 20, (Color){ 255, 120, 100, 255 });
        text_centered(text, cx, y + 312, 18, (Color){ 255, 120, 100, 255 });
    } else if (gui.lockTimer > 0) {
        snprintf(text, sizeof text, "Wait %.1fs", gui.lockTimer);
        text_centered("ENEMY FIRED", cx, y + 286, 18, (Color){ 255, 120, 100, 255 });
        text_centered(text, cx, y + 312, 18, (Color){ 255, 210, 120, 255 });
    } else {
        text_centered("YOUR TURN", cx, y + 286, 20, (Color){ 120, 230, 140, 255 });
        text_centered("Fire away!", cx, y + 312, 18, (Color){ 120, 230, 140, 255 });
    }
}

static void screen_battle(float dt)
{
    Coord hc;
    bool  canFire;

    if (gui.lockTimer > 0) {
        gui.lockTimer -= dt;
    }
    canFire = !gui.enemyTurn && gui.endTimer <= 0 && gui.lockTimer <= 0;

    DrawText("YOUR FLEET", LEFT_X, BOARD_Y - 52, 22, WHITE);
    DrawText("ENEMY WATERS  -  click to fire", RIGHT_X, BOARD_Y - 52, 22, WHITE);
    draw_board(game.humanBoard, LEFT_X, BOARD_Y, true, true);
    draw_board(game.computerBoard, RIGHT_X, BOARD_Y, false, true);

    /* ---- your shot ---- */
    if (gui.endTimer <= 0 && mouse_cell(RIGHT_X, BOARD_Y, &hc)) {
        if (canFire && board_classify_shot(game.computerBoard, hc) != SHOT_INVALID) {
            Vector2 p = cell_center(RIGHT_X, BOARD_Y, hc);
            gui.hand = true;
            DrawRectangle(RIGHT_X + hc.col * CELL, BOARD_Y + hc.row * CELL, CELL, CELL, Fade(WHITE, 0.22f));
            DrawLineEx((Vector2){ p.x - 12, p.y }, (Vector2){ p.x + 12, p.y }, 2, WHITE);
            DrawLineEx((Vector2){ p.x, p.y - 12 }, (Vector2){ p.x, p.y + 12 }, 2, WHITE);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                ShotOutcome out;
                battle_apply_human_shot(&game, hc, &out);
                announce(PLAYER_HUMAN, &out);
                after_shot(PLAYER_HUMAN, &out);
            }
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            sound_play(SND_ERROR);              /* already shot there, or not your turn yet */
        }
    }

    /* ---- the computer's shot (after a short pause) ---- */
    if (gui.enemyTurn && gui.endTimer <= 0) {
        gui.enemyTimer -= dt;
        if (gui.enemyTimer <= 0) {
            ShotOutcome out = battle_computer_turn(&game);   /* asks the AI, applies the shot */
            announce(PLAYER_COMPUTER, &out);
            after_shot(PLAYER_COMPUTER, &out);
        }
    }

    /* ---- let the last explosion play, then show the result screen ---- */
    if (gui.endTimer > 0) {
        gui.endTimer -= dt;
        if (gui.endTimer <= 0) {
            sound_play(!game.hasWinner ? SND_DRAW : game.winner == PLAYER_HUMAN ? SND_WIN : SND_LOSE);
            game_set_state(&game, STATE_GAME_OVER);
        }
    }

    draw_enemy_mark();
    draw_effects();
    draw_score_panel();

    DrawText("Your ships:", LEFT_X, BOARD_Y + BOARD_PX + 14, 16, (Color){ 190, 215, 240, 255 });
    draw_fleet_strip(game.humanBoard, LEFT_X + 100, BOARD_Y + BOARD_PX + 14);
    DrawText("Enemy ships:", RIGHT_X, BOARD_Y + BOARD_PX + 14, 16, (Color){ 190, 215, 240, 255 });
    draw_fleet_strip(game.computerBoard, RIGHT_X + 110, BOARD_Y + BOARD_PX + 14);

    DrawText(gui.logHuman, LEFT_X, BOARD_Y + BOARD_PX + 52, 20, (Color){ 120, 230, 140, 255 });
    DrawText(gui.logEnemy, LEFT_X, BOARD_Y + BOARD_PX + 80, 20, (Color){ 255, 130, 110, 255 });

    draw_banner();
}

static void screen_game_over(void)
{
    char text[96];
    bool draw = !game.hasWinner;
    bool won  = !draw && (game.winner == PLAYER_HUMAN);

    text_shadowed(draw ? "DRAW" : won ? "VICTORY!" : "DEFEAT", WIN_W / 2, 24, 64,
                  draw ? WHITE : won ? GOLD : (Color){ 255, 110, 90, 255 });
    snprintf(text, sizeof text, draw ? "Nobody won.  You fired %d shots.  Score: %d"
                             : won  ? "You sank the enemy fleet in %d shots.  Score: %d"
                                    : "The computer sank your fleet.  You fired %d shots.  Score: %d",
             game.score.shotsFired, score_get(&game.score));
    text_centered(text, WIN_W / 2, 90, 22, WHITE);

    DrawText("YOUR FLEET", LEFT_X, BOARD_Y - 52, 22, WHITE);
    DrawText("ENEMY FLEET (revealed)", RIGHT_X, BOARD_Y - 52, 22, WHITE);
    draw_board(game.humanBoard, LEFT_X, BOARD_Y, true, false);
    draw_board(game.computerBoard, RIGHT_X, BOARD_Y, true, false);

    if (button((Rectangle){ WIN_W / 2 - 110, 630, 220, 56 }, "Continue", 26, true)) {
        gui.nameLen  = 0;
        gui.name[0]  = '\0';
        game_set_state(&game, next_state_after_game_over(&game));
    }
}

static void screen_name_entry(void)
{
    char      text[64];
    int       ch;
    bool      ok;
    Rectangle box = { WIN_W / 2 - 200, 330, 400, 56 };

    text_shadowed("NEW HIGH SCORE!", WIN_W / 2, 140, 64, GOLD);
    snprintf(text, sizeof text, "You made the leaderboard with %d points.", score_get(&game.score));
    text_centered(text, WIN_W / 2, 230, 24, WHITE);
    text_centered("Enter your name:", WIN_W / 2, 290, 22, (Color){ 190, 215, 240, 255 });

    while ((ch = GetCharPressed()) > 0) {
        if (ch >= 32 && ch <= 126 && gui.nameLen < MAX_NAME_LENGTH) {
            gui.name[gui.nameLen++] = (char)ch;
            gui.name[gui.nameLen]   = '\0';
        }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && gui.nameLen > 0) {
        gui.name[--gui.nameLen] = '\0';
    }

    DrawRectangleRounded(box, 0.2f, 8, (Color){ 10, 25, 50, 255 });
    DrawRectangleLinesEx(box, 2, (Color){ 120, 180, 240, 255 });
    DrawText(gui.name, (int)box.x + 14, (int)box.y + 14, 28, WHITE);
    if (((int)(GetTime() * 2)) % 2 == 0) {
        DrawRectangle((int)box.x + 16 + MeasureText(gui.name, 28), (int)box.y + 12, 3, 32, WHITE);
    }

    {   /* a name made only of spaces is not allowed */
        int i;
        ok = false;
        for (i = 0; i < gui.nameLen; i++) {
            if (gui.name[i] != ' ') ok = true;
        }
    }
    if ((button((Rectangle){ WIN_W / 2 - 110, 430, 220, 56 }, "Save Score", 24, ok) || (ok && IsKeyPressed(KEY_ENTER)))) {
        char trimmed[MAX_NAME_LENGTH + 1];
        int  start = 0, end = gui.nameLen;
        while (gui.name[start] == ' ') start++;
        while (end > start && gui.name[end - 1] == ' ') end--;
        memcpy(trimmed, gui.name + start, (size_t)(end - start));
        trimmed[end - start] = '\0';

        leaderboard_insert(&game.leaderboard, trimmed, score_get(&game.score));
        leaderboard_save(&game.leaderboard, LEADERBOARD_FILE);
        game_set_state(&game, STATE_LEADERBOARD);
    }
}

static void screen_leaderboard(void)
{
    static const Color medal[3] = { { 255, 200, 40, 255 }, { 200, 205, 215, 255 }, { 205, 127, 50, 255 } };
    int  i;
    char text[64];

    text_shadowed("LEADERBOARD", WIN_W / 2, 70, 64, WHITE);
    text_centered("Top 3 - the fewer shots you take, the higher your score", WIN_W / 2, 150, 20,
                  (Color){ 190, 215, 240, 255 });

    for (i = 0; i < LEADERBOARD_SLOTS; i++) {
        Rectangle row = { WIN_W / 2 - 300, 220.0f + i * 100.0f, 600, 82 };
        DrawRectangleRounded(row, 0.2f, 8, Fade(BLACK, 0.35f));
        DrawCircle((int)row.x + 48, (int)row.y + 41, 28, medal[i]);
        snprintf(text, sizeof text, "%d", i + 1);
        text_centered(text, (int)row.x + 48, (int)row.y + 24, 36, (Color){ 40, 40, 50, 255 });

        if (i < game.leaderboard.count) {
            DrawText(game.leaderboard.entries[i].name, (int)row.x + 100, (int)row.y + 24, 32, WHITE);
            snprintf(text, sizeof text, "%d", game.leaderboard.entries[i].score);
            DrawText(text, (int)(row.x + row.width - 30 - MeasureText(text, 40)), (int)row.y + 20, 40, medal[i]);
        } else {
            DrawText("---", (int)row.x + 100, (int)row.y + 24, 32, Fade(WHITE, 0.4f));
        }
    }
    if (button((Rectangle){ WIN_W / 2 - 110, 560, 220, 56 }, "Main Menu", 24, true)) {
        game_set_state(&game, STATE_MAIN_MENU);
    }
}

/* ============================================================================
 *  MAIN LOOP
 * ========================================================================== */

int main(void)
{
    seed_random();
    if (!game_init(&game)) {
        return 1;
    }

    InitWindow(WIN_W, WIN_H, "Battleship");
    SetExitKey(KEY_NULL);                       /* ESC must not close the game mid-battle */
    SetTargetFPS(60);
    sound_init();

    while (!gui.quit && !WindowShouldClose()) {
        float dt = GetFrameTime();

        gui.hand = false;
        update_effects(dt);
        sound_update(dt);
        if (IsKeyPressed(KEY_M)) {
            sound_toggle_mute();
        }

        BeginDrawing();
        draw_background();
        switch (game.state) {
            case STATE_MAIN_MENU:       screen_main_menu();    break;
            case STATE_FLEET_FORMATION: screen_formation();    break;
            case STATE_BATTLE:          screen_battle(dt);     break;
            case STATE_GAME_OVER:       screen_game_over();    break;
            case STATE_NAME_ENTRY:      screen_name_entry();   break;
            case STATE_LEADERBOARD:     screen_leaderboard();  break;
            default:                    gui.quit = true;       break;
        }
        DrawText(sound_is_muted() ? "Sound: OFF (M)" : "Sound: ON (M)", WIN_W - 150, WIN_H - 26, 16, Fade(WHITE, 0.55f));
        EndDrawing();

        SetMouseCursor(gui.hand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }

    sound_shutdown();
    CloseWindow();
    game_shutdown(&game);
    return 0;
}

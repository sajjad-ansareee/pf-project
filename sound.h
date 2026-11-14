/* ============================================================================
 *  sound.h  -  Procedurally generated sound effects (raylib audio)
 *
 *  No audio files are needed: every effect is synthesised at start-up.
 *  If no audio device is available (e.g. a headless machine) every function
 *  silently does nothing, so the game still runs.
 * ========================================================================== */

#ifndef SOUND_H
#define SOUND_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    SND_CLICK,          /* button / menu click                    */
    SND_PLACE,          /* ship placed on the grid                */
    SND_ERROR,          /* illegal action (bad placement, locked) */
    SND_START,          /* battle begins                          */
    SND_FIRE,           /* cannon shot                            */
    SND_MISS,           /* shell lands in water                   */
    SND_HIT,            /* ship hit                               */
    SND_SUNK,           /* ship destroyed                         */
    SND_WIN,            /* victory fanfare                        */
    SND_LOSE,           /* defeat                                 */
    SND_DRAW,           /* draw                                   */
    SND_COUNT
} SoundId;

Wave sound_make_wave(SoundId id);                   /* synthesise one effect (caller unloads it) */

void sound_init(void);                              /* open device + build all effects           */
void sound_shutdown(void);
void sound_play(SoundId id);
void sound_play_pitched(SoundId id, float pitch);   /* 1.0 = normal, 0.7 = lower                 */
void sound_play_later(SoundId id, float delaySec);  /* e.g. splash a moment after the cannon     */
void sound_update(float dt);                        /* call once per frame (runs delayed sounds) */
void sound_toggle_mute(void);
bool sound_is_muted(void);

#endif /* SOUND_H */

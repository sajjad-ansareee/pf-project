/* ============================================================================
 *  sound.c  -  Sound effect synthesis and playback
 *
 *  Building blocks:
 *      add_tone()   a (sweeping) note with optional harmonics and a decay
 *      add_noise()  low-passed noise burst (explosions, water)
 *  Each effect is a few of these mixed into one buffer, then normalised.
 * ========================================================================== */

#include "sound.h"

#include <math.h>
#include <stdlib.h>

#define SAMPLE_RATE 22050
#define TWO_PI      6.28318530718f
#define MAX_PENDING 8

typedef struct {
    float *data;
    int    frames;
} Mix;

static Sound sounds[SND_COUNT];
static bool  ready = false;
static bool  muted = false;

static struct {
    SoundId id;
    float   timeLeft;
    bool    active;
} pending[MAX_PENDING];

/* ============================================================================
 *  SYNTHESIS
 * ========================================================================== */

/* Own tiny generator so the game's rand() sequence is never disturbed. */
static float random_noise(void)
{
    static unsigned int state = 22222u;
    state = state * 1664525u + 1013904223u;
    return ((state >> 9) & 0xFFFF) / 32768.0f - 1.0f;
}

static Mix mix_new(float seconds)
{
    Mix m;
    m.frames = (int)(seconds * SAMPLE_RATE);
    m.data   = calloc((size_t)m.frames, sizeof(float));
    return m;
}

static void add_tone(Mix *m, float start, float dur, float f0, float f1,
                     float vol, float decay, float h2, float h3)
{
    int   first = (int)(start * SAMPLE_RATE);
    int   count = (int)(dur * SAMPLE_RATE);
    float phase = 0.0f, norm = 1.0f + h2 + h3;
    int   i;

    for (i = 0; i < count && first + i < m->frames; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float f   = f0 + (f1 - f0) * (t / dur);
        float env = expf(-decay * t) * fminf(1.0f, t / 0.005f) * fminf(1.0f, (dur - t) / 0.01f);
        phase += TWO_PI * f / SAMPLE_RATE;
        m->data[first + i] += vol * env * (sinf(phase) + h2 * sinf(2 * phase) + h3 * sinf(3 * phase)) / norm;
    }
}

/* lp0 -> lp1 is the low-pass coefficient (1 = bright, small = dull and rumbling). */
static void add_noise(Mix *m, float start, float dur, float vol, float decay, float lp0, float lp1)
{
    int   first = (int)(start * SAMPLE_RATE);
    int   count = (int)(dur * SAMPLE_RATE);
    float y = 0.0f;
    int   i;

    for (i = 0; i < count && first + i < m->frames; i++) {
        float t   = (float)i / SAMPLE_RATE;
        float a   = lp0 + (lp1 - lp0) * (t / dur);
        float env = expf(-decay * t) * fminf(1.0f, t / 0.003f) * fminf(1.0f, (dur - t) / 0.01f);
        y += a * (random_noise() - y);
        m->data[first + i] += vol * env * y * sqrtf((2.0f - a) / a);   /* keep loudness independent of cutoff */
    }
}

static Wave mix_to_wave(Mix *m, float peakTarget)
{
    Wave   w = { 0 };
    short *out;
    float  peak = 0.0f, scale;
    int    i;

    for (i = 0; i < m->frames; i++) {
        if (fabsf(m->data[i]) > peak) peak = fabsf(m->data[i]);
    }
    scale = (peak > 0.0f) ? peakTarget / peak : 1.0f;

    out = malloc((size_t)m->frames * sizeof(short));
    for (i = 0; i < m->frames; i++) {
        out[i] = (short)(m->data[i] * scale * 32767.0f);
    }
    w.frameCount = (unsigned int)m->frames;
    w.sampleRate = SAMPLE_RATE;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = out;
    free(m->data);
    return w;
}

Wave sound_make_wave(SoundId id)
{
    Mix m;
    int i;

    switch (id) {
        case SND_CLICK:
            m = mix_new(0.08f);
            add_tone(&m, 0.0f, 0.06f, 900, 600, 0.5f, 40, 0, 0);
            return mix_to_wave(&m, 0.35f);

        case SND_PLACE:                                             /* dull thud */
            m = mix_new(0.2f);
            add_tone(&m, 0.0f, 0.15f, 200, 90, 0.8f, 18, 0.3f, 0);
            add_noise(&m, 0.0f, 0.05f, 0.4f, 60, 0.3f, 0.2f);
            return mix_to_wave(&m, 0.6f);

        case SND_ERROR:                                             /* two low buzzes */
            m = mix_new(0.3f);
            add_tone(&m, 0.00f, 0.09f, 150, 150, 0.5f, 10, 0.6f, 0.4f);
            add_tone(&m, 0.11f, 0.12f, 110, 110, 0.5f, 10, 0.6f, 0.4f);
            return mix_to_wave(&m, 0.5f);

        case SND_START:                                             /* two-note horn */
            m = mix_new(0.9f);
            add_tone(&m, 0.00f, 0.25f, 220, 220, 0.5f, 3.0f, 0.5f, 0.3f);
            add_tone(&m, 0.28f, 0.55f, 330, 330, 0.5f, 2.5f, 0.5f, 0.3f);
            return mix_to_wave(&m, 0.6f);

        case SND_FIRE:                                              /* cannon boom */
            m = mix_new(0.4f);
            add_noise(&m, 0.0f, 0.03f, 0.8f, 40, 0.9f, 0.9f);
            add_noise(&m, 0.0f, 0.30f, 0.7f, 11, 0.5f, 0.08f);
            add_tone(&m, 0.0f, 0.25f, 150, 45, 0.9f, 10, 0.2f, 0);
            return mix_to_wave(&m, 0.7f);

        case SND_MISS:                                              /* splash + bloops */
            m = mix_new(0.7f);
            add_noise(&m, 0.0f, 0.60f, 0.6f, 5.0f, 0.6f, 0.08f);
            add_tone(&m, 0.00f, 0.14f, 750, 260, 0.35f, 15, 0, 0);
            add_tone(&m, 0.12f, 0.10f, 950, 380, 0.22f, 20, 0, 0);
            return mix_to_wave(&m, 0.6f);

        case SND_HIT:                                               /* crack + low boom */
            m = mix_new(0.9f);
            add_noise(&m, 0.0f, 0.05f, 0.9f, 30, 0.9f, 0.9f);
            add_noise(&m, 0.0f, 0.80f, 0.8f, 4.5f, 0.35f, 0.04f);
            add_tone(&m, 0.0f, 0.70f, 120, 35, 1.0f, 4.0f, 0.2f, 0);
            return mix_to_wave(&m, 0.9f);

        case SND_SUNK:                                              /* double blast, rumble, sinking tone */
            m = mix_new(2.1f);
            add_noise(&m, 0.00f, 0.05f, 0.9f, 30, 0.9f, 0.9f);
            add_noise(&m, 0.00f, 0.80f, 0.8f, 4.5f, 0.35f, 0.04f);
            add_tone(&m, 0.00f, 0.70f, 120, 35, 1.0f, 4.0f, 0.2f, 0);
            add_noise(&m, 0.30f, 0.80f, 0.7f, 4.0f, 0.30f, 0.04f);
            add_tone(&m, 0.30f, 0.70f, 90, 30, 0.9f, 4.0f, 0.2f, 0);
            add_noise(&m, 0.15f, 1.80f, 0.6f, 1.6f, 0.12f, 0.02f);
            add_tone(&m, 0.40f, 1.50f, 330, 55, 0.35f, 1.5f, 0.4f, 0.2f);
            return mix_to_wave(&m, 0.95f);

        case SND_WIN: {                                             /* rising fanfare */
            static const float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
            m = mix_new(1.7f);
            for (i = 0; i < 4; i++) {
                add_tone(&m, i * 0.16f, i == 3 ? 0.95f : 0.2f, notes[i], notes[i], 0.5f,
                         i == 3 ? 2.5f : 4.0f, 0.4f, 0.2f);
            }
            add_tone(&m, 0.64f, 0.95f, 659.25f, 659.25f, 0.3f, 2.5f, 0.4f, 0.2f);
            add_tone(&m, 0.64f, 0.95f, 783.99f, 783.99f, 0.3f, 2.5f, 0.4f, 0.2f);
            return mix_to_wave(&m, 0.7f);
        }

        case SND_LOSE: {                                            /* falling, slow */
            static const float notes[4] = { 440.0f, 349.23f, 293.66f, 220.0f };
            m = mix_new(2.3f);
            for (i = 0; i < 4; i++) {
                add_tone(&m, i * 0.32f, i == 3 ? 1.3f : 0.34f, notes[i], notes[i] * 0.97f, 0.5f,
                         i == 3 ? 2.0f : 3.0f, 0.5f, 0.25f);
            }
            return mix_to_wave(&m, 0.7f);
        }

        case SND_DRAW:                                              /* neutral, unresolved */
            m = mix_new(1.4f);
            add_tone(&m, 0.00f, 0.22f, 440.0f, 440.0f, 0.5f, 4.0f, 0.3f, 0.1f);
            add_tone(&m, 0.26f, 0.22f, 440.0f, 440.0f, 0.5f, 4.0f, 0.3f, 0.1f);
            add_tone(&m, 0.52f, 0.85f, 329.63f, 329.63f, 0.5f, 2.5f, 0.3f, 0.1f);
            return mix_to_wave(&m, 0.65f);

        default:
            break;
    }
    m = mix_new(0.01f);
    return mix_to_wave(&m, 0.1f);
}

/* ============================================================================
 *  PLAYBACK
 * ========================================================================== */

void sound_init(void)
{
    int i;

    InitAudioDevice();
    ready = IsAudioDeviceReady();
    if (!ready) {
        return;                                     /* no sound card: stay silent */
    }
    for (i = 0; i < SND_COUNT; i++) {
        Wave w = sound_make_wave((SoundId)i);
        sounds[i] = LoadSoundFromWave(w);
        UnloadWave(w);
    }
}

void sound_shutdown(void)
{
    int i;
    if (!ready) {
        return;
    }
    for (i = 0; i < SND_COUNT; i++) {
        UnloadSound(sounds[i]);
    }
    CloseAudioDevice();
    ready = false;
}

void sound_play_pitched(SoundId id, float pitch)
{
    if (!ready || id < 0 || id >= SND_COUNT) {
        return;
    }
    SetSoundPitch(sounds[id], pitch);
    PlaySound(sounds[id]);
}

void sound_play(SoundId id)
{
    sound_play_pitched(id, 1.0f);
}

void sound_play_later(SoundId id, float delaySec)
{
    int i;
    for (i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].active) {
            pending[i].active   = true;
            pending[i].id       = id;
            pending[i].timeLeft = delaySec;
            return;
        }
    }
}

void sound_update(float dt)
{
    int i;
    for (i = 0; i < MAX_PENDING; i++) {
        if (pending[i].active) {
            pending[i].timeLeft -= dt;
            if (pending[i].timeLeft <= 0.0f) {
                pending[i].active = false;
                sound_play(pending[i].id);
            }
        }
    }
}

void sound_toggle_mute(void)
{
    muted = !muted;
    if (ready) {
        SetMasterVolume(muted ? 0.0f : 1.0f);
    }
}

bool sound_is_muted(void)
{
    return muted;
}

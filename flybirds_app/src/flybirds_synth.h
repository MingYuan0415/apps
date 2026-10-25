/**
 * @brief Flybirds procedural audio: a chiptune loop plus one-shot effects.
 *
 * Everything is synthesised on the caller's task at the product PCM format
 * (16 kHz, 16-bit, interleaved stereo). No audio files, decoder, or LVGL
 * dependency exists here, so the host suite can render and assert the stream.
 */

#ifndef __FLYBIRDS_SYNTH_H__
#define __FLYBIRDS_SYNTH_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** PCM rate matching audio_service's product configuration. */
#define FLYBIRDS_SYNTH_SAMPLE_RATE  16000U
/** Interleaved channel count matching audio_service's product configuration. */
#define FLYBIRDS_SYNTH_CHANNELS     2U
/** Frames rendered by one audio block (20 ms). */
#define FLYBIRDS_SYNTH_BLOCK_FRAMES 320U
/** Frames per sequencer step (a 120 BPM eighth note). */
#define FLYBIRDS_SYNTH_STEP_FRAMES  4000U
/** Sequencer steps in one loop: eight 4/4 bars of eighth notes. */
#define FLYBIRDS_SYNTH_STEPS        64U
/** Length of the music loop in frames (16 s). */
#define FLYBIRDS_SYNTH_LOOP_FRAMES \
    (FLYBIRDS_SYNTH_STEP_FRAMES * FLYBIRDS_SYNTH_STEPS)

/** One-shot effect voice selected by gameplay events. */
typedef enum
{
    FLYBIRDS_SFX_FLAP = 0, /**< Wing beat. */
    FLYBIRDS_SFX_SCORE,    /**< Pipe threaded. */
    FLYBIRDS_SFX_HIT,      /**< Crash. */
    FLYBIRDS_SFX_OVER,     /**< Run summary sting. */
    FLYBIRDS_SFX_COUNT,    /**< Number of effect voices. */
} flybirds_sfx_t;

/**
 * @brief Complete synthesiser state, owned by the audio task.
 *
 * The struct is exposed so the audio module can allocate it statically; treat
 * the fields as private to flybirds_synth.c.
 */
typedef struct flybirds_synth
{
    bool music_enabled;
    bool sfx_enabled;
    uint32_t frame;      /**< Frames rendered since init. */
    uint32_t step;       /**< Last sequencer step applied. */
    uint32_t lead_phase;
    uint32_t lead_inc;
    uint32_t arp_phase;
    uint32_t arp_inc;
    uint32_t bass_phase;
    uint32_t bass_inc;
    uint16_t noise;
    uint16_t noise_divider;
    uint8_t sfx_kind;    /**< Zero when silent, otherwise voice plus one. */
    uint32_t sfx_frame;  /**< Frames rendered inside the active voice. */
    uint32_t sfx_phase;
    uint16_t sfx_noise;
    uint16_t sfx_noise_divider;
} flybirds_synth_t;

/**
 * @brief Reset the synth to the top of the loop with both buses enabled.
 * @param synth is the state to initialise.
 */
void flybirds_synth_init(flybirds_synth_t *synth);

/**
 * @brief Enable or disable the music bus without losing loop position.
 * @param synth is the state to update.
 * @param enabled keeps the chiptune voices rendering when true.
 */
void flybirds_synth_set_music(flybirds_synth_t *synth, bool enabled);

/**
 * @brief Enable or disable the effect bus.
 * @param synth is the state to update.
 * @param enabled lets triggered effects sound when true.
 */
void flybirds_synth_set_sfx(flybirds_synth_t *synth, bool enabled);

/**
 * @brief Return the music bus state.
 * Test observation point: the audio task drives the bus through the setter.
 *
 * @param synth is the state to inspect.
 * @return true when music renders.
 */
bool flybirds_synth_music_enabled(const flybirds_synth_t *synth);

/**
 * @brief Return the effect bus state.
 * Test observation point: the audio task drives the bus through the setter.
 *
 * @param synth is the state to inspect.
 * @return true when effects render.
 */
bool flybirds_synth_sfx_enabled(const flybirds_synth_t *synth);

/**
 * @brief Start one effect voice, replacing any voice still playing.
 * @param synth is the state to update.
 * @param sfx selects the effect; out-of-range values are ignored.
 */
void flybirds_synth_trigger(flybirds_synth_t *synth, flybirds_sfx_t sfx);

/**
 * @brief Render interleaved stereo frames, advancing the loop and voices.
 * @param synth is the state to advance.
 * @param interleaved receives frames times two samples.
 * @param frames is the requested frame count; zero writes nothing.
 */
void flybirds_synth_render(flybirds_synth_t *synth, int16_t *interleaved,
                           size_t frames);

#endif /* __FLYBIRDS_SYNTH_H__ */

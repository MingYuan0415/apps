/**
 * @brief  Host test for the Flybirds chiptune and effect synthesiser.
 * @note   Compiles only flybirds_synth.c; no LVGL or ESP-IDF shims are involved.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flybirds_synth.h"

#define TEST_BLOCK_FRAMES  FLYBIRDS_SYNTH_BLOCK_FRAMES
#define TEST_PCM_SAMPLES   (TEST_BLOCK_FRAMES * FLYBIRDS_SYNTH_CHANNELS)

static int16_t s_pcm[TEST_PCM_SAMPLES];

static int32_t _peak(const int16_t *pcm, size_t frames)
{
    int32_t peak = 0;
    for (size_t index = 0U; index < frames * FLYBIRDS_SYNTH_CHANNELS; ++index)
    {
        const int32_t value = pcm[index];
        if (abs((int)value) > peak)
        {
            peak = abs((int)value);
        }
    }
    return peak;
}

static void _assert_stereo_identical(const int16_t *pcm, size_t frames)
{
    for (size_t index = 0U; index < frames; ++index)
    {
        assert(pcm[index * FLYBIRDS_SYNTH_CHANNELS] ==
               pcm[index * FLYBIRDS_SYNTH_CHANNELS + 1U]);
    }
}

static void test_silence_when_both_buses_disabled(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_music(&synth, false);
    flybirds_synth_set_sfx(&synth, false);
    flybirds_synth_trigger(&synth, FLYBIRDS_SFX_FLAP);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) == 0);
}

static void test_music_renders_on_both_channels(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_sfx(&synth, false);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) > 1000);
    _assert_stereo_identical(s_pcm, TEST_BLOCK_FRAMES);
    assert(flybirds_synth_music_enabled(&synth));
    assert(!flybirds_synth_sfx_enabled(&synth));
}

static void test_render_is_deterministic(void)
{
    flybirds_synth_t first;
    flybirds_synth_t second;
    int16_t first_pcm[TEST_PCM_SAMPLES];
    int16_t second_pcm[TEST_PCM_SAMPLES];
    flybirds_synth_init(&first);
    flybirds_synth_init(&second);
    flybirds_synth_trigger(&first, FLYBIRDS_SFX_SCORE);
    flybirds_synth_trigger(&second, FLYBIRDS_SFX_SCORE);
    flybirds_synth_render(&first, first_pcm, TEST_BLOCK_FRAMES);
    flybirds_synth_render(&second, second_pcm, TEST_BLOCK_FRAMES);
    assert(memcmp(first_pcm, second_pcm, sizeof(first_pcm)) == 0);
}

static void test_effect_decays_to_exact_silence(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_music(&synth, false);
    flybirds_synth_trigger(&synth, FLYBIRDS_SFX_FLAP);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) > 1000);
    /* The flap voice lasts 700 frames; once it drains the stream is mute. */
    for (size_t block = 0U; block < 3U; ++block)
    {
        flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    }
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) == 0);
}

static void test_effect_requires_the_sfx_bus(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_music(&synth, false);
    flybirds_synth_set_sfx(&synth, false);
    flybirds_synth_trigger(&synth, FLYBIRDS_SFX_HIT);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) == 0);
    flybirds_synth_set_sfx(&synth, true);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) == 0);
    flybirds_synth_trigger(&synth, FLYBIRDS_SFX_HIT);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) > 1000);
}

static void test_out_of_range_effect_is_ignored(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_music(&synth, false);
    flybirds_synth_trigger(&synth, FLYBIRDS_SFX_COUNT);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) == 0);
}

static void test_music_and_effects_leave_headroom(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    size_t clipped = 0U;
    for (size_t block = 0U; block < 8U; ++block)
    {
        if ((block & 1U) == 0U)
        {
            flybirds_synth_trigger(&synth, FLYBIRDS_SFX_HIT);
        }
        flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
        for (size_t index = 0U; index < TEST_PCM_SAMPLES; ++index)
        {
            if (s_pcm[index] == 32767 || s_pcm[index] == -32768)
            {
                ++clipped;
            }
        }
    }
    assert(clipped == 0U);
}

static void test_music_loops_without_a_gap(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_set_sfx(&synth, false);
    /* Advance to one block before the loop seam, then render across it. */
    synth.frame = FLYBIRDS_SYNTH_LOOP_FRAMES - TEST_BLOCK_FRAMES / 2U;
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) > 0);
    assert(synth.frame == FLYBIRDS_SYNTH_LOOP_FRAMES + TEST_BLOCK_FRAMES / 2U);
}

static void test_mute_keeps_loop_position(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    const uint32_t position = synth.frame;
    flybirds_synth_set_music(&synth, false);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(synth.frame == position + TEST_BLOCK_FRAMES);
    flybirds_synth_set_music(&synth, true);
    flybirds_synth_render(&synth, s_pcm, TEST_BLOCK_FRAMES);
    assert(_peak(s_pcm, TEST_BLOCK_FRAMES) > 0);
}

static void test_null_and_empty_render_are_safe(void)
{
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_render(&synth, NULL, TEST_BLOCK_FRAMES);
    flybirds_synth_render(&synth, s_pcm, 0U);
    assert(synth.frame == 0U);
    flybirds_synth_render(NULL, s_pcm, 4U);
    assert(_peak(s_pcm, 4U) == 0);
}

static void _assert_synth_state(const flybirds_synth_t *actual,
                                const flybirds_synth_t *expected)
{
    assert(actual->frame == expected->frame);
    assert(actual->step == expected->step);
    assert(actual->lead_inc == expected->lead_inc);
    assert(actual->arp_inc == expected->arp_inc);
    assert(actual->bass_inc == expected->bass_inc);
    assert(actual->lead_phase == expected->lead_phase);
    assert(actual->arp_phase == expected->arp_phase);
    assert(actual->bass_phase == expected->bass_phase);
    assert(actual->music_enabled == expected->music_enabled);
    assert(actual->sfx_enabled == expected->sfx_enabled);
    assert(actual->noise == expected->noise);
    assert(actual->noise_divider == expected->noise_divider);
    assert(actual->sfx_kind == expected->sfx_kind);
    assert(actual->sfx_frame == expected->sfx_frame);
    assert(actual->sfx_phase == expected->sfx_phase);
    assert(actual->sfx_noise == expected->sfx_noise);
    assert(actual->sfx_noise_divider == expected->sfx_noise_divider);
}

/* Independent note fixtures for the boundaries exercised below, not a second
 * synthesizer: no waveform, envelope, noise, or effect code is reproduced. */
static void _advance_expected_silence(flybirds_synth_t *expected, size_t frames)
{
    for (size_t index = 0U; index < frames; ++index)
    {
        const uint32_t step = (expected->frame % 256000U) / 4000U;
        uint32_t lead = 0U;
        uint32_t arp = 0U;
        uint32_t bass = 0U;
        switch (step)
        {
        case 0U:
            lead = 440U;
            arp = 220U;
            bass = 110U;
            break;
        case 1U:
            arp = 330U;
            break;
        case 2U:
            lead = 524U;
            arp = 440U;
            break;
        case 12U:
            lead = 392U;
            arp = 196U;
            bass = 98U;
            break;
        case 13U:
            arp = 392U;
            break;
        case 63U:
            arp = 440U;
            break;
        default:
            assert(false);
        }
        expected->step = step;
        expected->lead_inc = (lead << 16) / 16000U;
        expected->arp_inc = (arp << 16) / 16000U;
        expected->bass_inc = (bass << 16) / 16000U;
        expected->lead_phase += expected->lead_inc;
        expected->arp_phase += expected->arp_inc;
        expected->bass_phase += expected->bass_inc;
        ++expected->frame;
    }
}

static void _test_silent_boundary(uint32_t frame, size_t frames, bool independent)
{
    flybirds_synth_t bulk;
    flybirds_synth_init(&bulk);
    bulk.frame = frame;
    bulk.lead_phase = UINT32_MAX - 29U;
    bulk.arp_phase = UINT32_MAX - 79U;
    bulk.bass_phase = UINT32_MAX - 101U;
    bulk.noise_divider = 31U;
    bulk.sfx_frame = 17U;
    bulk.sfx_phase = 99U;
    bulk.sfx_noise_divider = 42U;
    flybirds_synth_set_music(&bulk, false);
    flybirds_synth_t singles = bulk;
    flybirds_synth_t expected = bulk;
    int16_t *muted = malloc(frames * 2U * sizeof(*muted));
    assert(muted != NULL);
    memset(muted, 0x7F, frames * 2U * sizeof(*muted));
    flybirds_synth_render(&bulk, muted, frames);
    for (size_t index = 0U; index < frames; ++index)
    {
        int16_t one[2] = {1, 1};
        flybirds_synth_render(&singles, one, 1U);
        assert(one[0] == 0 && one[1] == 0);
        assert(muted[index * 2U] == 0 && muted[index * 2U + 1U] == 0);
    }
    free(muted);
    if (independent)
    {
        _advance_expected_silence(&expected, frames);
        _assert_synth_state(&bulk, &expected);
    }
    else
    {
        assert(bulk.frame == frame + (uint32_t)frames);
        assert(bulk.step == ((bulk.frame - 1U) % 256000U) / 4000U);
        assert(bulk.noise == expected.noise);
        assert(bulk.noise_divider == expected.noise_divider);
        assert(bulk.sfx_frame == expected.sfx_frame);
        assert(bulk.sfx_phase == expected.sfx_phase);
        assert(bulk.sfx_noise == expected.sfx_noise);
        assert(bulk.sfx_noise_divider == expected.sfx_noise_divider);
    }
    _assert_synth_state(&bulk, &singles);
    flybirds_synth_set_music(&bulk, true);
    flybirds_synth_set_music(&singles, true);
    int16_t first[TEST_PCM_SAMPLES];
    int16_t second[TEST_PCM_SAMPLES];
    flybirds_synth_render(&bulk, first, TEST_BLOCK_FRAMES);
    flybirds_synth_render(&singles, second, TEST_BLOCK_FRAMES);
    assert(memcmp(first, second, sizeof(first)) == 0);
    _assert_synth_state(&bulk, &singles);
}

static void test_silent_chunks_preserve_exact_sequencer_state(void)
{
    _test_silent_boundary(0U, 4000U, true);
    _test_silent_boundary(0U, 4001U, true);
    _test_silent_boundary(0U, 8001U, true);
    _test_silent_boundary(255999U, 8002U, true);
    _test_silent_boundary(UINT32_MAX, 1U, true);
    _test_silent_boundary(UINT32_MAX, 8002U, true);
    _test_silent_boundary(UINT32_MAX - 5000U, 13002U, true);
    _test_silent_boundary(0U, 256001U, false);
}

int main(void)
{
    test_silence_when_both_buses_disabled();
    test_music_renders_on_both_channels();
    test_render_is_deterministic();
    test_effect_decays_to_exact_silence();
    test_effect_requires_the_sfx_bus();
    test_out_of_range_effect_is_ignored();
    test_music_and_effects_leave_headroom();
    test_music_loops_without_a_gap();
    test_mute_keeps_loop_position();
    test_null_and_empty_render_are_safe();
    test_silent_chunks_preserve_exact_sequencer_state();
    printf("flybirds_synth_test: ok\n");
    return 0;
}

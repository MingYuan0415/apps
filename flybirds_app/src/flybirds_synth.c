#include "flybirds_synth.h"

#include <string.h>

#define FLYBIRDS_MUSIC_SCALE      128
/* The effect bus keeps one extra bit of headroom (music 128 x 2) so a hit
 * layered over four music voices still fits int16 without hard clipping. */
#define FLYBIRDS_SFX_DIVISOR      (FLYBIRDS_MUSIC_SCALE * 2)
#define FLYBIRDS_LEAD_VOLUME      30
#define FLYBIRDS_ARP_VOLUME       14
#define FLYBIRDS_BASS_VOLUME      22
#define FLYBIRDS_HAT_VOLUME       10
#define FLYBIRDS_PULSE_DUTY_LEAD  16384U  /* 25 % */
#define FLYBIRDS_PULSE_DUTY_SFX   32768U  /* 50 % */
#define FLYBIRDS_NOISE_SEED       0x4A5BU
#define FLYBIRDS_SFX_NOISE_SEED   0x2C91U
#define FLYBIRDS_GATE_LEAD        72U
#define FLYBIRDS_GATE_ARP         42U
#define FLYBIRDS_GATE_BASS        80U
#define FLYBIRDS_GATE_HAT         18U

/** One rendered segment of a one-shot effect; frames advance the cursor. */
typedef struct flybirds_sfx_segment
{
    uint16_t frames;   /**< Segment duration. */
    uint16_t hz_start; /**< Pitch at the first frame. */
    uint16_t hz_end;   /**< Pitch at the last frame. */
    uint8_t wave;      /**< Zero is a pulse, one is noise. */
    uint8_t volume;    /**< Peak amplitude before the scale divide. */
} flybirds_sfx_segment_t;

/* C4..B4 equal-tempered pitches; every note is derived by octave shifting. */
static const uint16_t s_semitone_hz[12] =
{
    262U, 277U, 294U, 311U, 330U, 349U,
    370U, 392U, 415U, 440U, 466U, 494U,
};

/* A minor phrase, one entry per eighth note; zero is a rest. */
static const uint8_t s_lead_melody[FLYBIRDS_SYNTH_STEPS] =
{
    69U, 0U, 72U, 0U, 76U, 0U, 74U, 0U,
    72U, 0U, 69U, 0U, 67U, 0U, 69U, 0U,
    76U, 0U, 74U, 0U, 72U, 0U, 69U, 0U,
    67U, 0U, 64U, 0U, 67U, 0U, 69U, 0U,
    69U, 0U, 72U, 0U, 76U, 0U, 79U, 0U,
    76U, 0U, 74U, 0U, 72U, 0U, 74U, 0U,
    76U, 0U, 72U, 0U, 69U, 0U, 67U, 0U,
    69U, 0U, 0U, 0U, 64U, 0U, 0U, 0U,
};

/* One chord root per bar: Am, G, C, Am, Am, C, G, Am. */
static const uint8_t s_chord_roots[8] =
{
    57U, 55U, 60U, 57U, 57U, 60U, 55U, 57U,
};

static const uint8_t s_arp_pattern[8] =
{
    0U, 7U, 12U, 7U, 0U, 12U, 7U, 12U,
};

static const flybirds_sfx_segment_t s_sfx_flap[] =
{
    {
        .frames = 700U, .hz_start = 900U, .hz_end = 1500U,
        .wave = 0U, .volume = 80U
    },
};

static const flybirds_sfx_segment_t s_sfx_score[] =
{
    {
        .frames = 900U, .hz_start = 880U, .hz_end = 880U,
        .wave = 0U, .volume = 88U
    },
    {
        .frames = 1200U, .hz_start = 1318U, .hz_end = 1318U,
        .wave = 0U, .volume = 88U
    },
};

static const flybirds_sfx_segment_t s_sfx_hit[] =
{
    {
        .frames = 1200U, .hz_start = 0U, .hz_end = 0U,
        .wave = 1U, .volume = 92U
    },
    {
        .frames = 900U, .hz_start = 200U, .hz_end = 70U,
        .wave = 0U, .volume = 100U
    },
};

static const flybirds_sfx_segment_t s_sfx_over[] =
{
    {
        .frames = 1400U, .hz_start = 660U, .hz_end = 660U,
        .wave = 0U, .volume = 84U
    },
    {
        .frames = 1200U, .hz_start = 554U, .hz_end = 554U,
        .wave = 0U, .volume = 84U
    },
    {
        .frames = 1200U, .hz_start = 440U, .hz_end = 440U,
        .wave = 0U, .volume = 84U
    },
    {
        .frames = 1800U, .hz_start = 330U, .hz_end = 330U,
        .wave = 0U, .volume = 84U
    },
};

static uint32_t _note_hz(uint8_t note)
{
    uint32_t hz = s_semitone_hz[note % 12U];
    const int octave = (int)(note / 12U) - 5;
    if (octave >= 0)
    {
        hz <<= (unsigned)octave;
    }
    else
    {
        hz >>= (unsigned)(-octave);
    }
    return hz == 0U ? 1U : hz;
}

static uint32_t _phase_increment(uint32_t hz)
{
    return (hz << 16) / FLYBIRDS_SYNTH_SAMPLE_RATE;
}

static int32_t _pulse(uint32_t phase, uint32_t duty)
{
    return (phase & 0xFFFFU) < duty ? 32767 : -32767;
}

static int32_t _triangle(uint32_t phase)
{
    const int32_t point = (int32_t)(phase & 0xFFFFU);
    return point < 32768 ? point * 2 - 32768 : 98304 - point * 2;
}

static int32_t _noise(uint16_t *state, uint16_t *divider)
{
    if ((*divider & 1U) == 0U)
    {
        const uint16_t bit = (uint16_t)((*state ^ (*state >> 1)) & 1U);
        *state = (uint16_t)((*state >> 1) | (bit << 14));
    }
    ++(*divider);
    return (*state & 1U) != 0U ? 32767 : -32767;
}

static void _synth_apply_step(flybirds_synth_t *synth, uint32_t step)
{
    const uint8_t lead = s_lead_melody[step];
    synth->lead_inc = lead == 0U ? 0U : _phase_increment(_note_hz(lead));

    const uint8_t root = s_chord_roots[step / 8U];
    const uint8_t arp = (uint8_t)(root + s_arp_pattern[step % 8U]);
    synth->arp_inc = _phase_increment(_note_hz(arp));

    const uint32_t beat = step % 8U;
    synth->bass_inc = (beat == 0U || beat == 4U) ?
                      _phase_increment(_note_hz((uint8_t)(root - 12U))) : 0U;
}

static int32_t _synth_music(flybirds_synth_t *synth, uint32_t step,
                            uint32_t position)
{
    int32_t mix = 0;
    const int32_t lead_gate = (int32_t)(FLYBIRDS_SYNTH_STEP_FRAMES *
                                        FLYBIRDS_GATE_LEAD / 100U);
    if (synth->lead_inc != 0U && (int32_t)position < lead_gate)
    {
        const int32_t amp = FLYBIRDS_LEAD_VOLUME *
                            (lead_gate - (int32_t)position) / lead_gate;
        mix += _pulse(synth->lead_phase, FLYBIRDS_PULSE_DUTY_LEAD) *
               amp / FLYBIRDS_MUSIC_SCALE;
    }

    const int32_t arp_gate = (int32_t)(FLYBIRDS_SYNTH_STEP_FRAMES *
                                       FLYBIRDS_GATE_ARP / 100U);
    if (synth->arp_inc != 0U && (int32_t)position < arp_gate)
    {
        const int32_t amp = FLYBIRDS_ARP_VOLUME *
                            (arp_gate - (int32_t)position) / arp_gate;
        mix += _pulse(synth->arp_phase, FLYBIRDS_PULSE_DUTY_SFX) *
               amp / FLYBIRDS_MUSIC_SCALE;
    }

    const int32_t bass_gate = (int32_t)(FLYBIRDS_SYNTH_STEP_FRAMES *
                                        FLYBIRDS_GATE_BASS / 100U);
    if (synth->bass_inc != 0U && (int32_t)position < bass_gate)
    {
        const int32_t amp = FLYBIRDS_BASS_VOLUME *
                            (bass_gate - (int32_t)position) / bass_gate;
        mix += _triangle(synth->bass_phase) * amp / FLYBIRDS_MUSIC_SCALE;
    }

    const uint32_t beat = step % 8U;
    const int32_t hat_gate = (int32_t)(FLYBIRDS_SYNTH_STEP_FRAMES *
                                       FLYBIRDS_GATE_HAT / 100U);
    if ((beat & 1U) != 0U && (int32_t)position < hat_gate)
    {
        const int32_t amp = FLYBIRDS_HAT_VOLUME *
                            (hat_gate - (int32_t)position) / hat_gate;
        mix += _noise(&synth->noise, &synth->noise_divider) *
               amp / FLYBIRDS_MUSIC_SCALE;
    }
    return mix;
}

static const flybirds_sfx_segment_t *_synth_sfx_table(uint8_t kind,
        size_t *count)
{
    switch (kind)
    {
    case (uint8_t)FLYBIRDS_SFX_FLAP + 1U:
        *count = sizeof(s_sfx_flap) / sizeof(s_sfx_flap[0]);
        return s_sfx_flap;
    case (uint8_t)FLYBIRDS_SFX_SCORE + 1U:
        *count = sizeof(s_sfx_score) / sizeof(s_sfx_score[0]);
        return s_sfx_score;
    case (uint8_t)FLYBIRDS_SFX_HIT + 1U:
        *count = sizeof(s_sfx_hit) / sizeof(s_sfx_hit[0]);
        return s_sfx_hit;
    case (uint8_t)FLYBIRDS_SFX_OVER + 1U:
        *count = sizeof(s_sfx_over) / sizeof(s_sfx_over[0]);
        return s_sfx_over;
    default:
        *count = 0U;
        return NULL;
    }
}

static int32_t _synth_effect(flybirds_synth_t *synth)
{
    if (!synth->sfx_enabled || synth->sfx_kind == 0U)
    {
        return 0;
    }
    size_t count = 0U;
    const flybirds_sfx_segment_t *segments =
        _synth_sfx_table(synth->sfx_kind, &count);
    uint32_t frame = synth->sfx_frame;
    size_t index = 0U;
    while (index < count && frame >= segments[index].frames)
    {
        frame -= segments[index].frames;
        ++index;
    }
    if (index >= count)
    {
        synth->sfx_kind = 0U;
        return 0;
    }
    const flybirds_sfx_segment_t *segment = &segments[index];
    const int32_t span = (int32_t)segment->hz_end -
                         (int32_t)segment->hz_start;
    const int32_t hz = segment->frames <= 1U ? (int32_t)segment->hz_end :
                       (int32_t)segment->hz_start +
                       span * (int32_t)frame /
                       (int32_t)(segment->frames - 1U);
    synth->sfx_phase += _phase_increment((uint32_t)hz);
    const int32_t amplitude = (int32_t)segment->volume *
                              (int32_t)(segment->frames - frame) /
                              (int32_t)segment->frames;
    const int32_t wave = segment->wave == 1U ?
                         _noise(&synth->sfx_noise,
                                &synth->sfx_noise_divider) :
                         _pulse(synth->sfx_phase, FLYBIRDS_PULSE_DUTY_SFX);
    ++synth->sfx_frame;
    return wave * amplitude / FLYBIRDS_SFX_DIVISOR;
}

void flybirds_synth_init(flybirds_synth_t *synth)
{
    if (synth == NULL)
    {
        return;
    }
    memset(synth, 0, sizeof(*synth));
    synth->music_enabled = true;
    synth->sfx_enabled = true;
    synth->step = UINT32_MAX;
    synth->noise = FLYBIRDS_NOISE_SEED;
    synth->sfx_noise = FLYBIRDS_SFX_NOISE_SEED;
}

void flybirds_synth_set_music(flybirds_synth_t *synth, bool enabled)
{
    if (synth != NULL)
    {
        synth->music_enabled = enabled;
    }
}

void flybirds_synth_set_sfx(flybirds_synth_t *synth, bool enabled)
{
    if (synth != NULL)
    {
        synth->sfx_enabled = enabled;
        if (!enabled)
        {
            synth->sfx_kind = 0U;
        }
    }
}

bool flybirds_synth_music_enabled(const flybirds_synth_t *synth)
{
    return synth != NULL && synth->music_enabled;
}

bool flybirds_synth_sfx_enabled(const flybirds_synth_t *synth)
{
    return synth != NULL && synth->sfx_enabled;
}

void flybirds_synth_trigger(flybirds_synth_t *synth, flybirds_sfx_t sfx)
{
    if (synth == NULL || !synth->sfx_enabled ||
            (unsigned)sfx >= (unsigned)FLYBIRDS_SFX_COUNT)
    {
        return;
    }
    synth->sfx_kind = (uint8_t)sfx + 1U;
    synth->sfx_frame = 0U;
    synth->sfx_phase = 0U;
    synth->sfx_noise = FLYBIRDS_SFX_NOISE_SEED;
    synth->sfx_noise_divider = 0U;
}

void flybirds_synth_render(flybirds_synth_t *synth, int16_t *interleaved,
                           size_t frames)
{
    if (interleaved == NULL || frames == 0U)
    {
        return;
    }
    if (synth == NULL)
    {
        memset(interleaved, 0, frames * FLYBIRDS_SYNTH_CHANNELS *
               sizeof(*interleaved));
        return;
    }
    if (!synth->music_enabled && (!synth->sfx_enabled || synth->sfx_kind == 0U))
    {
        memset(interleaved, 0, frames * FLYBIRDS_SYNTH_CHANNELS * sizeof(*interleaved));
        while (frames != 0U)
        {
            const uint32_t loop = synth->frame % FLYBIRDS_SYNTH_LOOP_FRAMES;
            const uint32_t step = loop / FLYBIRDS_SYNTH_STEP_FRAMES;
            if (step != synth->step)
            {
                _synth_apply_step(synth, step);
                synth->step = step;
            }
            uint32_t chunk = FLYBIRDS_SYNTH_STEP_FRAMES -
                             loop % FLYBIRDS_SYNTH_STEP_FRAMES;
            const uint64_t until_wrap = UINT64_C(0x100000000) - synth->frame;
            if (until_wrap < chunk)
            {
                chunk = (uint32_t)until_wrap;
            }
            if (frames < chunk)
            {
                chunk = (uint32_t)frames;
            }
            synth->lead_phase += synth->lead_inc * chunk;
            synth->arp_phase += synth->arp_inc * chunk;
            synth->bass_phase += synth->bass_inc * chunk;
            synth->frame += chunk;
            frames -= chunk;
        }
        return;
    }
    for (size_t index = 0U; index < frames; ++index)
    {
        const uint32_t loop = synth->frame % FLYBIRDS_SYNTH_LOOP_FRAMES;
        const uint32_t step = loop / FLYBIRDS_SYNTH_STEP_FRAMES;
        const uint32_t position = loop % FLYBIRDS_SYNTH_STEP_FRAMES;
        if (step != synth->step)
        {
            _synth_apply_step(synth, step);
            synth->step = step;
        }

        int32_t mix = 0;
        if (synth->music_enabled)
        {
            mix += _synth_music(synth, step, position);
        }
        mix += _synth_effect(synth);
        if (mix > 32767)
        {
            mix = 32767;
        }
        else if (mix < -32768)
        {
            mix = -32768;
        }
        interleaved[index * FLYBIRDS_SYNTH_CHANNELS] = (int16_t)mix;
        interleaved[index * FLYBIRDS_SYNTH_CHANNELS + 1U] = (int16_t)mix;

        synth->lead_phase += synth->lead_inc;
        synth->arp_phase += synth->arp_inc;
        synth->bass_phase += synth->bass_inc;
        ++synth->frame;
    }
}

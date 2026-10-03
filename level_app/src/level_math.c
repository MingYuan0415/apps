/**
 * @brief  Hardware-independent bubble-level math.
 * @note   Compiled on the host by layers/apps/tests/host/level_math_test.c.
 */

#include "level_math.h"

#include <math.h>

void level_math_raw_angles(const level_math_vector_t *accel,
                           float *out_roll, float *out_pitch)
{
    *out_roll = atan2f(accel->y, accel->z) * 57.2957795F;
    *out_pitch = atan2f(-accel->x,
                        sqrtf(accel->y * accel->y + accel->z * accel->z)) *
                 57.2957795F;
}

level_math_result_t level_math_compute(const level_math_vector_t *accel,
                                       const level_math_config_t *config,
                                       level_math_filter_t *filter,
                                       float roll_offset,
                                       float pitch_offset)
{
    level_math_result_t result = {0};
    float raw_roll = 0.0F;
    float raw_pitch = 0.0F;
    level_math_raw_angles(accel, &raw_roll, &raw_pitch);

    if (!filter->valid)
    {
        filter->roll = raw_roll;
        filter->pitch = raw_pitch;
        filter->valid = true;
    }
    else
    {
        filter->roll += (raw_roll - filter->roll) * config->filter_alpha;
        filter->pitch += (raw_pitch - filter->pitch) * config->filter_alpha;
    }

    result.roll_deg = filter->roll - roll_offset;
    result.pitch_deg = filter->pitch - pitch_offset;
    result.magnitude_deg = fmaxf(fabsf(result.roll_deg),
                                 fabsf(result.pitch_deg));
    result.level = result.magnitude_deg < config->level_degrees;

    const float clamp = config->max_degrees;
    float bx = result.roll_deg;
    float by = result.pitch_deg;
    /* Radial clamp keeps the bubble on the circular board instead of letting
     * a combined tilt park it in a square corner outside the ring. */
    const float tilt = sqrtf(bx * bx + by * by);
    if (tilt > clamp)
    {
        bx *= clamp / tilt;
        by *= clamp / tilt;
    }
    const float scale =
        (float)(config->board_size - config->bubble_size - 8) / 2.0F / clamp;
    const int32_t center = config->board_size / 2 - config->bubble_size / 2;
    result.bubble_x = center + (int32_t)(bx * scale);
    result.bubble_y = center + (int32_t)(by * scale);
    return result;
}
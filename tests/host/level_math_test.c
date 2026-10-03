/**
 * @brief  Host test for hardware-independent level math.
 * @note   Compiles only level_math.c; no LVGL or ESP-IDF shims are involved.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "level_math.h"

#define LEVEL_MATH_EPS 1e-3F

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < LEVEL_MATH_EPS;
}

static void test_raw_angles(void)
{
    level_math_vector_t accel = { .x = 0.0F, .y = 0.0F, .z = 9.81F };
    float roll = 1.0F;
    float pitch = 1.0F;
    level_math_raw_angles(&accel, &roll, &pitch);
    assert(near(roll, 0.0F));
    assert(near(pitch, 0.0F));

    accel = (level_math_vector_t)
    {
        .x = 0.0F, .y = 9.81F, .z = 0.0F
    };
    level_math_raw_angles(&accel, &roll, &pitch);
    assert(near(roll, 90.0F));
    assert(near(pitch, 0.0F));

    /* Positive X tilts the pitch negative. */
    accel = (level_math_vector_t)
    {
        .x = 2.0F, .y = 0.0F, .z = 9.81F
    };
    level_math_raw_angles(&accel, &roll, &pitch);
    assert(pitch < 0.0F);
}

static void test_first_sample_seeds_and_level(void)
{
    level_math_config_t config = LEVEL_MATH_DEFAULT_CONFIG();
    level_math_filter_t filter = {0};
    const level_math_vector_t flat = { .x = 0.0F, .y = 0.0F, .z = 9.81F };

    const level_math_result_t first =
        level_math_compute(&flat, &config, &filter, 0.0F, 0.0F);
    assert(near(first.roll_deg, 0.0F));
    assert(near(first.pitch_deg, 0.0F));
    assert(first.level == true);
    /* center = board/2 - bubble/2 = 68 */
    assert(first.bubble_x == 68);
    assert(first.bubble_y == 68);
}

static void test_smoothing(void)
{
    level_math_config_t config = LEVEL_MATH_DEFAULT_CONFIG();
    level_math_filter_t filter = {0};
    const level_math_vector_t flat = { .x = 0.0F, .y = 0.0F, .z = 9.81F };
    const level_math_vector_t tilted = { .x = 0.0F, .y = 2.0F, .z = 9.81F };

    (void)level_math_compute(&flat, &config, &filter, 0.0F, 0.0F);
    float raw_roll = 0.0F;
    float raw_pitch = 0.0F;
    level_math_raw_angles(&tilted, &raw_roll, &raw_pitch);

    const level_math_result_t smoothed =
        level_math_compute(&tilted, &config, &filter, 0.0F, 0.0F);
    assert(near(smoothed.roll_deg, raw_roll * config.filter_alpha));
    assert(smoothed.level == false);
}

static void test_calibration_offset(void)
{
    level_math_config_t config = LEVEL_MATH_DEFAULT_CONFIG();
    level_math_filter_t filter = {0};
    const level_math_vector_t tilted = { .x = 0.0F, .y = 2.0F, .z = 9.81F };
    float raw_roll = 0.0F;
    float raw_pitch = 0.0F;
    level_math_raw_angles(&tilted, &raw_roll, &raw_pitch);

    /* Offsetting by the current attitude reports a level board. */
    const level_math_result_t level =
        level_math_compute(&tilted, &config, &filter, raw_roll, raw_pitch);
    assert(near(level.roll_deg, 0.0F));
    assert(near(level.pitch_deg, 0.0F));
    assert(level.level == true);
}

static void test_radial_clamp(void)
{
    level_math_config_t config = LEVEL_MATH_DEFAULT_CONFIG();
    level_math_filter_t filter = {0};
    const level_math_vector_t steep = { .x = 0.0F, .y = 9.81F, .z = 0.0F };

    const level_math_result_t clamped =
        level_math_compute(&steep, &config, &filter, 0.0F, 0.0F);
    assert(clamped.level == false);
    /* scale = (160 - 24 - 8) / 2 / 15; clamped roll maps to +64 px. */
    assert(clamped.bubble_x == 132);
    assert(clamped.bubble_y == 68);
    assert(clamped.bubble_x <= config.board_size - config.bubble_size);
}

int main(void)
{
    test_raw_angles();
    test_first_sample_seeds_and_level();
    test_smoothing();
    test_calibration_offset();
    test_radial_clamp();
    printf("level math: ok\n");
    return 0;
}
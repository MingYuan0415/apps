#ifndef __LEVEL_MATH_H__
#define __LEVEL_MATH_H__

/*
 * Hardware-independent bubble-level math. This module depends only on the C
 * standard library so it can be compiled and tested on the host without LVGL,
 * ESP-IDF, or FreeRTOS. The page owns rendering and persistence; this module
 * only converts an acceleration vector into filtered angles and bubble
 * coordinates.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Acceleration vector in metres per second squared. */
typedef struct level_math_vector
{
    float x; /**< X axis acceleration. */
    float y; /**< Y axis acceleration. */
    float z; /**< Z axis acceleration. */
} level_math_vector_t;

/** @brief Exponential filter state carried between samples. */
typedef struct level_math_filter
{
    float roll;   /**< Filtered roll in degrees. */
    float pitch;  /**< Filtered pitch in degrees. */
    bool valid;   /**< Whether the filter has a first sample. */
} level_math_filter_t;

/** @brief Bubble-level tuning parameters. */
typedef struct level_math_config
{
    float filter_alpha; /**< Smoothing factor in (0, 1]; first sample seeds. */
    float max_degrees;  /**< Radial clamp for the bubble offset. */
    float level_degrees; /**< Magnitude threshold considered level. */
    int32_t board_size; /**< Square board edge in pixels. */
    int32_t bubble_size; /**< Bubble diameter in pixels. */
} level_math_config_t;

/** @brief Filtered angles and rendered bubble position. */
typedef struct level_math_result
{
    float roll_deg;      /**< Roll after offset, in degrees. */
    float pitch_deg;     /**< Pitch after offset, in degrees. */
    float magnitude_deg; /**< max(|roll|, |pitch|) in degrees. */
    bool level;          /**< Whether magnitude is below the threshold. */
    int32_t bubble_x;    /**< Bubble X position within the board. */
    int32_t bubble_y;    /**< Bubble Y position within the board. */
} level_math_result_t;

/** @brief Convenience tuning for host tests and other callers; the level page
 *         builds an equivalent config from its layout constants. */
#define LEVEL_MATH_DEFAULT_CONFIG() \
    { .filter_alpha = 0.2F, .max_degrees = 15.0F, .level_degrees = 1.5F, \
      .board_size = 160, .bubble_size = 24 }

/**
 * @brief Convert acceleration into raw roll and pitch angles.
 *
 * @param accel is the sampled acceleration vector; must not be NULL.
 * @param out_roll receives the raw roll in degrees; must not be NULL.
 * @param out_pitch receives the raw pitch in degrees; must not be NULL.
 */
void level_math_raw_angles(const level_math_vector_t *accel,
                           float *out_roll, float *out_pitch);

/**
 * @brief Update the filter and compute the bubble result.
 *
 * The first sample seeds the filter directly; later samples are smoothed with
 * config->filter_alpha. The bubble offset is radially clamped so a combined
 * tilt stays on the circular board.
 *
 * @param accel is the sampled acceleration vector; must not be NULL.
 * @param config is the tuning parameter set; must not be NULL.
 * @param filter is the carried filter state; must not be NULL.
 * @param roll_offset is the calibration offset in degrees.
 * @param pitch_offset is the calibration offset in degrees.
 *
 * @return The filtered angles, level flag, and bubble coordinates.
 */
level_math_result_t level_math_compute(const level_math_vector_t *accel,
                                       const level_math_config_t *config,
                                       level_math_filter_t *filter,
                                       float roll_offset,
                                       float pitch_offset);

#ifdef __cplusplus
}
#endif

#endif /* __LEVEL_MATH_H__ */
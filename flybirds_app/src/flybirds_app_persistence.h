/**
 * @brief Flybirds preferences persisted in NVS as one versioned blob.
 */

#ifndef __FLYBIRDS_APP_PERSISTENCE_H__
#define __FLYBIRDS_APP_PERSISTENCE_H__

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Highest score the game accepts from storage; keeps the label readable. */
#define FLYBIRDS_BEST_SCORE_LIMIT 9999U

/** Preferences owned by the game and restored across mounts. */
typedef struct flybirds_preferences
{
    uint16_t best_score; /**< Highest score ever recorded. */
    bool music;          /**< Background music preference. */
    bool sfx;            /**< Effect preference. */
} flybirds_preferences_t;

/**
 * @brief Fill in the first-run defaults.
 * @param preferences receives zero score with both audio buses enabled.
 */
void flybirds_app_persistence_defaults(flybirds_preferences_t *preferences);

/**
 * @brief Load the stored preferences, falling back to defaults.
 *
 * Absent, wrongly sized, stale-version, or out-of-range blobs all yield the
 * defaults with ESP_OK; only a real NVS error is returned to the caller.
 *
 * @param preferences receives the restored preferences.
 * @return ESP_OK on success, otherwise the storage error.
 */
esp_err_t flybirds_app_persistence_load(flybirds_preferences_t *preferences);

/**
 * @brief Persist the complete preference set.
 * @param preferences is the state to write; NULL is rejected.
 * @return ESP_OK on success, otherwise the storage error.
 */
esp_err_t flybirds_app_persistence_save(
    const flybirds_preferences_t *preferences);

/**
 * @brief Erase the stored preferences; an absent key counts as success.
 * @return ESP_OK on success, otherwise the storage error.
 */
esp_err_t flybirds_app_persistence_reset(void);

#endif /* __FLYBIRDS_APP_PERSISTENCE_H__ */

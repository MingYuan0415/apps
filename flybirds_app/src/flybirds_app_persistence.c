#include "flybirds_app_persistence.h"

#include "nv_storage.h"
#include "nvs.h"

#include <stddef.h>
#include <stdint.h>

#define FLYBIRDS_PREFERENCE_KEY     "flybirds_pref"
#define FLYBIRDS_PREFERENCE_VERSION 1U

/** On-flash layout: version, high score, two boolean flags, reserved tail. */
typedef struct flybirds_preference_blob
{
    uint32_t version;
    uint16_t best_score;
    uint8_t music;
    uint8_t sfx;
    uint32_t reserved;
} flybirds_preference_blob_t;

void flybirds_app_persistence_defaults(flybirds_preferences_t *preferences)
{
    if (preferences == NULL)
    {
        return;
    }
    preferences->best_score = 0U;
    preferences->music = true;
    preferences->sfx = true;
}

esp_err_t flybirds_app_persistence_load(flybirds_preferences_t *preferences)
{
    if (preferences == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    flybirds_app_persistence_defaults(preferences);
    flybirds_preference_blob_t blob;
    size_t size = sizeof(blob);
    const esp_err_t result = nv_storage_get_blob(FLYBIRDS_PREFERENCE_KEY,
        &blob, &size);

    if (result == ESP_ERR_NVS_NOT_FOUND || result == ESP_ERR_NVS_INVALID_LENGTH)
    {
        return ESP_OK;
    }
    if (result != ESP_OK)
    {
        return result;
    }
    if (size != sizeof(blob) || blob.version != FLYBIRDS_PREFERENCE_VERSION ||
            blob.best_score > FLYBIRDS_BEST_SCORE_LIMIT ||
            blob.music > 1U || blob.sfx > 1U || blob.reserved != 0U)
    {
        return ESP_OK;
    }
    preferences->best_score = blob.best_score;
    preferences->music = blob.music != 0U;
    preferences->sfx = blob.sfx != 0U;
    return ESP_OK;
}

esp_err_t flybirds_app_persistence_save(
    const flybirds_preferences_t *preferences)
{
    if (preferences == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const flybirds_preference_blob_t blob =
    {
        .version = FLYBIRDS_PREFERENCE_VERSION,
        .best_score = preferences->best_score > FLYBIRDS_BEST_SCORE_LIMIT ?
        FLYBIRDS_BEST_SCORE_LIMIT : preferences->best_score,
        .music = preferences->music ? 1U : 0U,
        .sfx = preferences->sfx ? 1U : 0U,
        .reserved = 0U,
    };

    return nv_storage_set_blob(FLYBIRDS_PREFERENCE_KEY, &blob, sizeof(blob));
}

esp_err_t flybirds_app_persistence_reset(void)
{
    const esp_err_t result = nv_storage_erase_key(FLYBIRDS_PREFERENCE_KEY);

    return result == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : result;
}

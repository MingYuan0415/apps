#define DBG_TAG "level_persistence"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "level_app_persistence.h"

#include "nv_storage.h"
#include "nvs.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define LEVEL_CALIBRATION_KEY     "level_cal"
#define LEVEL_CALIBRATION_VERSION 1U

typedef struct level_calibration_blob
{
    uint32_t version;
    float roll_offset;
    float pitch_offset;
} level_calibration_blob_t;

esp_err_t level_app_persistence_load(float *roll_offset,
                                     float *pitch_offset)
{
    if (roll_offset == NULL || pitch_offset == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *roll_offset = 0.0F;
    *pitch_offset = 0.0F;
    level_calibration_blob_t blob;
    size_t size = sizeof(blob);
    const esp_err_t result = nv_storage_get_blob(LEVEL_CALIBRATION_KEY, &blob,
                             &size);

    if (result == ESP_ERR_NVS_NOT_FOUND)
    {
        return ESP_OK;
    }
    if (result != ESP_OK)
    {
        return result;
    }
    if (size == sizeof(blob) && blob.version == LEVEL_CALIBRATION_VERSION &&
            isfinite(blob.roll_offset) && isfinite(blob.pitch_offset) &&
            fabsf(blob.roll_offset) <= 180.0F &&
            fabsf(blob.pitch_offset) <= 180.0F)
    {
        *roll_offset = blob.roll_offset;
        *pitch_offset = blob.pitch_offset;
    }
    return ESP_OK;
}

esp_err_t level_app_persistence_save(float roll_offset, float pitch_offset)
{
    const level_calibration_blob_t blob =
    {
        .version = LEVEL_CALIBRATION_VERSION,
        .roll_offset = roll_offset,
        .pitch_offset = pitch_offset,
    };

    return nv_storage_set_blob(LEVEL_CALIBRATION_KEY, &blob, sizeof(blob));
}

esp_err_t level_app_persistence_reset(void)
{
    const esp_err_t result = nv_storage_erase_key(LEVEL_CALIBRATION_KEY);

    return result == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : result;
}

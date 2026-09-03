#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "apps_persistence.h"
#include "level_app_persistence.h"
#include "nv_storage.h"
#include "nvs.h"

static uint8_t s_blob[32];
static size_t s_blob_size;
static bool s_blob_present;
static esp_err_t s_next_get_result;
static esp_err_t s_next_erase_result;

esp_err_t nv_storage_set_blob(const char *key, const void *data, size_t length)
{
    assert(strcmp(key, "level_cal") == 0);
    assert(data != NULL);
    assert(length <= sizeof(s_blob));
    memcpy(s_blob, data, length);
    s_blob_size = length;
    s_blob_present = true;
    return ESP_OK;
}

esp_err_t nv_storage_get_blob(const char *key, void *output, size_t *size)
{
    assert(strcmp(key, "level_cal") == 0);
    assert(output != NULL);
    assert(size != NULL);
    if (s_next_get_result != ESP_OK)
    {
        const esp_err_t result = s_next_get_result;

        s_next_get_result = ESP_OK;
        return result;
    }
    if (!s_blob_present)
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    assert(*size >= s_blob_size);
    memcpy(output, s_blob, s_blob_size);
    *size = s_blob_size;
    return ESP_OK;
}

esp_err_t nv_storage_erase_key(const char *key)
{
    assert(strcmp(key, "level_cal") == 0);
    if (s_next_erase_result != ESP_OK)
    {
        const esp_err_t result = s_next_erase_result;

        s_next_erase_result = ESP_OK;
        return result;
    }
    if (!s_blob_present)
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    s_blob_present = false;
    s_blob_size = 0U;
    return ESP_OK;
}

static void _test_reset(void)
{
    memset(s_blob, 0, sizeof(s_blob));
    s_blob_size = 0U;
    s_blob_present = false;
    s_next_get_result = ESP_OK;
    s_next_erase_result = ESP_OK;
}

static void _test_absent_calibration_uses_zero(void)
{
    _test_reset();
    float roll_offset = 1.0F;
    float pitch_offset = 1.0F;

    assert(level_app_persistence_load(&roll_offset, &pitch_offset) == ESP_OK);
    assert(roll_offset == 0.0F);
    assert(pitch_offset == 0.0F);
    assert(apps_factory_reset_persisted_state() == ESP_OK);
}

static void _test_save_load_and_factory_reset(void)
{
    _test_reset();
    assert(level_app_persistence_save(1.25F, -2.5F) == ESP_OK);
    float roll_offset = 0.0F;
    float pitch_offset = 0.0F;

    assert(level_app_persistence_load(&roll_offset, &pitch_offset) == ESP_OK);
    assert(roll_offset == 1.25F);
    assert(pitch_offset == -2.5F);
    assert(apps_factory_reset_persisted_state() == ESP_OK);
    assert(!s_blob_present);
    assert(apps_factory_reset_persisted_state() == ESP_OK);
}

static void _test_storage_errors_propagate(void)
{
    _test_reset();
    float roll_offset = 0.0F;
    float pitch_offset = 0.0F;

    s_next_get_result = ESP_FAIL;
    assert(level_app_persistence_load(&roll_offset, &pitch_offset) ==
           ESP_FAIL);
    s_next_erase_result = ESP_FAIL;
    assert(apps_factory_reset_persisted_state() == ESP_FAIL);
}

int main(void)
{
    _test_absent_calibration_uses_zero();
    _test_save_load_and_factory_reset();
    _test_storage_errors_propagate();
    return 0;
}

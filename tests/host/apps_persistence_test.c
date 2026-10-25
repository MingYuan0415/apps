#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "apps_persistence.h"
#include "flybirds_app_persistence.h"
#include "level_app_persistence.h"
#include "nv_storage.h"
#include "nvs.h"

#define TEST_KEY_CAPACITY   16U
#define TEST_BLOB_CAPACITY  32U
#define TEST_ENTRY_CAPACITY 2U

typedef struct test_entry
{
    char key[TEST_KEY_CAPACITY];
    uint8_t data[TEST_BLOB_CAPACITY];
    size_t size;
    bool present;
} test_entry_t;

static test_entry_t s_entries[TEST_ENTRY_CAPACITY];
static esp_err_t s_next_get_result;
static esp_err_t s_next_erase_result;

static test_entry_t *_find_entry(const char *key)
{
    for (size_t index = 0U; index < TEST_ENTRY_CAPACITY; ++index)
    {
        if (strcmp(s_entries[index].key, key) == 0)
        {
            return &s_entries[index];
        }
    }
    assert(0);
    return NULL;
}

esp_err_t nv_storage_set_blob(const char *key, const void *data, size_t length)
{
    assert(key != NULL);
    assert(data != NULL);
    assert(length <= TEST_BLOB_CAPACITY);
    test_entry_t *entry = _find_entry(key);
    memcpy(entry->data, data, length);
    entry->size = length;
    entry->present = true;
    return ESP_OK;
}

esp_err_t nv_storage_get_blob(const char *key, void *output, size_t *size)
{
    assert(output != NULL);
    assert(size != NULL);
    if (s_next_get_result != ESP_OK)
    {
        const esp_err_t result = s_next_get_result;

        s_next_get_result = ESP_OK;
        return result;
    }
    test_entry_t *entry = _find_entry(key);
    if (!entry->present)
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*size < entry->size)
    {
        *size = entry->size;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(output, entry->data, entry->size);
    *size = entry->size;
    return ESP_OK;
}

esp_err_t nv_storage_erase_key(const char *key)
{
    if (s_next_erase_result != ESP_OK)
    {
        const esp_err_t result = s_next_erase_result;

        s_next_erase_result = ESP_OK;
        return result;
    }
    test_entry_t *entry = _find_entry(key);
    if (!entry->present)
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    entry->present = false;
    entry->size = 0U;
    return ESP_OK;
}

static void _test_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    memcpy(s_entries[0].key, "level_cal", sizeof("level_cal"));
    memcpy(s_entries[1].key, "flybirds_pref", sizeof("flybirds_pref"));
    s_next_get_result = ESP_OK;
    s_next_erase_result = ESP_OK;
}

static void _test_absent_values_use_defaults(void)
{
    _test_reset();
    float roll_offset = 1.0F;
    float pitch_offset = 1.0F;
    assert(level_app_persistence_load(&roll_offset, &pitch_offset) == ESP_OK);
    assert(roll_offset == 0.0F);
    assert(pitch_offset == 0.0F);

    flybirds_preferences_t preferences;
    memset(&preferences, 0xAA, sizeof(preferences));
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 0U);
    assert(preferences.music);
    assert(preferences.sfx);
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

    flybirds_preferences_t preferences =
    {
        .best_score = 42U,
        .music = false,
        .sfx = true,
    };
    assert(flybirds_app_persistence_save(&preferences) == ESP_OK);
    memset(&preferences, 0, sizeof(preferences));
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 42U);
    assert(!preferences.music);
    assert(preferences.sfx);

    assert(apps_factory_reset_persisted_state() == ESP_OK);
    assert(!_find_entry("level_cal")->present);
    assert(!_find_entry("flybirds_pref")->present);
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 0U);
    assert(preferences.music);
    assert(preferences.sfx);
    assert(apps_factory_reset_persisted_state() == ESP_OK);
}

static void _test_corrupted_blob_falls_back(void)
{
    _test_reset();
    flybirds_preferences_t preferences =
    {
        .best_score = 7U,
        .music = false,
        .sfx = false,
    };
    assert(flybirds_app_persistence_save(&preferences) == ESP_OK);
    test_entry_t *entry = _find_entry("flybirds_pref");
    assert(entry->size == 12U);
    /* Version lives in the first four bytes; a stale version must be ignored. */
    entry->data[0] = 0x02U;
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 0U);
    assert(preferences.music);
    assert(preferences.sfx);

    assert(flybirds_app_persistence_save(&preferences) == ESP_OK);
    entry = _find_entry("flybirds_pref");
    entry->data[4] = 0xFFU; /* best score high byte: 0xFF07 > limit */
    entry->data[5] = 0xFFU;
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 0U);

    assert(flybirds_app_persistence_save(&preferences) == ESP_OK);
    entry = _find_entry("flybirds_pref");
    entry->data[6] = 3U; /* music flag must be 0 or 1 */
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.music);

    entry->size = 4U;
    assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
    assert(preferences.best_score == 0U);
    assert(preferences.music);
    assert(preferences.sfx);
}

static void _test_oversized_preferences_use_defaults(void)
{
    _test_reset();
    test_entry_t *entry = _find_entry("flybirds_pref");
    entry->present = true;
    const size_t sizes[] = {13U, TEST_BLOB_CAPACITY};
    for (size_t index = 0U; index < sizeof(sizes) / sizeof(sizes[0]); ++index)
    {
        entry->size = sizes[index];
        memset(entry->data, 0xA5, sizeof(entry->data));
        uint8_t output[12];
        memset(output, 0x5A, sizeof(output));
        size_t size = sizeof(output);
        assert(nv_storage_get_blob("flybirds_pref", output, &size) ==
               ESP_ERR_NVS_INVALID_LENGTH);
        assert(size == entry->size);
        for (size_t byte = 0U; byte < sizeof(output); ++byte)
        {
            assert(output[byte] == 0x5A);
        }
        flybirds_preferences_t preferences = {.best_score = 42U};
        assert(flybirds_app_persistence_load(&preferences) == ESP_OK);
        assert(preferences.best_score == 0U && preferences.music && preferences.sfx);
    }
}

static void _test_storage_errors_propagate(void)
{
    _test_reset();
    float roll_offset = 0.0F;
    float pitch_offset = 0.0F;
    s_next_get_result = ESP_FAIL;
    assert(level_app_persistence_load(&roll_offset, &pitch_offset) ==
           ESP_FAIL);

    flybirds_preferences_t preferences;
    s_next_get_result = ESP_FAIL;
    assert(flybirds_app_persistence_load(&preferences) == ESP_FAIL);

    s_next_erase_result = ESP_FAIL;
    assert(flybirds_app_persistence_reset() == ESP_FAIL);

    s_next_erase_result = ESP_FAIL;
    assert(apps_factory_reset_persisted_state() == ESP_FAIL);
}

int main(void)
{
    _test_absent_values_use_defaults();
    _test_save_load_and_factory_reset();
    _test_corrupted_blob_falls_back();
    _test_oversized_preferences_use_defaults();
    _test_storage_errors_propagate();
    return 0;
}

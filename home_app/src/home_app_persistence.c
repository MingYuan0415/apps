#define DBG_TAG "home_persistence"
#include "mt_log.h"

#include "home_app_persistence.h"

#include "nv_storage.h"
#include "nvs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HOME_SHORTCUT_KEY     "home_shortcut"
#define HOME_SHORTCUT_VERSION 2U

typedef struct home_shortcut_blob
{
    uint32_t version;
    uint8_t configured;
    uint8_t count;
    uint8_t reserved[2];
    char app_ids[HOME_SHORTCUT_SLOTS][APP_MANAGER_ID_BYTES];
} home_shortcut_blob_t;

static bool _home_app_id_valid(const char *app_id)
{
    if (app_id == NULL)
    {
        return false;
    }
    for (size_t index = 0U; index < APP_MANAGER_ID_BYTES; ++index)
    {
        if (app_id[index] == '\0')
        {
            return index > 0U;
        }
    }
    return false;
}

esp_err_t home_app_persistence_load(home_shortcut_prefs_t *prefs)
{
    if (prefs == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(prefs, 0, sizeof(*prefs));

    home_shortcut_blob_t blob;
    size_t size = sizeof(blob);
    const esp_err_t result = nv_storage_get_blob(HOME_SHORTCUT_KEY, &blob,
        &size);
    if (result == ESP_ERR_NVS_NOT_FOUND || result == ESP_ERR_NVS_INVALID_LENGTH)
    {
        return ESP_OK;
    }
    if (result != ESP_OK)
    {
        return result;
    }
    if (size != sizeof(blob) || blob.version != HOME_SHORTCUT_VERSION ||
            blob.configured > 1U || blob.count > HOME_SHORTCUT_SLOTS ||
            blob.reserved[0] != 0U || blob.reserved[1] != 0U)
    {
        return ESP_OK;
    }
    if (blob.configured == 0U && blob.count != 0U)
    {
        return ESP_OK;
    }
    for (size_t index = 0U; index < blob.count; ++index)
    {
        if (!_home_app_id_valid(blob.app_ids[index]))
        {
            return ESP_OK;
        }
    }

    prefs->configured = blob.configured != 0U;
    prefs->count = blob.count;
    for (size_t index = 0U; index < blob.count; ++index)
    {
        memcpy(prefs->app_ids[index], blob.app_ids[index],
               APP_MANAGER_ID_BYTES);
    }
    return ESP_OK;
}

esp_err_t home_app_persistence_save(const home_shortcut_prefs_t *prefs)
{
    if (prefs == NULL || prefs->count > HOME_SHORTCUT_SLOTS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    home_shortcut_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = HOME_SHORTCUT_VERSION;
    blob.configured = prefs->configured ? 1U : 0U;
    blob.count = prefs->configured ? prefs->count : 0U;
    for (size_t index = 0U; index < blob.count; ++index)
    {
        if (!_home_app_id_valid(prefs->app_ids[index]))
        {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(blob.app_ids[index], prefs->app_ids[index],
               APP_MANAGER_ID_BYTES);
    }
    return nv_storage_set_blob(HOME_SHORTCUT_KEY, &blob, sizeof(blob));
}

esp_err_t home_app_persistence_reset(void)
{
    const esp_err_t result = nv_storage_erase_key(HOME_SHORTCUT_KEY);

    return result == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : result;
}

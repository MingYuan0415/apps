#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_manager.h"
#include "chore_service.h"
#include "home_app_persistence.h"
#include "home_shortcuts.h"
#include "nv_storage.h"
#include "nvs.h"

#define TEST_BLOB_CAPACITY 128U

#ifdef HOME_SHORTCUTS_TESTING
    void home_shortcuts_test_reset(void);
#endif

typedef struct test_entry
{
    const char *key;
    uint8_t data[TEST_BLOB_CAPACITY];
    size_t size;
    bool present;
} test_entry_t;

static test_entry_t s_entries[] =
{
    { .key = "home_shortcut" },
    { .key = "other" },
};

static const app_manager_app_desc_t s_apps[] =
{
    {
        .name = "home", .id = "home", .root_page_id = "root",
        .flags = APP_MANAGER_APP_FLAG_PINNED, .display_name = "主页",
        .launcher_order = 1U
    },
    {
        .name = "menu", .id = "menu", .root_page_id = "root",
        .display_name = "应用", .launcher_order = 5U
    },
    {
        .name = "weather", .id = "weather", .root_page_id = "root",
        .display_name = "天气", .launcher_order = 10U
    },
    {
        .name = "calculator", .id = "calculator", .root_page_id = "root",
        .display_name = "计算器", .launcher_order = 15U
    },
    {
        .name = "music", .id = "music", .root_page_id = "root",
        .display_name = "音乐", .launcher_order = 18U
    },
    {
        .name = "clock", .id = "clock", .root_page_id = "root",
        .display_name = "时钟", .launcher_order = 20U
    },
    {
        .name = "level", .id = "level", .root_page_id = "root",
        .display_name = "水平仪", .launcher_order = 40U
    },
    {
        .name = "files", .id = "files", .root_page_id = "root",
        .display_name = "文件管理", .launcher_order = 60U
    },
    {
        .name = "setup", .id = "setup", .root_page_id = "root",
        .flags = APP_MANAGER_APP_FLAG_HIDDEN, .display_name = "网络设置",
        .launcher_order = 60U
    },
};

static size_t _entry_count(void)
{
    return sizeof(s_entries) / sizeof(s_entries[0]);
}

static test_entry_t *_find_entry(const char *key)
{
    for (size_t index = 0U; index < _entry_count(); ++index)
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
    test_entry_t *entry = _find_entry(key);
    if (!entry->present)
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    entry->present = false;
    entry->size = 0U;
    return ESP_OK;
}

const app_manager_app_desc_t *app_manager_builtin_list_open(void)
{
    return &s_apps[0];
}

const app_manager_app_desc_t *app_manager_builtin_list_get_next(
    const app_manager_app_desc_t *prev)
{
    if (prev == NULL || prev < &s_apps[0] ||
            prev >= &s_apps[sizeof(s_apps) / sizeof(s_apps[0]) - 1U])
    {
        return NULL;
    }
    return prev + 1;
}

bool chore_service_cancel_pending(
    const chore_service_cancel_token_t *cancel)
{
    (void)cancel;
    return false;
}

esp_err_t chore_service_submit(const chore_service_job_t *job,
                               chore_service_handle_t *handle)
{
    assert(job != NULL);
    assert(job->run != NULL);
    if (handle != NULL)
    {
        handle->slot = 0U;
        handle->generation = 1U;
    }
    if (job->release != NULL)
    {
        job->release(job->arg);
    }
    return ESP_OK;
}

static void _reset_storage(void)
{
    for (size_t index = 0U; index < _entry_count(); ++index)
    {
        s_entries[index].present = false;
        s_entries[index].size = 0U;
        memset(s_entries[index].data, 0, sizeof(s_entries[index].data));
    }
#ifdef HOME_SHORTCUTS_TESTING
    home_shortcuts_test_reset();
#endif
}

static void _test_catalog_and_default(void)
{
    _reset_storage();
    assert(home_shortcuts_ensure_loaded() == ESP_OK);
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    assert(!prefs->configured);
    assert(prefs->count == 3U);
    assert(strcmp(prefs->app_ids[0], "calculator") == 0);
    assert(strcmp(prefs->app_ids[1], "music") == 0);
    assert(strcmp(prefs->app_ids[2], "clock") == 0);

    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    assert(count == 6U);
    assert(strcmp(catalog[0]->id, "weather") == 0);
    assert(strcmp(catalog[1]->id, "calculator") == 0);
    assert(strcmp(catalog[2]->id, "music") == 0);
    assert(strcmp(catalog[3]->id, "clock") == 0);
    assert(strcmp(catalog[4]->id, "level") == 0);
    assert(strcmp(catalog[5]->id, "files") == 0);

    const app_manager_app_desc_t *effective[HOME_SHORTCUT_SLOTS];
    const size_t effective_count = home_shortcut_effective(
                                       prefs, catalog, count, effective,
                                       HOME_SHORTCUT_SLOTS);
    assert(effective_count == 3U);
    assert(strcmp(effective[0]->id, "calculator") == 0);
    assert(strcmp(effective[1]->id, "music") == 0);
    assert(strcmp(effective[2]->id, "clock") == 0);
}

static void _test_selection_limit_and_drop(void)
{
    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    assert(count == 6U);

    /* The loaded defaults occupy all three slots. */
    assert(home_shortcuts_set_selected(catalog[0], true) ==
           ESP_ERR_INVALID_STATE);
    assert(home_shortcuts_set_selected(catalog[1], false) == ESP_OK);
    assert(home_shortcuts_set_selected(catalog[0], true) == ESP_OK);
    assert(home_shortcuts_set_selected(catalog[5], true) ==
           ESP_ERR_INVALID_STATE);

    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    assert(prefs->configured);
    assert(prefs->count == 3U);
    assert(home_shortcuts_is_selected(prefs, "weather"));
    assert(home_shortcuts_is_selected(prefs, "music"));
    assert(home_shortcuts_is_selected(prefs, "clock"));

    const app_manager_app_desc_t *effective[HOME_SHORTCUT_SLOTS];
    assert(home_shortcut_effective(prefs, catalog, count, effective,
                                   HOME_SHORTCUT_SLOTS) == 3U);
    assert(strcmp(effective[0]->id, "weather") == 0);
    assert(strcmp(effective[1]->id, "music") == 0);
    assert(strcmp(effective[2]->id, "clock") == 0);

    assert(home_shortcuts_set_selected(catalog[2], false) == ESP_OK);
    assert(home_shortcuts_set_selected(catalog[5], true) == ESP_OK);
    prefs = home_shortcuts_get();
    assert(prefs->count == 3U);
    assert(!home_shortcuts_is_selected(prefs, "music"));
    assert(home_shortcuts_is_selected(prefs, "files"));
}

static void _test_persistence_and_reset(void)
{
    _reset_storage();
    home_shortcut_prefs_t prefs;
    home_shortcuts_defaults(&prefs);
    prefs.configured = true;
    prefs.count = 2U;
    memcpy(prefs.app_ids[0], "music", sizeof("music"));
    memcpy(prefs.app_ids[1], "clock", sizeof("clock"));
    assert(home_app_persistence_save(&prefs) == ESP_OK);

    home_shortcut_prefs_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    assert(home_app_persistence_load(&loaded) == ESP_OK);
    assert(loaded.configured);
    assert(loaded.count == 2U);
    assert(strcmp(loaded.app_ids[0], "music") == 0);
    assert(strcmp(loaded.app_ids[1], "clock") == 0);

    assert(home_app_persistence_reset() == ESP_OK);
    assert(home_app_persistence_load(&loaded) == ESP_OK);
    assert(!loaded.configured);
    assert(loaded.count == 0U);
}

static void _test_load_normalizes_unavailable_and_duplicate_ids(void)
{
    _reset_storage();
    home_shortcut_prefs_t stored;
    home_shortcuts_defaults(&stored);
    stored.configured = true;
    stored.count = 3U;
    memcpy(stored.app_ids[0], "removed", sizeof("removed"));
    memcpy(stored.app_ids[1], "music", sizeof("music"));
    memcpy(stored.app_ids[2], "music", sizeof("music"));
    assert(home_app_persistence_save(&stored) == ESP_OK);

    assert(home_shortcuts_ensure_loaded() == ESP_OK);
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    assert(prefs->configured);
    assert(prefs->count == 1U);
    assert(strcmp(prefs->app_ids[0], "music") == 0);

    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    assert(count == 6U);
    const home_shortcut_prefs_t *loaded = home_shortcuts_get();
    const app_manager_app_desc_t *clock = catalog[3];
    assert(strcmp(clock->id, "clock") == 0);
    assert(home_shortcuts_set_selected(clock, true) == ESP_OK);
    loaded = home_shortcuts_get();
    assert(loaded->count == 2U);
    assert(home_shortcuts_is_selected(loaded, "music"));
    assert(home_shortcuts_is_selected(loaded, "clock"));
}

int main(void)
{
    _test_catalog_and_default();
    _test_selection_limit_and_drop();
    _test_load_normalizes_unavailable_and_duplicate_ids();
    _test_persistence_and_reset();
    return 0;
}

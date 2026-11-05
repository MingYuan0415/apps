#define DBG_TAG "home_shortcuts"
#include "mt_log.h"

#include "home_shortcuts.h"

#include "home_app_persistence.h"

#include "chore_service.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static home_shortcut_prefs_t s_prefs;
static bool s_loaded;
static uint32_t s_generation;

typedef struct home_shortcut_save_job
{
    home_shortcut_prefs_t prefs;
} home_shortcut_save_job_t;

static void _home_shortcut_save_job_run(
    const chore_service_cancel_token_t *cancel, void *argument)
{
    home_shortcut_save_job_t *job = argument;
    if (job == NULL || chore_service_cancel_pending(cancel))
    {
        return;
    }
    const esp_err_t result = home_app_persistence_save(&job->prefs);
    if (result != ESP_OK)
    {
        LOG_W("shortcut save failed: %s", esp_err_to_name(result));
    }
}

static void _home_shortcut_save_job_release(void *argument)
{
    free(argument);
}

static esp_err_t _home_shortcuts_save_submit(void)
{
    home_shortcut_save_job_t *job = calloc(1U, sizeof(*job));
    if (job == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    job->prefs = s_prefs;
    const chore_service_job_t chore =
    {
        .run = _home_shortcut_save_job_run,
        .release = _home_shortcut_save_job_release,
        .arg = job,
    };
    chore_service_handle_t handle;
    const esp_err_t result = chore_service_submit(&chore, &handle);
    if (result != ESP_OK)
    {
        free(job);
    }
    return result;
}

static uint16_t _home_shortcut_order(const app_manager_app_desc_t *app)
{
    return app->launcher_order == 0U ? UINT16_MAX : app->launcher_order;
}

static bool _home_shortcut_eligible(const app_manager_app_desc_t *app)
{
    if (app == NULL || app->id == NULL ||
            (app->flags & APP_MANAGER_APP_FLAG_HIDDEN) != 0U)
    {
        return false;
    }
    return strcmp(app->id, APP_MANAGER_ID_HOME) != 0 &&
           strcmp(app->id, APP_MANAGER_ID_MENU) != 0;
}

static bool _home_shortcut_catalog_has_id(
    const app_manager_app_desc_t *const *catalog, size_t catalog_count,
    const char *app_id)
{
    for (size_t index = 0U; index < catalog_count; ++index)
    {
        if (catalog[index] != NULL && catalog[index]->id != NULL &&
                strcmp(catalog[index]->id, app_id) == 0)
        {
            return true;
        }
    }
    return false;
}

static void _home_shortcuts_normalize_loaded(
    const app_manager_app_desc_t *const *catalog, size_t catalog_count)
{
    home_shortcut_prefs_t normalized;
    home_shortcuts_defaults(&normalized);
    normalized.configured = true;

    for (size_t index = 0U; index < s_prefs.count; ++index)
    {
        const char *app_id = s_prefs.app_ids[index];
        bool duplicate = false;
        for (size_t selected = 0U; selected < normalized.count; ++selected)
        {
            if (strcmp(normalized.app_ids[selected], app_id) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate ||
                !_home_shortcut_catalog_has_id(catalog, catalog_count, app_id))
        {
            continue;
        }
        memcpy(normalized.app_ids[normalized.count], app_id,
               APP_MANAGER_ID_BYTES);
        ++normalized.count;
    }
    s_prefs = normalized;
}

void home_shortcuts_defaults(home_shortcut_prefs_t *prefs)
{
    if (prefs == NULL)
    {
        return;
    }
    memset(prefs, 0, sizeof(*prefs));
}

#ifdef HOME_SHORTCUTS_TESTING
void home_shortcuts_test_reset(void)
{
    home_shortcuts_defaults(&s_prefs);
    s_loaded = false;
    s_generation = 0U;
}
#endif

esp_err_t home_shortcuts_ensure_loaded(void)
{
    if (s_loaded)
    {
        return ESP_OK;
    }
    s_loaded = true;
    home_shortcuts_defaults(&s_prefs);
    const esp_err_t result = home_app_persistence_load(&s_prefs);
    if (result != ESP_OK)
    {
        LOG_W("shortcut load failed: %s", esp_err_to_name(result));
        home_shortcuts_defaults(&s_prefs);
    }
    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t catalog_count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    if (!s_prefs.configured)
    {
        const app_manager_app_desc_t *effective[HOME_SHORTCUT_SLOTS];
        const size_t effective_count = home_shortcut_effective(&s_prefs,
            catalog, catalog_count, effective,
            HOME_SHORTCUT_SLOTS);
        s_prefs.count = (uint8_t)effective_count;
        for (size_t index = 0U; index < effective_count; ++index)
        {
            const size_t length = strlen(effective[index]->id) + 1U;
            memcpy(s_prefs.app_ids[index], effective[index]->id, length);
        }
    }
    else
    {
        /* A product update can remove Apps and older stores can contain
         * duplicates. Normalize once so the picker count and the add limit
         * describe the same set of selectable entries. */
        _home_shortcuts_normalize_loaded(catalog, catalog_count);
    }
    return result;
}

const home_shortcut_prefs_t *home_shortcuts_get(void)
{
    (void)home_shortcuts_ensure_loaded();
    return &s_prefs;
}

uint32_t home_shortcuts_generation(void)
{
    return s_generation;
}

size_t home_shortcut_catalog(const app_manager_app_desc_t **out,
                             size_t capacity)
{
    if (out == NULL || capacity == 0U)
    {
        return 0U;
    }
    const size_t limit = capacity < HOME_SHORTCUT_CATALOG_MAX ?
                         capacity : HOME_SHORTCUT_CATALOG_MAX;
    size_t count = 0U;
    const app_manager_app_desc_t *app = app_manager_builtin_list_open();
    while (app != NULL)
    {
        if (_home_shortcut_eligible(app) && count < limit)
        {
            size_t insert = count++;
            while (insert > 0U &&
                    _home_shortcut_order(out[insert - 1U]) >
                    _home_shortcut_order(app))
            {
                out[insert] = out[insert - 1U];
                --insert;
            }
            out[insert] = app;
        }
        app = app_manager_builtin_list_get_next(app);
    }
    return count;
}

static bool _home_shortcut_selected(const home_shortcut_prefs_t *prefs,
                                    const char *app_id)
{
    if (prefs == NULL || app_id == NULL)
    {
        return false;
    }
    for (size_t index = 0U; index < prefs->count; ++index)
    {
        if (strcmp(prefs->app_ids[index], app_id) == 0)
        {
            return true;
        }
    }
    return false;
}

bool home_shortcuts_is_selected(const home_shortcut_prefs_t *prefs,
                                const char *app_id)
{
    return _home_shortcut_selected(prefs, app_id);
}

size_t home_shortcut_effective(const home_shortcut_prefs_t *prefs,
                               const app_manager_app_desc_t *const *catalog,
                               size_t catalog_count,
                               const app_manager_app_desc_t **out,
                               size_t capacity)
{
    if (prefs == NULL || catalog == NULL || out == NULL || capacity == 0U)
    {
        return 0U;
    }
    const size_t limit = capacity < HOME_SHORTCUT_SLOTS ?
                         capacity : HOME_SHORTCUT_SLOTS;
    size_t count = 0U;
    for (size_t index = 0U; index < catalog_count && count < limit; ++index)
    {
        const app_manager_app_desc_t *app = catalog[index];
        if (app == NULL)
        {
            continue;
        }
        if (prefs->configured)
        {
            if (_home_shortcut_selected(prefs, app->id))
            {
                out[count++] = app;
            }
        }
        else if (strcmp(app->id, APP_MANAGER_ID_WEATHER) != 0)
        {
            out[count++] = app;
        }
    }
    return count;
}

static size_t _home_shortcut_existing_index(const char *app_id)
{
    for (size_t index = 0U; index < s_prefs.count; ++index)
    {
        if (strcmp(s_prefs.app_ids[index], app_id) == 0)
        {
            return index;
        }
    }
    return HOME_SHORTCUT_SLOTS;
}

esp_err_t home_shortcuts_set_selected(const app_manager_app_desc_t *app,
                                      bool selected)
{
    if (app == NULL || app->id == NULL || app->id[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    (void)home_shortcuts_ensure_loaded();

    const size_t existing = _home_shortcut_existing_index(app->id);

    if (selected)
    {
        if (existing < HOME_SHORTCUT_SLOTS)
        {
            return ESP_OK;
        }
        if (s_prefs.count >= HOME_SHORTCUT_SLOTS)
        {
            return ESP_ERR_INVALID_STATE;
        }
        const size_t length = strlen(app->id) + 1U;
        if (length > APP_MANAGER_ID_BYTES)
        {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(s_prefs.app_ids[s_prefs.count], app->id, length);
        ++s_prefs.count;
    }
    else
    {
        if (existing >= HOME_SHORTCUT_SLOTS)
        {
            return ESP_OK;
        }
        for (size_t index = existing; index + 1U < s_prefs.count; ++index)
        {
            memcpy(s_prefs.app_ids[index], s_prefs.app_ids[index + 1U],
                   APP_MANAGER_ID_BYTES);
        }
        --s_prefs.count;
        memset(s_prefs.app_ids[s_prefs.count], 0,
               APP_MANAGER_ID_BYTES);
    }
    s_prefs.configured = true;
    ++s_generation;
    return _home_shortcuts_save_submit();
}

#define DBG_TAG "menu_app"
#include "mt_log.h"

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Every launcher entry uses the same two-line row. The list remains a single
 * registry-driven flow, so adding an App only adds a row. */
#define MENU_FALLBACK_SUBTITLE "打开应用"

typedef struct menu_page_state
{
    app_ui_page_t page;
    lv_obj_t *list;
    size_t entry_count;
} menu_page_state_t;

_Static_assert(sizeof(menu_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Applications page state exceeds lifecycle arena slot");

static bool _menu_skip_descriptor(const app_manager_app_desc_t *descriptor)
{
    if (descriptor == NULL || descriptor->id == NULL ||
            descriptor->root_page_id == NULL ||
            (descriptor->flags & APP_MANAGER_APP_FLAG_HIDDEN) != 0U)
    {
        return true;
    }
    return strcmp(descriptor->id, APP_MANAGER_ID_HOME) == 0 ||
           strcmp(descriptor->id, APP_MANAGER_ID_MENU) == 0;
}

static uint16_t _menu_order(const app_manager_app_desc_t *descriptor)
{
    return descriptor->launcher_order == 0U ? UINT16_MAX :
           descriptor->launcher_order;
}

static void _menu_open_app(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_run(lv_event_get_user_data(event));
    }
}

static bool _menu_add_entry(menu_page_state_t *state,
                            const app_manager_app_desc_t *app)
{
    const char *name = app->display_name != NULL ? app->display_name :
                       app->name;
    const char *subtitle =
        app->launcher_subtitle != NULL && app->launcher_subtitle[0] != '\0' ?
        app->launcher_subtitle : MENU_FALLBACK_SUBTITLE;
    return app_ui_group_add_app_image(state->list, app->icon_id,
                                      LV_SYMBOL_IMAGE, name, subtitle,
                                      _menu_open_app, (void *)app->id) != NULL;
}

static void _menu_page_build(menu_page_state_t *state)
{
    app_ui_page_create(&state->page, "应用", false);

    lv_obj_t *content = state->page.content;
    lv_obj_set_style_pad_top(content, 8, 0);
    lv_obj_set_style_pad_bottom(content, 16, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    size_t registry_count = 0U;
    const app_manager_app_desc_t *scan = app_manager_builtin_list_open();
    while (scan != NULL)
    {
        if (!_menu_skip_descriptor(scan))
        {
            ++registry_count;
        }
        scan = app_manager_builtin_list_get_next(scan);
    }
    const app_manager_app_desc_t **ordered = registry_count == 0U ? NULL :
        calloc(registry_count, sizeof(*ordered));
    if (registry_count != 0U && ordered == NULL)
    {
        (void)app_ui_add_empty_state(content, "应用目录内存不足", NULL);
        LOG_W("application menu allocation failed: count=%u",
              (unsigned)registry_count);
        return;
    }
    size_t count = 0U;
    const app_manager_app_desc_t *descriptor = app_manager_builtin_list_open();
    while (descriptor != NULL)
    {
        if (!_menu_skip_descriptor(descriptor))
        {
            size_t insert = count++;
            while (insert > 0U &&
                    _menu_order(ordered[insert - 1U]) >
                    _menu_order(descriptor))
            {
                ordered[insert] = ordered[insert - 1U];
                --insert;
            }
            ordered[insert] = descriptor;
        }
        descriptor = app_manager_builtin_list_get_next(descriptor);
    }

    if (count == 0U)
    {
        (void)app_ui_add_empty_state(content, "暂无可用应用", NULL);
        free(ordered);
        return;
    }
    state->list = app_ui_group_create(content);
    if (state->list == NULL)
    {
        (void)app_ui_add_empty_state(content, "应用目录内存不足", NULL);
        LOG_W("application menu group creation failed");
        free(ordered);
        return;
    }
    size_t rendered = 0U;
    for (size_t index = 0U; index < count; ++index)
    {
        if (_menu_add_entry(state, ordered[index]))
        {
            ++rendered;
        }
    }
    free(ordered);
    if (rendered == 0U)
    {
        lv_obj_delete(state->list);
        state->list = NULL;
        (void)app_ui_add_empty_state(content, "应用目录加载失败", NULL);
        LOG_W("application menu row creation failed: count=%u",
              (unsigned)count);
        return;
    }
    char count_text[24];
    (void)snprintf(count_text, sizeof(count_text), "共 %u 个",
                   (unsigned)rendered);
    app_ui_page_set_subtitle(&state->page, count_text);
    state->entry_count = rendered;
}

static void _menu_mount(const app_manager_page_context_t *context)
{
    _menu_page_build(context->state);
}

static void _menu_unmount(const app_manager_page_context_t *context)
{
    menu_page_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->list = NULL;
    state->entry_count = 0U;
}

static const app_manager_page_ops_t s_menu_root_ops =
{
    .mount = _menu_mount,
    .unmount = _menu_unmount,
};

static const app_manager_page_definition_t s_menu_root_definition =
{
    .ops = &s_menu_root_ops,
    .memory_size = sizeof(menu_page_state_t),
};

static const app_manager_page_route_t s_menu_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_menu_root_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(menu, APP_IMAGE_MENU_ICON, "应用",
                            APP_MANAGER_ID_MENU, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_menu_routes, 5U,
                            "全部功能");

#ifdef MENU_APP_TESTING
const app_manager_page_definition_t *menu_app_test_root_definition(void)
{
    return &s_menu_root_definition;
}
#endif

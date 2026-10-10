#define DBG_TAG "menu_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Three columns: a 4-character launcher name ("系统设置") must stay on one
 * line at the SMALL theme role, so the cell is sized to its worst case. */
#define MENU_TILE_WIDTH 100
#define MENU_TILE_HEIGHT 104

typedef struct menu_page_state
{
    app_ui_page_t page;
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

static void _menu_add_tile(menu_page_state_t *state,
                           const app_manager_app_desc_t *app)
{
    const char *name = app->display_name != NULL ? app->display_name :
                       app->name;
    lv_obj_t *tile = app_ui_add_icon_tile(state->page.content, app->icon_id,
                                          LV_SYMBOL_IMAGE, name,
                                          _menu_open_app, (void *)app->id);
    if (tile == NULL)
    {
        return;
    }
    lv_obj_set_width(tile, MENU_TILE_WIDTH);
    lv_obj_set_height(tile, MENU_TILE_HEIGHT);
}

static void _menu_page_build(menu_page_state_t *state)
{
    app_ui_page_create(&state->page, "应用", false);

    lv_obj_t *content = state->page.content;
    lv_obj_set_style_pad_top(content, 12, 0);
    lv_obj_set_style_pad_bottom(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(content, 8, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    /* Keep every launcher app reachable instead of rendering below a dead
     * edge when the registry grows past the viewport. */
    lv_obj_set_scrollable(content, true);
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
    for (size_t index = 0U; index < count; ++index)
    {
        _menu_add_tile(state, ordered[index]);
    }
    state->entry_count = count;
    free(ordered);
}

static void _menu_mount(const app_manager_page_context_t *context)
{
    _menu_page_build(context->state);
}

static void _menu_unmount(const app_manager_page_context_t *context)
{
    menu_page_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
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

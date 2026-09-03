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

static lv_obj_t *_menu_add_tile(menu_page_state_t *state,
                                const app_manager_app_desc_t *app)
{
    lv_obj_t *card = lv_button_create(state->page.content);
    app_ui_click_only(card);
    lv_obj_set_width(card, 168);
    lv_obj_set_height(card, 136);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(card, _menu_open_app, LV_EVENT_CLICKED,
                        (void *)app->id);

    const lv_image_dsc_t *image = NULL;
    if (app->icon_id != 0U &&
            app_manager_get_image(app->icon_id, &image) == ESP_OK &&
            image != NULL)
    {
        lv_obj_t *icon = lv_image_create(card);
        lv_obj_set_size(icon, 40, 40);
        lv_image_set_src(icon, image);
        app_ui_make_passive(icon, false);
    }

    const char *name = app->display_name != NULL ? app->display_name :
                       app->name;
    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_height(title, 26);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(title, name);

    if (app->launcher_subtitle != NULL)
    {
        lv_obj_t *subtitle = lv_label_create(card);
        lv_obj_set_width(subtitle, LV_PCT(100));
        lv_obj_set_height(subtitle, 24);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(subtitle,
                                    lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_obj_set_style_text_font(subtitle,
                                   app_ui_font(APP_THEME_FONT_BODY), 0);
        lv_label_set_text(subtitle, app->launcher_subtitle);
    }
    return card;
}

static void _menu_page_build(menu_page_state_t *state)
{
    app_ui_page_create_home(&state->page);

    lv_obj_t *content = state->page.content;
    lv_obj_set_style_pad_top(content, 12, 0);
    lv_obj_set_style_pad_bottom(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(content, 8, 0);
    lv_obj_set_style_pad_row(content, 8, 0);

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
        lv_obj_t *error = app_ui_add_body_label(content, "应用目录内存不足");
        lv_obj_set_width(error, LV_PCT(100));
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
        lv_obj_t *empty = app_ui_add_body_label(content, "暂无可用应用");
        lv_obj_set_width(empty, LV_PCT(100));
        free(ordered);
        return;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        (void)_menu_add_tile(state, ordered[index]);
    }
    if (count % 2U != 0U)
    {
        lv_obj_t *spacer = lv_obj_create(content);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_size(spacer, 168, 0);
        app_ui_make_passive(spacer, false);
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

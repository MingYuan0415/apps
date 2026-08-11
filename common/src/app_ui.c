#define DBG_TAG "app_ui"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_ui.h"
#include <string.h>

#define COLOR_BACKGROUND 0x0D1113
#define COLOR_SURFACE    0x1A2024
#define COLOR_SURFACE_HI 0x252D32
#define COLOR_TEXT       0xF2F5F6
#define COLOR_MUTED      0x91A0A8
#define COLOR_ACCENT     0x39C6C8
#define COLOR_SUCCESS    0x65D18A
#define COLOR_WARNING    0xF0B35A
#define COLOR_ERROR      0xF06A6A

const lv_font_t *app_ui_font(app_theme_font_id_t id)
{
    const lv_font_t *font = app_manager_get_font(id);
    return font != NULL ? font : LV_FONT_DEFAULT;
}

static bool _app_ui_id_is_valid(const char *id)
{
    size_t length = id != NULL ? strnlen(id, APP_MANAGER_ID_BYTES) : 0U;
    return length > 0U && length < APP_MANAGER_ID_BYTES;
}

static void _app_ui_navigation_complete(esp_err_t result, void *context)
{
    const char *operation = context;
    (void)operation;
    if (result != ESP_OK)
    {
        LOG_W("%s failed: %s", operation, esp_err_to_name(result));
    }
}

static void _app_ui_navigate(app_manager_nav_operation_t operation,
                             const char *app_id, const char *page_id,
                             const char *operation_name)
{
    const app_manager_nav_request_t request =
    {
        .operation = operation,
        .app_id = app_id,
        .page_id = page_id,
        .transition =
        {
            .effect = APP_MANAGER_TRANSITION_DEFAULT,
        },
    };
    esp_err_t result = app_manager_navigate_async(
                           &request, _app_ui_navigation_complete,
                           (void *)operation_name);
    if (result != ESP_OK)
    {
        LOG_W("failed to queue %s: %s", operation_name,
              esp_err_to_name(result));
    }
}

void app_ui_request_back(void)
{
    _app_ui_navigate(APP_MANAGER_NAV_OP_BACK, NULL, NULL, "back");
}

static void _app_ui_back_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_back();
    }
}

void app_ui_request_run(const char *app_id)
{
    if (!_app_ui_id_is_valid(app_id))
    {
        LOG_W("invalid app id");
        return;
    }

    _app_ui_navigate(APP_MANAGER_NAV_OP_RUN, app_id, NULL, "run app");
}

void app_ui_request_open_page(const char *app_id, const char *page_id)
{
    if (!_app_ui_id_is_valid(app_id) || !_app_ui_id_is_valid(page_id))
    {
        LOG_W("invalid page id");
        return;
    }

    _app_ui_navigate(APP_MANAGER_NAV_OP_OPEN_PAGE, app_id, page_id,
                     "open page");
}

void app_ui_page_create(app_ui_page_t *page, const char *title, bool show_back)
{
    memset(page, 0, sizeof(*page));

    lv_obj_t *screen = app_manager_this_page_screen();
    if (screen == NULL)
    {
        LOG_E("page screen unavailable");
        return;
    }
    page->root = lv_obj_create(screen);
    lv_obj_remove_style_all(page->root);
    lv_obj_set_size(page->root, LV_PCT(100), LV_PCT(100));
    lv_obj_align(page->root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(page->root, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(page->root, 0, 0);
    lv_obj_set_style_pad_gap(page->root, 0, 0);
    lv_obj_set_flex_flow(page->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_SCROLLABLE);

    page->header = lv_obj_create(page->root);
    lv_obj_remove_style_all(page->header);
    lv_obj_set_width(page->header, LV_PCT(100));
    lv_obj_set_height(page->header, 64);
    lv_obj_set_style_bg_color(page->header, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(page->header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(page->header, 12, 0);
    lv_obj_set_style_pad_right(page->header, 16, 0);
    lv_obj_set_style_pad_top(page->header, 10, 0);
    lv_obj_set_style_pad_bottom(page->header, 10, 0);
    lv_obj_set_style_pad_column(page->header, 8, 0);
    lv_obj_set_flex_flow(page->header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page->header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(page->header, LV_OBJ_FLAG_SCROLLABLE);

    if (show_back)
    {
        lv_obj_t *back = lv_button_create(page->header);
        lv_obj_set_size(back, 44, 44);
        lv_obj_set_style_radius(back, 6, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_SURFACE_HI),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_add_event_cb(back, _app_ui_back_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *icon = lv_label_create(back);
        lv_label_set_text(icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
        lv_obj_center(icon);
    }

    page->title = lv_label_create(page->header);
    lv_label_set_text(page->title, title);
    lv_obj_set_style_text_color(page->title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(page->title, app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_obj_set_flex_grow(page->title, 1);

    page->content = lv_obj_create(page->root);
    lv_obj_remove_style_all(page->content);
    lv_obj_set_width(page->content, LV_PCT(100));
    lv_obj_set_height(page->content, 0);
    lv_obj_set_flex_grow(page->content, 1);
    lv_obj_set_style_bg_color(page->content, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(page->content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(page->content, 18, 0);
    lv_obj_set_style_pad_right(page->content, 18, 0);
    lv_obj_set_style_pad_top(page->content, 10, 0);
    lv_obj_set_style_pad_bottom(page->content, 18, 0);
    lv_obj_set_style_pad_row(page->content, 10, 0);
    lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(page->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page->content, LV_SCROLLBAR_MODE_AUTO);
}

void app_ui_page_destroy(app_ui_page_t *page)
{
    if (page->root != NULL)
    {
        lv_obj_delete(page->root);
    }
    memset(page, 0, sizeof(*page));
}

lv_obj_t *app_ui_add_section(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_pad_top(label, 5, 0);
    return label;
}

static lv_obj_t *_app_ui_add_action(lv_obj_t *parent, const char *symbol,
                                    const char *title, const char *subtitle,
                                    lv_event_cb_t callback, void *user_data,
                                    bool navigation)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, subtitle != NULL ? 66 : 58);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_left(button, 14, 0);
    lv_obj_set_style_pad_right(button, 14, 0);
    lv_obj_set_style_pad_top(button, 8, 0);
    lv_obj_set_style_pad_bottom(button, 8, 0);
    lv_obj_set_style_pad_column(button, 12, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(button);
    lv_label_set_text(icon, symbol != NULL ? symbol : LV_SYMBOL_RIGHT);
    lv_obj_set_width(icon, 28);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);

    lv_obj_t *text = lv_obj_create(button);
    lv_obj_remove_style_all(text);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(text, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(text);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_BODY), 0);

    if (subtitle != NULL)
    {
        lv_obj_t *subtitle_label = lv_label_create(text);
        lv_label_set_text(subtitle_label, subtitle);
        lv_obj_set_width(subtitle_label, LV_PCT(100));
        lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(subtitle_label, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(subtitle_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
    }

    if (navigation)
    {
        lv_obj_t *chevron = lv_label_create(button);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(chevron, LV_FONT_DEFAULT, 0);
    }

    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

lv_obj_t *app_ui_add_action(lv_obj_t *parent, const char *symbol,
                            const char *title, const char *subtitle,
                            lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, symbol, title, subtitle, callback,
                              user_data, true);
}

lv_obj_t *app_ui_add_command(lv_obj_t *parent, const char *symbol,
                             const char *title, const char *subtitle,
                             lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, symbol, title, subtitle, callback,
                              user_data, false);
}

lv_obj_t *app_ui_add_value_row(lv_obj_t *parent, const char *name,
                               const char *value, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 68);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, LV_PCT(36));
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(name_label, app_ui_font(APP_THEME_FONT_SMALL), 0);

    lv_obj_t *current = lv_label_create(row);
    lv_label_set_text(current, value);
    lv_obj_set_width(current, LV_PCT(60));
    lv_label_set_long_mode(current, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(current, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(current, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(current, app_ui_font(APP_THEME_FONT_BODY), 0);
    if (value_label != NULL)
    {
        *value_label = current;
    }
    return row;
}

lv_obj_t *app_ui_add_body_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_obj_set_style_text_line_space(label, 6, 0);
    return label;
}

void app_ui_set_status_text(lv_obj_t *label, const char *text,
                            app_ui_status_t status)
{
    uint32_t color = COLOR_MUTED;
    switch (status)
    {
    case APP_UI_STATUS_ACCENT:
        color = COLOR_ACCENT;
        break;
    case APP_UI_STATUS_SUCCESS:
        color = COLOR_SUCCESS;
        break;
    case APP_UI_STATUS_WARNING:
        color = COLOR_WARNING;
        break;
    case APP_UI_STATUS_ERROR:
        color = COLOR_ERROR;
        break;
    case APP_UI_STATUS_NEUTRAL:
    default:
        break;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

#define DBG_TAG "app_ui"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_ui.h"
#include "app_ui_theme.h"
#include <string.h>

#define COLOR_BACKGROUND APP_UI_COLOR_BACKGROUND
#define COLOR_SURFACE    APP_UI_COLOR_SURFACE
#define COLOR_SURFACE_HI APP_UI_COLOR_SURFACE_HI
#define COLOR_TEXT       APP_UI_COLOR_TEXT
#define COLOR_MUTED      APP_UI_COLOR_MUTED
#define COLOR_ACCENT     APP_UI_COLOR_RAIN
#define COLOR_SUCCESS    0x65D18A
#define COLOR_WARNING    APP_UI_COLOR_SUN
#define COLOR_ERROR      APP_UI_COLOR_WARNING

const lv_font_t *app_ui_font(app_theme_font_id_t id)
{
    const lv_font_t *font = app_manager_get_font(id);
    return font != NULL ? font : LV_FONT_DEFAULT;
}

void app_ui_make_passive(lv_obj_t *object, bool scrollable)
{
    if (object == NULL)
    {
        return;
    }
    uint32_t passive_flags = LV_OBJ_FLAG_CLICK_FOCUSABLE |
                             LV_OBJ_FLAG_GESTURE_BUBBLE |
                             LV_OBJ_FLAG_SCROLL_ELASTIC |
                             LV_OBJ_FLAG_SCROLL_MOMENTUM;
    lv_obj_remove_flag(object, passive_flags);
    if (scrollable)
    {
        lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
    else
    {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
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

static void _app_ui_page_create_root(app_ui_page_t *page, bool with_header)
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
    app_ui_make_passive(page->root, false);

    if (!with_header)
    {
        page->content = page->root;
        lv_obj_set_style_pad_left(page->content, 12, 0);
        lv_obj_set_style_pad_right(page->content, 12, 0);
        lv_obj_set_style_pad_top(page->content, 8, 0);
        lv_obj_set_style_pad_bottom(page->content, 12, 0);
        lv_obj_set_style_pad_row(page->content, 8, 0);
        lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(page->content, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        app_ui_make_passive(page->content, false);
        return;
    }

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
    app_ui_make_passive(page->header, false);

    page->header_text = lv_obj_create(page->header);
    lv_obj_remove_style_all(page->header_text);
    lv_obj_set_width(page->header_text, 0);
    lv_obj_set_height(page->header_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(page->header_text, 1);
    lv_obj_set_flex_flow(page->header_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page->header_text, 1, 0);
    app_ui_make_passive(page->header_text, false);

    page->title = lv_label_create(page->header_text);
    lv_obj_set_width(page->title, LV_PCT(100));
    lv_obj_set_style_text_color(page->title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(page->title, app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_label_set_long_mode(page->title, LV_LABEL_LONG_SCROLL_CIRCULAR);

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
    app_ui_make_passive(page->content, true);
    lv_obj_set_scroll_dir(page->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page->content, LV_SCROLLBAR_MODE_AUTO);
}

void app_ui_page_create(app_ui_page_t *page, const char *title, bool show_back)
{
    _app_ui_page_create_root(page, true);
    if (page->header == NULL)
    {
        return;
    }
    (void)show_back;
    lv_label_set_text(page->title, title != NULL ? title : "");
}

void app_ui_page_create_home(app_ui_page_t *page)
{
    _app_ui_page_create_root(page, false);
}

void app_ui_page_set_title(app_ui_page_t *page, const char *title)
{
    if (page == NULL || page->title == NULL)
    {
        return;
    }
    lv_label_set_text(page->title, title != NULL ? title : "");
}

void app_ui_page_set_subtitle(app_ui_page_t *page, const char *subtitle)
{
    if (page == NULL || page->header == NULL)
    {
        return;
    }
    if (page->subtitle == NULL)
    {
        page->subtitle = lv_label_create(page->header_text);
        lv_obj_set_width(page->subtitle, LV_PCT(100));
        lv_obj_set_style_text_color(page->subtitle, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(page->subtitle,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_long_mode(page->subtitle, LV_LABEL_LONG_WRAP);
    }
    lv_label_set_text(page->subtitle, subtitle != NULL ? subtitle : "");
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
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_pad_top(label, 5, 0);
    lv_label_set_text(label, text != NULL ? text : "");
    return label;
}

static lv_obj_t *_app_ui_add_action(lv_obj_t *parent, const char *symbol,
                                    const char *title, const char *subtitle,
                                    lv_event_cb_t callback, void *user_data,
                                    bool navigation)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, LV_SIZE_CONTENT);
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
    lv_obj_set_width(icon, 28);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
    app_ui_make_passive(icon, false);
    lv_label_set_text(icon, symbol != NULL ? symbol : LV_SYMBOL_RIGHT);

    lv_obj_t *text = lv_obj_create(button);
    lv_obj_remove_style_all(text);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    app_ui_make_passive(text, false);

    lv_obj_t *title_label = lv_label_create(text);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(title_label, title != NULL ? title : "");

    if (subtitle != NULL)
    {
        lv_obj_t *subtitle_label = lv_label_create(text);
        lv_obj_set_width(subtitle_label, LV_PCT(100));
        lv_obj_set_height(subtitle_label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(subtitle_label, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(subtitle_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(subtitle_label, subtitle);
    }

    if (navigation)
    {
        lv_obj_t *chevron = lv_label_create(button);
        lv_obj_set_style_text_color(chevron, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(chevron, LV_FONT_DEFAULT, 0);
        app_ui_make_passive(chevron, false);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
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

lv_obj_t *app_ui_add_icon_button(lv_obj_t *parent, uint32_t image_id,
                                 const char *fallback_symbol,
                                 lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(button, 0);
    lv_obj_set_height(button, 56);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }

    const lv_image_dsc_t *descriptor = NULL;
    if (image_id != 0U && app_manager_get_image(image_id, &descriptor) == ESP_OK)
    {
        lv_obj_t *image = lv_image_create(button);
        if (image != NULL)
        {
            lv_obj_set_size(image, 40, 40);
            lv_image_set_src(image, descriptor);
            app_ui_make_passive(image, false);
            lv_obj_center(image);
            return button;
        }
    }

    lv_obj_t *symbol = lv_label_create(button);
    if (symbol != NULL)
    {
        lv_obj_set_size(symbol, 40, 40);
        lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
        lv_label_set_text(symbol, fallback_symbol != NULL ? fallback_symbol :
                          LV_SYMBOL_RIGHT);
        app_ui_make_passive(symbol, false);
        lv_obj_center(symbol);
    }
    return button;
}

lv_obj_t *app_ui_add_value_row(lv_obj_t *parent, const char *name,
                               const char *value, lv_obj_t **value_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
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
    app_ui_make_passive(row, false);

    lv_obj_t *name_label = lv_label_create(row);
    lv_obj_set_width(name_label, LV_PCT(36));
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(name_label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(name_label, name != NULL ? name : "");

    lv_obj_t *current = lv_label_create(row);
    lv_obj_set_width(current, LV_PCT(60));
    lv_label_set_long_mode(current, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(current, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(current, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(current, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(current, value != NULL ? value : "");
    if (value_label != NULL)
    {
        *value_label = current;
    }
    return row;
}

lv_obj_t *app_ui_add_body_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_obj_set_style_text_line_space(label, 6, 0);
    lv_label_set_text(label, text != NULL ? text : "");
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
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
}

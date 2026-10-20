#define DBG_TAG "app_ui"
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include <string.h>

#define COLOR_BACKGROUND APP_UI_COLOR_BACKGROUND
#define COLOR_SURFACE    APP_UI_COLOR_SURFACE
#define COLOR_SHEET      APP_UI_COLOR_SHEET
#define COLOR_SURFACE_HI APP_UI_COLOR_SURFACE_HI
#define COLOR_TEXT       APP_UI_COLOR_TEXT
#define COLOR_MUTED      APP_UI_COLOR_MUTED

/* ~78% black scrim behind a modal bottom sheet. */
#define APP_UI_SHEET_SCRIM_OPA 200
/* A sheet's action list is unbounded; cap the panel and scroll the excess. */
#define APP_UI_SHEET_MAX_HEIGHT 420
/* Sheet panel floats inset from the screen edges like a card. */
#define APP_UI_SHEET_INSET 8

/* Row glyph art is authored at 28 px; the circular well frames it. */
#define APP_UI_ROW_GLYPH 28
#define APP_UI_ROW_WELL  40

/* Launcher icon well; the 64 px record is drawn 1:1 inside it. */
#define APP_UI_TILE_WELL  72
#define APP_UI_TILE_IMAGE 64

/* Shared local styles. Instantiating one style per property on every widget
 * fragments the heap and costs more than the widgets themselves; this table
 * is initialized once on the LVGL worker after lv_init(). */
static lv_style_t s_page_root;
static lv_style_t s_page_content;
static lv_style_t s_page_content_home;
static lv_style_t s_page_header;
static lv_style_t s_header_text;
static lv_style_t s_section_label;
static lv_style_t s_symbol;
static lv_style_t s_title_label;
static lv_style_t s_subtitle_label;
static lv_style_t s_body_label;
static lv_style_t s_chevron;
static lv_style_t s_text_column;
static lv_style_t s_card_row;
static lv_style_t s_row_well;
static lv_style_t s_row_well_danger;
static lv_style_t s_button;
static lv_style_t s_button_primary;
static lv_style_t s_button_primary_pressed;
static lv_style_t s_button_danger;
static lv_style_t s_button_danger_pressed;
static lv_style_t s_chip_button;
static lv_style_t s_chip_selected;
static lv_style_t s_icon_tile;
static lv_style_t s_icon_chip;
static lv_style_t s_group;
static lv_style_t s_group_row;
static lv_style_t s_group_divider;
static lv_style_t s_value_label;
static lv_style_t s_empty_state;
static lv_style_t s_sheet_scrim;
static lv_style_t s_sheet_panel;
static lv_style_t s_page_action_button;
static lv_style_t s_surface_pressed;
static lv_style_t s_tile_pressed;
static lv_style_t s_switch_main;
static lv_style_t s_switch_indicator_checked;
static lv_style_t s_switch_knob;
static lv_style_t s_pill;
static bool s_styles_ready;

static void _app_ui_styles_init_layout(void)
{
    lv_style_init(&s_page_root);
    lv_style_set_bg_color(&s_page_root, lv_color_hex(COLOR_BACKGROUND));
    lv_style_set_bg_opa(&s_page_root, LV_OPA_COVER);
    lv_style_set_pad_all(&s_page_root, 0);
    lv_style_set_pad_gap(&s_page_root, 0);

    lv_style_init(&s_page_content);
    lv_style_set_bg_color(&s_page_content, lv_color_hex(COLOR_BACKGROUND));
    lv_style_set_bg_opa(&s_page_content, LV_OPA_COVER);
    lv_style_set_pad_left(&s_page_content, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_right(&s_page_content, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_top(&s_page_content, 8);
    lv_style_set_pad_bottom(&s_page_content, 24);
    lv_style_set_pad_row(&s_page_content, APP_UI_GAP);

    lv_style_init(&s_page_content_home);
    lv_style_set_bg_color(&s_page_content_home, lv_color_hex(COLOR_BACKGROUND));
    lv_style_set_bg_opa(&s_page_content_home, LV_OPA_COVER);
    lv_style_set_pad_left(&s_page_content_home, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_right(&s_page_content_home, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_top(&s_page_content_home, 10);
    lv_style_set_pad_bottom(&s_page_content_home, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_row(&s_page_content_home, APP_UI_GAP);

    /* 64 px title-only header (36 px title). The trailing action is pinned to
     * the bottom, as low as the header allows, so the 40 px control-center pull
     * band only reaches the button's top edge and never its glyph. */
    lv_style_init(&s_page_header);
    lv_style_set_bg_color(&s_page_header, lv_color_hex(COLOR_BACKGROUND));
    lv_style_set_bg_opa(&s_page_header, LV_OPA_COVER);
    lv_style_set_pad_left(&s_page_header, APP_UI_PAGE_MARGIN + 4);
    lv_style_set_pad_right(&s_page_header, APP_UI_PAGE_MARGIN);
    lv_style_set_pad_top(&s_page_header, 12);
    lv_style_set_pad_bottom(&s_page_header, 4);
    lv_style_set_pad_column(&s_page_header, APP_UI_GAP);

    lv_style_init(&s_header_text);
    lv_style_set_pad_row(&s_header_text, 0);

    lv_style_init(&s_section_label);
    lv_style_set_text_color(&s_section_label, lv_color_hex(APP_UI_COLOR_FAINT));
    lv_style_set_pad_top(&s_section_label, 8);
    lv_style_set_pad_left(&s_section_label, 4);

    lv_style_init(&s_symbol);
    lv_style_set_text_align(&s_symbol, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_symbol, lv_color_hex(COLOR_MUTED));

    lv_style_init(&s_title_label);
    lv_style_set_text_color(&s_title_label, lv_color_hex(COLOR_TEXT));

    lv_style_init(&s_subtitle_label);
    lv_style_set_text_color(&s_subtitle_label, lv_color_hex(COLOR_MUTED));

    lv_style_init(&s_body_label);
    lv_style_set_text_color(&s_body_label, lv_color_hex(COLOR_MUTED));
    lv_style_set_text_line_space(&s_body_label, 6);

    lv_style_init(&s_chevron);
    lv_style_set_text_color(&s_chevron, lv_color_hex(APP_UI_COLOR_FAINT));

    lv_style_init(&s_text_column);
    lv_style_set_pad_row(&s_text_column, 2);

    lv_style_init(&s_value_label);
    lv_style_set_text_align(&s_value_label, LV_TEXT_ALIGN_RIGHT);
    lv_style_set_text_color(&s_value_label, lv_color_hex(COLOR_MUTED));

    lv_style_init(&s_empty_state);
    lv_style_set_pad_top(&s_empty_state, 56);
    lv_style_set_pad_bottom(&s_empty_state, 24);
    lv_style_set_pad_row(&s_empty_state, 8);
}
static void _app_ui_styles_init_surfaces(void)
{
    lv_style_init(&s_card_row);
    lv_style_set_radius(&s_card_row, APP_UI_RADIUS_CARD);
    lv_style_set_bg_color(&s_card_row, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_card_row, LV_OPA_COVER);
    lv_style_set_border_width(&s_card_row, 0);
    lv_style_set_shadow_width(&s_card_row, 0);
    lv_style_set_pad_left(&s_card_row, APP_UI_CARD_PAD);
    lv_style_set_pad_right(&s_card_row, 14);
    lv_style_set_pad_top(&s_card_row, 10);
    lv_style_set_pad_bottom(&s_card_row, 10);
    lv_style_set_pad_column(&s_card_row, 14);

    lv_style_init(&s_row_well);
    lv_style_set_radius(&s_row_well, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_row_well, lv_color_hex(APP_UI_COLOR_RAIN_TINT));
    lv_style_set_bg_opa(&s_row_well, LV_OPA_COVER);

    lv_style_init(&s_row_well_danger);
    lv_style_set_bg_color(&s_row_well_danger,
                          lv_color_hex(APP_UI_COLOR_WARNING_TINT));

    lv_style_init(&s_group);
    lv_style_set_bg_color(&s_group, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_group, LV_OPA_COVER);
    lv_style_set_radius(&s_group, APP_UI_RADIUS_CARD);
    lv_style_set_pad_all(&s_group, 4);
    lv_style_set_pad_row(&s_group, 0);

    /* Rows stay square so the 1 px divider is a straight line; the pressed
     * state rounds the highlight inside the group's 4 px inset. */
    lv_style_init(&s_group_row);
    lv_style_set_radius(&s_group_row, 0);
    lv_style_set_bg_opa(&s_group_row, LV_OPA_TRANSP);
    lv_style_set_shadow_width(&s_group_row, 0);
    lv_style_set_border_width(&s_group_row, 0);
    lv_style_set_pad_left(&s_group_row, 12);
    lv_style_set_pad_right(&s_group_row, 10);
    lv_style_set_pad_column(&s_group_row, 14);

    lv_style_init(&s_group_divider);
    lv_style_set_border_side(&s_group_divider, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&s_group_divider, 1);
    lv_style_set_border_color(&s_group_divider,
                              lv_color_hex(APP_UI_COLOR_HAIRLINE));
    lv_style_set_border_opa(&s_group_divider, LV_OPA_COVER);

    lv_style_init(&s_tile_pressed);
    lv_style_set_bg_color(&s_tile_pressed, lv_color_hex(COLOR_SURFACE_HI));
    lv_style_set_bg_opa(&s_tile_pressed, LV_OPA_COVER);
    lv_style_set_radius(&s_tile_pressed, 16);
    lv_style_set_border_opa(&s_tile_pressed, LV_OPA_TRANSP);

    lv_style_init(&s_surface_pressed);
    lv_style_set_bg_color(&s_surface_pressed, lv_color_hex(COLOR_SURFACE_HI));

    lv_style_init(&s_sheet_scrim);
    lv_style_set_bg_color(&s_sheet_scrim, lv_color_hex(APP_THEME_COLOR_SCRIM));
    lv_style_set_bg_opa(&s_sheet_scrim, APP_UI_SHEET_SCRIM_OPA);
    lv_style_set_pad_all(&s_sheet_scrim, APP_UI_SHEET_INSET);

    lv_style_init(&s_sheet_panel);
    lv_style_set_bg_color(&s_sheet_panel, lv_color_hex(COLOR_SHEET));
    lv_style_set_bg_opa(&s_sheet_panel, LV_OPA_COVER);
    lv_style_set_radius(&s_sheet_panel, APP_UI_RADIUS_SHEET);
    lv_style_set_pad_left(&s_sheet_panel, 20);
    lv_style_set_pad_right(&s_sheet_panel, 20);
    lv_style_set_pad_top(&s_sheet_panel, 24);
    lv_style_set_pad_bottom(&s_sheet_panel, 20);
    lv_style_set_pad_row(&s_sheet_panel, 10);

    lv_style_init(&s_pill);
    lv_style_set_radius(&s_pill, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&s_pill, LV_OPA_COVER);
    lv_style_set_pad_left(&s_pill, 12);
    lv_style_set_pad_right(&s_pill, 12);
    lv_style_set_pad_top(&s_pill, 4);
    lv_style_set_pad_bottom(&s_pill, 4);
    lv_style_set_pad_column(&s_pill, 8);
}
static void _app_ui_styles_init_controls(void)
{
    /* Pill buttons: secondary is the default, primary/danger are accent fills.
     * Pressed states always lighten (emissive panel). */
    lv_style_init(&s_button);
    lv_style_set_radius(&s_button, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_button, lv_color_hex(COLOR_SURFACE_HI));
    lv_style_set_bg_opa(&s_button, LV_OPA_COVER);
    lv_style_set_border_width(&s_button, 0);
    lv_style_set_shadow_width(&s_button, 0);
    lv_style_set_text_color(&s_button, lv_color_hex(COLOR_TEXT));

    lv_style_init(&s_button_primary);
    lv_style_set_bg_color(&s_button_primary, lv_color_hex(APP_UI_COLOR_RAIN));
    lv_style_set_text_color(&s_button_primary,
                            lv_color_hex(APP_UI_COLOR_ON_ACCENT));

    lv_style_init(&s_button_primary_pressed);
    lv_style_set_bg_color(&s_button_primary_pressed,
                          lv_color_hex(APP_UI_COLOR_RAIN_HI));

    lv_style_init(&s_button_danger);
    lv_style_set_bg_color(&s_button_danger,
                          lv_color_hex(APP_UI_COLOR_WARNING_TINT));
    lv_style_set_text_color(&s_button_danger,
                            lv_color_hex(APP_UI_COLOR_WARNING));

    lv_style_init(&s_button_danger_pressed);
    lv_style_set_bg_color(&s_button_danger_pressed,
                          lv_color_hex(APP_UI_COLOR_SURFACE_HI));

    /* Segmented control: the row is the pill track, chips are transparent
     * segments and the selected one lifts onto the L2 surface. */
    lv_style_init(&s_chip_button);
    lv_style_set_radius(&s_chip_button, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&s_chip_button, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_chip_button, 0);
    lv_style_set_shadow_width(&s_chip_button, 0);
    lv_style_set_pad_left(&s_chip_button, 2);
    lv_style_set_pad_right(&s_chip_button, 2);

    lv_style_init(&s_chip_selected);
    lv_style_set_bg_color(&s_chip_selected, lv_color_hex(APP_UI_COLOR_TRACK));
    lv_style_set_bg_opa(&s_chip_selected, LV_OPA_COVER);

    lv_style_init(&s_icon_tile);
    lv_style_set_radius(&s_icon_tile, APP_UI_RADIUS_CARD);
    lv_style_set_bg_opa(&s_icon_tile, LV_OPA_TRANSP);
    lv_style_set_shadow_width(&s_icon_tile, 0);
    lv_style_set_border_width(&s_icon_tile, 0);
    lv_style_set_pad_all(&s_icon_tile, 4);
    lv_style_set_pad_row(&s_icon_tile, 8);

    lv_style_init(&s_icon_chip);
    lv_style_set_radius(&s_icon_chip, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_icon_chip, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_icon_chip, LV_OPA_COVER);

    lv_style_init(&s_page_action_button);
    lv_style_set_radius(&s_page_action_button, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_page_action_button, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&s_page_action_button, LV_OPA_COVER);
    lv_style_set_border_width(&s_page_action_button, 0);
    lv_style_set_shadow_width(&s_page_action_button, 0);

    lv_style_init(&s_switch_main);
    lv_style_set_bg_color(&s_switch_main, lv_color_hex(APP_UI_COLOR_TRACK));
    lv_style_set_bg_opa(&s_switch_main, LV_OPA_COVER);
    lv_style_set_border_width(&s_switch_main, 0);

    lv_style_init(&s_switch_indicator_checked);
    lv_style_set_bg_color(&s_switch_indicator_checked,
                          lv_color_hex(APP_UI_COLOR_RAIN));
    lv_style_set_bg_opa(&s_switch_indicator_checked, LV_OPA_COVER);

    lv_style_init(&s_switch_knob);
    lv_style_set_bg_color(&s_switch_knob, lv_color_hex(COLOR_TEXT));
    lv_style_set_bg_opa(&s_switch_knob, LV_OPA_COVER);
    lv_style_set_pad_all(&s_switch_knob, -4);
}

static void _app_ui_styles_init(void)
{
    if (s_styles_ready)
    {
        return;
    }
    _app_ui_styles_init_layout();
    _app_ui_styles_init_surfaces();
    _app_ui_styles_init_controls();
    s_styles_ready = true;
}

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
    lv_obj_set_click_focusable(object, false);
    lv_obj_set_gesture_bubble(object, false);
    lv_obj_set_scroll_elastic(object, false);
    lv_obj_set_scroll_momentum(object, false);
    if (scrollable)
    {
        lv_obj_set_clickable(object, true);
        lv_obj_set_scrollable(object, true);
    }
    else
    {
        lv_obj_set_clickable(object, false);
        lv_obj_set_scrollable(object, false);
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
    _app_ui_styles_init();
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
    lv_obj_add_style(page->root, &s_page_root, 0);
    lv_obj_set_flex_flow(page->root, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(page->root, false);

    if (!with_header)
    {
        page->content = page->root;
        lv_obj_add_style(page->content, &s_page_content_home, 0);
        lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(page->content, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        app_ui_make_passive(page->content, false);
        return;
    }

    page->header = lv_obj_create(page->root);
    lv_obj_remove_style_all(page->header);
    lv_obj_set_width(page->header, LV_PCT(100));
    lv_obj_set_height(page->header, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(page->header, 64, 0);
    lv_obj_add_style(page->header, &s_page_header, 0);
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
    lv_obj_add_style(page->header_text, &s_header_text, 0);
    app_ui_make_passive(page->header_text, false);

    page->title = lv_label_create(page->header_text);
    lv_obj_set_width(page->title, LV_PCT(100));
    lv_obj_add_style(page->title, &s_title_label, 0);
    lv_obj_set_style_text_font(page->title, app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_label_set_long_mode(page->title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    page->content = lv_obj_create(page->root);
    lv_obj_remove_style_all(page->content);
    lv_obj_set_width(page->content, LV_PCT(100));
    lv_obj_set_height(page->content, 0);
    lv_obj_set_flex_grow(page->content, 1);
    lv_obj_add_style(page->content, &s_page_content, 0);
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
    _app_ui_styles_init();
    if (page->subtitle == NULL)
    {
        page->subtitle = lv_label_create(page->header_text);
        lv_obj_set_width(page->subtitle, LV_PCT(100));
        lv_obj_add_style(page->subtitle, &s_subtitle_label, 0);
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
    _app_ui_styles_init();
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_add_style(label, &s_section_label, 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    return label;
}

lv_obj_t *app_ui_image(lv_obj_t *parent, uint32_t image_id,
                       const char *fallback_symbol, int32_t size)
{
    _app_ui_styles_init();
    if (size <= 0)
    {
        size = 28;
    }
    const lv_image_dsc_t *descriptor = NULL;
    if (image_id != 0U &&
            app_manager_get_image(image_id, &descriptor) == ESP_OK &&
            descriptor != NULL)
    {
        lv_obj_t *image = lv_image_create(parent);
        if (image != NULL)
        {
            lv_obj_set_size(image, size, size);
            lv_image_set_src(image, descriptor);
            app_ui_make_passive(image, false);
            return image;
        }
    }
    lv_obj_t *symbol = lv_label_create(parent);
    if (symbol == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(symbol, size);
    lv_obj_add_style(symbol, &s_symbol, 0);
    lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
    lv_label_set_text(symbol,
                      fallback_symbol != NULL ? fallback_symbol :
                      LV_SYMBOL_IMAGE);
    app_ui_make_passive(symbol, false);
    return symbol;
}

void app_ui_icon_set_color(lv_obj_t *icon, uint32_t color)
{
    if (icon == NULL)
    {
        return;
    }
    lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor(icon, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
}

/* Circular tinted well framing one 28 px row glyph. */
static lv_obj_t *_app_ui_row_well(lv_obj_t *parent, uint32_t image_id,
                                  const char *symbol, bool danger)
{
    lv_obj_t *well = lv_obj_create(parent);
    lv_obj_remove_style_all(well);
    lv_obj_set_size(well, APP_UI_ROW_WELL, APP_UI_ROW_WELL);
    lv_obj_add_style(well, &s_row_well, 0);
    if (danger)
    {
        lv_obj_add_style(well, &s_row_well_danger, 0);
    }
    app_ui_make_passive(well, false);
    lv_obj_t *icon = app_ui_image(well, image_id,
                                  symbol != NULL ? symbol : LV_SYMBOL_RIGHT,
                                  APP_UI_ROW_GLYPH);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, danger ? APP_UI_COLOR_WARNING :
                              APP_UI_COLOR_RAIN);
        lv_obj_center(icon);
    }
    return well;
}

static void _app_ui_chevron(lv_obj_t *parent)
{
    lv_obj_t *chevron = app_ui_image(parent, APP_IMAGE_UI_CHEVRON_RIGHT,
                                     LV_SYMBOL_RIGHT, APP_UI_ROW_GLYPH);
    if (chevron != NULL)
    {
        lv_obj_add_style(chevron, &s_chevron, 0);
        app_ui_icon_set_color(chevron, APP_UI_COLOR_FAINT);
    }
}

/* Title/subtitle column that grows between a row's leading and trailing
 * elements. Long titles ellipsize; subtitles wrap when @p wrap is set. */
static lv_obj_t *_app_ui_row_text(lv_obj_t *row, const char *title,
                                  const char *subtitle, uint32_t title_color,
                                  bool wrap, lv_obj_t **title_out,
                                  lv_obj_t **subtitle_out)
{
    if (subtitle_out != NULL)
    {
        *subtitle_out = NULL;
    }
    lv_obj_t *text = lv_obj_create(row);
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(text, &s_text_column, 0);
    app_ui_make_passive(text, false);

    lv_obj_t *title_label = lv_label_create(text);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title_label, lv_color_hex(title_color), 0);
    lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(title_label, title != NULL ? title : "");
    app_ui_make_passive(title_label, false);
    if (title_out != NULL)
    {
        *title_out = title_label;
    }
    if (subtitle != NULL)
    {
        lv_obj_t *subtitle_label = lv_label_create(text);
        lv_obj_set_width(subtitle_label, LV_PCT(100));
        lv_obj_set_height(subtitle_label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(subtitle_label,
                               wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
        lv_obj_add_style(subtitle_label, &s_subtitle_label, 0);
        lv_obj_set_style_text_font(subtitle_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(subtitle_label, subtitle);
        app_ui_make_passive(subtitle_label, false);
        if (subtitle_out != NULL)
        {
            *subtitle_out = subtitle_label;
        }
    }
    return text;
}

static lv_obj_t *_app_ui_add_action(lv_obj_t *parent, uint32_t image_id,
                                    const char *symbol,
                                    const char *title, const char *subtitle,
                                    lv_event_cb_t callback, void *user_data,
                                    bool navigation, bool danger)
{
    _app_ui_styles_init();
    lv_obj_t *button = lv_button_create(parent);
    app_ui_click_only(button);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(button, APP_UI_ROW_HEIGHT_TALL, 0);
    lv_obj_add_style(button, &s_card_row, 0);
    lv_obj_add_style(button, &s_surface_pressed, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    (void)_app_ui_row_well(button, image_id, symbol, danger);
    (void)_app_ui_row_text(button, title, subtitle,
                           danger ? APP_UI_COLOR_WARNING : COLOR_TEXT, true,
                           NULL, NULL);
    if (navigation)
    {
        _app_ui_chevron(button);
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
    return _app_ui_add_action(parent, 0U, symbol, title, subtitle, callback,
                              user_data, true, false);
}

lv_obj_t *app_ui_add_action_image(lv_obj_t *parent, uint32_t image_id,
                                  const char *fallback_symbol,
                                  const char *title, const char *subtitle,
                                  lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, image_id, fallback_symbol, title,
                              subtitle, callback, user_data, true, false);
}

lv_obj_t *app_ui_add_command(lv_obj_t *parent, const char *symbol,
                             const char *title, const char *subtitle,
                             lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, 0U, symbol, title, subtitle, callback,
                              user_data, false, false);
}

lv_obj_t *app_ui_add_command_image(lv_obj_t *parent, uint32_t image_id,
                                   const char *fallback_symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, image_id, fallback_symbol, title,
                              subtitle, callback, user_data, false, false);
}

lv_obj_t *app_ui_add_entry_row(lv_obj_t *parent, const char *title,
                               lv_obj_t **summary_out,
                               lv_event_cb_t callback, void *user_data)
{
    _app_ui_styles_init();
    lv_obj_t *button = lv_button_create(parent);
    app_ui_click_only(button);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, APP_UI_ROW_HEIGHT_TALL);
    lv_obj_add_style(button, &s_card_row, 0);
    lv_obj_add_style(button, &s_surface_pressed, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *summary = NULL;
    (void)_app_ui_row_text(button, title, "", COLOR_TEXT, false, NULL,
                           &summary);
    if (summary_out != NULL)
    {
        *summary_out = summary;
    }
    _app_ui_chevron(button);
    return button;
}

lv_obj_t *app_ui_add_danger_action(lv_obj_t *parent, const char *symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data)
{
    return _app_ui_add_action(parent, 0U, symbol, title, subtitle, callback,
                              user_data, true, true);
}

lv_obj_t *app_ui_add_danger_action_image(lv_obj_t *parent, uint32_t image_id,
        const char *fallback_symbol,
        const char *title,
        const char *subtitle,
        lv_event_cb_t callback,
        void *user_data)
{
    return _app_ui_add_action(parent, image_id, fallback_symbol, title,
                              subtitle, callback, user_data, true, true);
}

lv_obj_t *app_ui_button_row_create(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, APP_UI_GAP, 0);
    app_ui_make_passive(row, false);
    return row;
}

lv_obj_t *app_ui_button_create(lv_obj_t *row, const char *text,
                               lv_event_cb_t callback, void *user_data)
{
    _app_ui_styles_init();
    lv_obj_t *button = lv_button_create(row);
    app_ui_click_only(button);
    lv_obj_set_width(button, 0);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_add_style(button, &s_button, 0);
    lv_obj_add_style(button, &s_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_center(label);
    return button;
}

void app_ui_button_set_kind(lv_obj_t *button, app_ui_button_kind_t kind)
{
    if (button == NULL)
    {
        return;
    }
    _app_ui_styles_init();
    lv_obj_remove_style(button, &s_button_primary, 0);
    lv_obj_remove_style(button, &s_button_primary_pressed, LV_STATE_PRESSED);
    lv_obj_remove_style(button, &s_button_danger, 0);
    lv_obj_remove_style(button, &s_button_danger_pressed, LV_STATE_PRESSED);
    if (kind == APP_UI_BUTTON_PRIMARY)
    {
        lv_obj_add_style(button, &s_button_primary, 0);
        lv_obj_add_style(button, &s_button_primary_pressed, LV_STATE_PRESSED);
    }
    else if (kind == APP_UI_BUTTON_DANGER)
    {
        lv_obj_add_style(button, &s_button_danger, 0);
        lv_obj_add_style(button, &s_button_danger_pressed, LV_STATE_PRESSED);
    }
}

void app_ui_button_set_text(lv_obj_t *button, const char *text)
{
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label != NULL)
    {
        app_ui_label_set_text_if(label, text != NULL ? text : "");
    }
}

lv_obj_t *app_ui_chip_row_create(lv_obj_t *parent)
{
    _app_ui_styles_init();
    lv_obj_t *row = app_ui_button_row_create(parent, APP_UI_CHIP_HEIGHT);
    lv_obj_set_style_radius(row, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_column(row, 2, 0);
    return row;
}

lv_obj_t *app_ui_chip_create(lv_obj_t *row, const char *text,
                             lv_event_cb_t callback, void *user_data)
{
    _app_ui_styles_init();
    lv_obj_t *chip = lv_button_create(row);
    app_ui_click_only(chip);
    lv_obj_set_width(chip, 0);
    lv_obj_set_flex_grow(chip, 1);
    lv_obj_set_height(chip, LV_PCT(100));
    lv_obj_add_style(chip, &s_chip_button, 0);
    lv_obj_add_style(chip, &s_tile_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(chip, &s_chip_selected, LV_STATE_CHECKED);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(chip, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_add_style(label, &s_subtitle_label, 0);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_center(label);
    return chip;
}

void app_ui_chip_set_selected(lv_obj_t *chip, bool selected)
{
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    if (label == NULL)
    {
        return;
    }
    if (selected)
    {
        lv_obj_add_state(chip, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(chip, LV_STATE_CHECKED);
    }
    lv_obj_set_style_text_color(label,
                                lv_color_hex(selected ? COLOR_TEXT :
                                        COLOR_MUTED), 0);
}

void app_ui_click_only(lv_obj_t *obj)
{
    lv_obj_set_press_lock(obj, false);
}

lv_obj_t *app_ui_add_icon_tile(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, const char *label,
                               lv_event_cb_t callback, void *user_data)
{
    _app_ui_styles_init();
    lv_obj_t *tile = lv_button_create(parent);
    if (tile == NULL)
    {
        return NULL;
    }
    app_ui_click_only(tile);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_add_style(tile, &s_icon_tile, 0);
    lv_obj_add_style(tile, &s_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(tile, APP_UI_RADIUS_CARD, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *chip = lv_obj_create(tile);
    if (chip != NULL)
    {
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, APP_UI_TILE_WELL, APP_UI_TILE_WELL);
        lv_obj_add_style(chip, &s_icon_chip, 0);
        app_ui_make_passive(chip, false);

        const lv_image_dsc_t *descriptor = NULL;
        if (image_id != 0U &&
                app_manager_get_image(image_id, &descriptor) == ESP_OK &&
                descriptor != NULL)
        {
            lv_obj_t *image = lv_image_create(chip);
            if (image != NULL)
            {
                /* The record is drawn 1:1 and clipped to this window. */
                lv_obj_set_size(image, 72, 72);
                lv_image_set_src(image, descriptor);
                app_ui_make_passive(image, false);
                lv_obj_center(image);
            }
        }
        else
        {
            lv_obj_t *symbol = lv_label_create(chip);
            if (symbol != NULL)
            {
                lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
                lv_obj_set_style_text_color(symbol, lv_color_hex(COLOR_TEXT), 0);
                lv_label_set_text(symbol, fallback_symbol != NULL ?
                                  fallback_symbol : LV_SYMBOL_IMAGE);
                lv_obj_center(symbol);
            }
        }
    }

    lv_obj_t *caption = lv_label_create(tile);
    if (caption != NULL)
    {
        lv_obj_set_width(caption, LV_PCT(100));
        lv_obj_set_height(caption, LV_SIZE_CONTENT);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_style(caption, &s_subtitle_label, 0);
        lv_obj_set_style_text_font(caption, app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(caption, label != NULL ? label : "");
    }
    return tile;
}

lv_obj_t *app_ui_add_empty_state(lv_obj_t *parent, const char *title,
                                 const char *subtitle)
{
    _app_ui_styles_init();
    lv_obj_t *block = lv_obj_create(parent);
    if (block == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(block);
    lv_obj_set_width(block, LV_PCT(100));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(block, &s_empty_state, 0);
    app_ui_make_passive(block, false);

    if (title != NULL && title[0] != '\0')
    {
        lv_obj_t *title_label = lv_label_create(block);
        lv_obj_set_width(title_label, LV_PCT(100));
        lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_style(title_label, &s_title_label, 0);
        lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_BODY),
                                   0);
        lv_label_set_text(title_label, title);
    }
    if (subtitle != NULL && subtitle[0] != '\0')
    {
        lv_obj_t *subtitle_label = lv_label_create(block);
        lv_obj_set_width(subtitle_label, LV_PCT(100));
        lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_style(subtitle_label, &s_subtitle_label, 0);
        lv_obj_set_style_text_font(subtitle_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(subtitle_label, subtitle);
    }
    return block;
}

lv_obj_t *app_ui_group_create(lv_obj_t *parent)
{
    if (parent == NULL)
    {
        return NULL;
    }
    _app_ui_styles_init();
    lv_obj_t *group = lv_obj_create(parent);
    if (group == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(group);
    lv_obj_set_width(group, LV_PCT(100));
    lv_obj_set_height(group, LV_SIZE_CONTENT);
    lv_obj_add_style(group, &s_group, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    app_ui_make_passive(group, false);
    return group;
}

static lv_obj_t *_app_ui_group_row(lv_obj_t *group, int32_t height)
{
    if (group == NULL)
    {
        return NULL;
    }
    _app_ui_styles_init();
    const bool divider = lv_obj_get_child_count(group) > 0U;
    lv_obj_t *row = lv_button_create(group);
    if (row == NULL)
    {
        return NULL;
    }
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_add_style(row, &s_group_row, 0);
    if (divider)
    {
        lv_obj_add_style(row, &s_group_divider, 0);
    }
    lv_obj_add_style(row, &s_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *_app_ui_group_nav(lv_obj_t *group, uint32_t image_id,
                                   const char *symbol,
                                   const char *title, const char *subtitle,
                                   bool danger, bool navigation,
                                   lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *row = _app_ui_group_row(group, subtitle != NULL ?
                                      APP_UI_ROW_HEIGHT_TALL :
                                      APP_UI_ROW_HEIGHT);
    if (row == NULL)
    {
        return NULL;
    }
    if (image_id != 0U || symbol != NULL)
    {
        (void)_app_ui_row_well(row, image_id, symbol, danger);
    }
    (void)_app_ui_row_text(row, title, subtitle,
                           danger ? APP_UI_COLOR_WARNING : COLOR_TEXT, false,
                           NULL, NULL);
    if (navigation)
    {
        _app_ui_chevron(row);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

lv_obj_t *app_ui_group_add_nav(lv_obj_t *group, const char *symbol,
                               const char *title, const char *subtitle,
                               lv_event_cb_t callback, void *user_data)
{
    return _app_ui_group_nav(group, 0U, symbol, title, subtitle, false,
                             true, callback, user_data);
}

lv_obj_t *app_ui_group_add_nav_image(lv_obj_t *group, uint32_t image_id,
                                     const char *fallback_symbol,
                                     const char *title, const char *subtitle,
                                     lv_event_cb_t callback, void *user_data)
{
    return _app_ui_group_nav(group, image_id, fallback_symbol, title, subtitle,
                             false, true, callback, user_data);
}

lv_obj_t *app_ui_group_add_command(lv_obj_t *group, const char *symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data)
{
    return _app_ui_group_nav(group, 0U, symbol, title, subtitle, false,
                             false, callback, user_data);
}

lv_obj_t *app_ui_group_add_command_image(lv_obj_t *group, uint32_t image_id,
        const char *fallback_symbol,
        const char *title,
        const char *subtitle,
        lv_event_cb_t callback,
        void *user_data)
{
    return _app_ui_group_nav(group, image_id, fallback_symbol, title, subtitle,
                             false, false, callback, user_data);
}

lv_obj_t *app_ui_group_add_danger(lv_obj_t *group, const char *symbol,
                                  const char *title, const char *subtitle,
                                  lv_event_cb_t callback, void *user_data)
{
    return _app_ui_group_nav(group, 0U, symbol, title, subtitle, true, true,
                             callback, user_data);
}

lv_obj_t *app_ui_group_add_danger_image(lv_obj_t *group, uint32_t image_id,
                                        const char *fallback_symbol,
                                        const char *title, const char *subtitle,
                                        lv_event_cb_t callback,
                                        void *user_data)
{
    return _app_ui_group_nav(group, image_id, fallback_symbol, title, subtitle,
                             true, true, callback, user_data);
}

/* Single-line entry: title on the left, the live summary right-aligned as the
 * row value, chevron last. Two-line rows are reserved for subtitles that carry
 * explanatory copy. */
lv_obj_t *app_ui_group_add_entry(lv_obj_t *group, const char *title,
                                 lv_obj_t **summary_out,
                                 lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *row = _app_ui_group_row(group, APP_UI_ROW_HEIGHT);
    if (row == NULL)
    {
        return NULL;
    }
    lv_obj_t *title_label = lv_label_create(row);
    lv_obj_add_style(title_label, &s_title_label, 0);
    lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_BODY),
                               0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(title_label, title != NULL ? title : "");
    /* DOT only truncates once the label has a width; the summary keeps the
     * rest of the row. */
    lv_obj_set_style_max_width(title_label, LV_PCT(50), 0);
    app_ui_make_passive(title_label, false);

    const lv_font_t *font = app_ui_font(APP_THEME_FONT_SMALL);
    lv_obj_t *summary = lv_label_create(row);
    lv_obj_set_width(summary, 0);
    lv_obj_set_flex_grow(summary, 1);
    lv_obj_set_height(summary, lv_font_get_line_height(font));
    lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
    lv_obj_add_style(summary, &s_value_label, 0);
    lv_obj_set_style_text_font(summary, font, 0);
    lv_label_set_text(summary, "");
    app_ui_make_passive(summary, false);
    if (summary_out != NULL)
    {
        *summary_out = summary;
    }
    _app_ui_chevron(row);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

/* Name on the left, muted value right-aligned in the remaining width and
 * bounded to one line so a long value ellipsizes instead of wrapping. */
static lv_obj_t *_app_ui_value_pair(lv_obj_t *row, const char *name,
                                    const char *value)
{
    lv_obj_t *name_label = lv_label_create(row);
    lv_obj_add_style(name_label, &s_title_label, 0);
    lv_obj_set_style_text_font(name_label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(name_label, name != NULL ? name : "");
    lv_obj_set_style_max_width(name_label, LV_PCT(50), 0);
    app_ui_make_passive(name_label, false);

    const lv_font_t *font = app_ui_font(APP_THEME_FONT_BODY);
    lv_obj_t *value_label = lv_label_create(row);
    lv_obj_set_width(value_label, 0);
    lv_obj_set_flex_grow(value_label, 1);
    lv_obj_set_height(value_label, lv_font_get_line_height(font));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_add_style(value_label, &s_value_label, 0);
    lv_obj_set_style_text_font(value_label, font, 0);
    lv_label_set_text(value_label, value != NULL ? value : "");
    app_ui_make_passive(value_label, false);
    return value_label;
}

lv_obj_t *app_ui_group_add_value(lv_obj_t *group, const char *name,
                                 const char *value, lv_obj_t **value_out)
{
    lv_obj_t *row = _app_ui_group_row(group, APP_UI_VALUE_ROW_HEIGHT);
    if (row == NULL)
    {
        return NULL;
    }
    /* Read-only: drags over it belong to the page scroll. */
    app_ui_make_passive(row, false);
    lv_obj_t *value_label = _app_ui_value_pair(row, name, value);
    if (value_out != NULL)
    {
        *value_out = value_label;
    }
    return row;
}

static void _app_ui_group_switch_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            lv_event_get_target(event) != lv_event_get_current_target(event))
    {
        return;
    }
    lv_obj_t *toggle = lv_event_get_user_data(event);
    if (toggle != NULL && lv_obj_is_valid(toggle))
    {
        if (lv_obj_has_state(toggle, LV_STATE_CHECKED))
        {
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        }
        (void)lv_obj_send_event(toggle, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

/* iOS-proportioned switch: L3 track, azure when on, ink knob inset 4 px. */
static lv_obj_t *_app_ui_switch(lv_obj_t *row, lv_event_cb_t callback,
                                void *user_data)
{
    lv_obj_t *toggle = lv_switch_create(row);
    app_ui_click_only(toggle);
    lv_obj_set_size(toggle, 60, 34);
    lv_obj_add_style(toggle, &s_switch_main, 0);
    lv_obj_add_style(toggle, &s_switch_indicator_checked,
                     LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(toggle, &s_switch_knob, LV_PART_KNOB);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(toggle, callback, LV_EVENT_VALUE_CHANGED,
                            user_data);
    }
    return toggle;
}

lv_obj_t *app_ui_group_add_switch(lv_obj_t *group, const char *title,
                                  const char *subtitle,
                                  lv_event_cb_t callback, void *user_data,
                                  lv_obj_t **switch_out)
{
    lv_obj_t *row = _app_ui_group_row(group, subtitle != NULL ?
                                      APP_UI_ROW_HEIGHT_TALL :
                                      APP_UI_ROW_HEIGHT);
    if (row == NULL)
    {
        return NULL;
    }
    (void)_app_ui_row_text(row, title, subtitle, COLOR_TEXT, false, NULL, NULL);
    lv_obj_t *toggle = _app_ui_switch(row, callback, user_data);
    lv_obj_add_event_cb(row, _app_ui_group_switch_event, LV_EVENT_CLICKED,
                        toggle);
    if (switch_out != NULL)
    {
        *switch_out = toggle;
    }
    return row;
}

lv_obj_t *app_ui_page_set_action(app_ui_page_t *page, uint32_t image_id,
                                 const char *fallback_symbol,
                                 lv_event_cb_t callback, void *user_data)
{
    if (page == NULL || page->header == NULL)
    {
        return NULL;
    }
    _app_ui_styles_init();
    lv_obj_t *button = lv_button_create(page->header);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    lv_obj_set_size(button, 42, 42);
    lv_obj_set_ignore_layout(button, true);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
    lv_obj_add_style(button, &s_page_action_button, 0);
    lv_obj_add_style(button, &s_surface_pressed, LV_STATE_PRESSED);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *icon = app_ui_image(button, image_id,
                                  fallback_symbol != NULL ? fallback_symbol :
                                  LV_SYMBOL_RIGHT, APP_UI_ROW_GLYPH);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, COLOR_TEXT);
        lv_obj_center(icon);
    }
    return button;
}

lv_obj_t *app_ui_add_pill(lv_obj_t *parent, const char *text,
                          app_ui_status_t status)
{
    _app_ui_styles_init();
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_style(pill, &s_pill, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(pill, false);

    lv_obj_t *dot = lv_obj_create(pill);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    app_ui_make_passive(dot, false);

    lv_obj_t *label = lv_label_create(pill);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_make_passive(label, false);
    app_ui_pill_set(pill, text, status);
    return pill;
}

void app_ui_pill_set(lv_obj_t *pill, const char *text, app_ui_status_t status)
{
    lv_obj_t *dot = pill != NULL ? lv_obj_get_child(pill, 0) : NULL;
    lv_obj_t *label = pill != NULL ? lv_obj_get_child(pill, 1) : NULL;
    if (dot == NULL || label == NULL)
    {
        return;
    }
    uint32_t ink = COLOR_MUTED;
    uint32_t fill = COLOR_SURFACE;
    switch (status)
    {
    case APP_UI_STATUS_ACCENT:
        ink = APP_UI_COLOR_RAIN;
        fill = APP_UI_COLOR_RAIN_TINT;
        break;
    case APP_UI_STATUS_SUCCESS:
        ink = APP_UI_COLOR_SUCCESS;
        fill = APP_UI_COLOR_SUCCESS_TINT;
        break;
    case APP_UI_STATUS_WARNING:
        ink = APP_UI_COLOR_SUN;
        fill = APP_UI_COLOR_SUN_TINT;
        break;
    case APP_UI_STATUS_ERROR:
        ink = APP_UI_COLOR_WARNING;
        fill = APP_UI_COLOR_WARNING_TINT;
        break;
    case APP_UI_STATUS_NEUTRAL:
    default:
        break;
    }
    lv_obj_set_style_bg_color(pill, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(ink), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ink), 0);
    app_ui_label_set_text_if(label, text != NULL ? text : "");
}

static void _app_ui_sheet_delete_async(void *object)
{
    if (object != NULL && lv_obj_is_valid((lv_obj_t *)object))
    {
        lv_obj_delete((lv_obj_t *)object);
    }
}

lv_obj_t *app_ui_sheet_open(lv_obj_t *parent, const char *title,
                            const char *message)
{
    if (parent == NULL)
    {
        return NULL;
    }
    _app_ui_styles_init();
    lv_obj_t *scrim = lv_obj_create(parent);
    if (scrim == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_align(scrim, LV_ALIGN_TOP_LEFT, 0, 0);
    /* Float above the page's flex tree instead of joining its flow. */
    lv_obj_set_ignore_layout(scrim, true);
    lv_obj_set_floating(scrim, true);
    lv_obj_add_style(scrim, &s_sheet_scrim, 0);
    /* Swallow every touch so the covered page receives none. */
    lv_obj_set_clickable(scrim, true);
    lv_obj_set_scrollable(scrim, false);
    lv_obj_set_click_focusable(scrim, false);

    lv_obj_t *panel = lv_obj_create(scrim);
    if (panel == NULL)
    {
        lv_obj_delete(scrim);
        return NULL;
    }
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, APP_UI_SHEET_MAX_HEIGHT, 0);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_style(panel, &s_sheet_panel, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    /* Keep taps on the panel from falling through to the scrim. */
    lv_obj_set_clickable(panel, true);
    lv_obj_set_scrollable(panel, true);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *title_label = lv_label_create(panel);
    if (title_label != NULL)
    {
        lv_obj_set_width(title_label, LV_PCT(100));
        lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(title_label, &s_title_label, 0);
        lv_obj_set_style_text_font(title_label, app_ui_font(APP_THEME_FONT_HEAD),
                                   0);
        lv_label_set_text(title_label, title != NULL ? title : "");
    }
    if (message != NULL && message[0] != '\0')
    {
        lv_obj_t *message_label = lv_label_create(panel);
        if (message_label != NULL)
        {
            lv_obj_set_width(message_label, LV_PCT(100));
            lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
            lv_obj_add_style(message_label, &s_subtitle_label, 0);
            lv_obj_set_style_text_font(message_label,
                                       app_ui_font(APP_THEME_FONT_SMALL), 0);
            lv_obj_set_style_text_line_space(message_label, 4, 0);
            lv_obj_set_style_pad_bottom(message_label, 8, 0);
            lv_label_set_text(message_label, message);
        }
    }
    return scrim;
}

lv_obj_t *app_ui_sheet_add_action(lv_obj_t *sheet, const char *text,
                                  bool danger, lv_event_cb_t callback,
                                  void *user_data)
{
    if (sheet == NULL)
    {
        return NULL;
    }
    lv_obj_t *panel = lv_obj_get_child(sheet, 0);
    if (panel == NULL)
    {
        return NULL;
    }
    _app_ui_styles_init();
    lv_obj_t *button = lv_button_create(panel);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 56);
    lv_obj_add_style(button, &s_button, 0);
    lv_obj_add_style(button, &s_tile_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
    if (danger)
    {
        app_ui_button_set_kind(button, APP_UI_BUTTON_DANGER);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = lv_label_create(button);
    if (label != NULL)
    {
        lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
        lv_label_set_text(label, text != NULL ? text : "");
        lv_obj_center(label);
    }
    return button;
}

void app_ui_sheet_dismiss(lv_obj_t *sheet)
{
    if (sheet == NULL)
    {
        return;
    }
    lv_obj_set_hidden(sheet, true);
    (void)lv_async_call(_app_ui_sheet_delete_async, sheet);
}

lv_obj_t *app_ui_ring_create(lv_obj_t *parent, int32_t size, int32_t width,
                             uint32_t track_color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    if (arc == NULL)
    {
        return NULL;
    }
    lv_obj_set_size(arc, size, size);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(track_color), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    lv_arc_set_rotation(arc, 270);
    app_ui_make_passive(arc, false);
    return arc;
}

lv_obj_t *app_ui_add_value_row(lv_obj_t *parent, const char *name,
                               const char *value, lv_obj_t **value_label)
{
    _app_ui_styles_init();
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, APP_UI_ROW_HEIGHT, 0);
    lv_obj_add_style(row, &s_card_row, 0);
    /* The card-row padding alone leaves a single-line value high in the 60 px
     * minimum height; symmetric padding centres it and still lets a wrapped
     * value grow the row. */
    const int32_t value_line = lv_font_get_line_height(
                                   app_ui_font(APP_THEME_FONT_BODY));
    const int32_t value_pad = (APP_UI_ROW_HEIGHT - value_line) / 2;
    lv_obj_set_style_pad_top(row, value_pad, 0);
    lv_obj_set_style_pad_bottom(row, value_pad, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(row, false);

    lv_obj_t *name_label = lv_label_create(row);
    lv_obj_add_style(name_label, &s_title_label, 0);
    lv_obj_set_style_text_font(name_label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(name_label, name != NULL ? name : "");
    lv_obj_set_style_max_width(name_label, LV_PCT(50), 0);

    lv_obj_t *current = lv_label_create(row);
    lv_obj_set_width(current, 0);
    lv_obj_set_flex_grow(current, 1);
    lv_label_set_long_mode(current, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(current, &s_value_label, 0);
    lv_obj_set_style_text_font(current, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(current, value != NULL ? value : "");
    if (value_label != NULL)
    {
        *value_label = current;
    }
    return row;
}

static void _app_ui_switch_row_event(lv_event_t *event)
{
    /* Only react when the row itself was pressed; a CLICKED bubbling up from
     * the switch would otherwise toggle it back. */
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            lv_event_get_target(event) != lv_event_get_current_target(event))
    {
        return;
    }
    lv_obj_t *toggle = lv_event_get_user_data(event);
    if (toggle != NULL && lv_obj_is_valid(toggle))
    {
        if (lv_obj_has_state(toggle, LV_STATE_CHECKED))
        {
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        }
        /* LVGL only emits VALUE_CHANGED from the widget's own RELEASED path;
         * a programmatic toggle must notify the page callback explicitly. */
        (void)lv_obj_send_event(toggle, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

lv_obj_t *app_ui_add_switch_row(lv_obj_t *parent, const char *title,
                                const char *subtitle, lv_event_cb_t callback,
                                void *user_data, lv_obj_t **switch_out)
{
    _app_ui_styles_init();
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, subtitle != NULL ?
                                APP_UI_ROW_HEIGHT_TALL : APP_UI_ROW_HEIGHT, 0);
    lv_obj_add_style(row, &s_card_row, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(row, false);
    /* The whole row is the touch target; it forwards to the switch so the
     * small toggle never becomes a dead-zone hit. */
    lv_obj_set_clickable(row, true);
    lv_obj_set_press_lock(row, false);
    lv_obj_add_style(row, &s_surface_pressed, LV_STATE_PRESSED);

    (void)_app_ui_row_text(row, title, subtitle, COLOR_TEXT, true, NULL, NULL);
    lv_obj_t *toggle = _app_ui_switch(row, callback, user_data);
    lv_obj_add_event_cb(row, _app_ui_switch_row_event, LV_EVENT_CLICKED,
                        toggle);
    if (switch_out != NULL)
    {
        *switch_out = toggle;
    }
    return row;
}

lv_obj_t *app_ui_add_body_label(lv_obj_t *parent, const char *text)
{
    _app_ui_styles_init();
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(label, &s_body_label, 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
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
        color = APP_UI_COLOR_RAIN;
        break;
    case APP_UI_STATUS_SUCCESS:
        color = APP_UI_COLOR_SUCCESS;
        break;
    case APP_UI_STATUS_WARNING:
        color = APP_UI_COLOR_SUN;
        break;
    case APP_UI_STATUS_ERROR:
        color = APP_UI_COLOR_WARNING;
        break;
    case APP_UI_STATUS_NEUTRAL:
    default:
        break;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    app_ui_label_set_text_if(label, text != NULL ? text : "");
}

void app_ui_label_set_text_if(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL)
    {
        return;
    }
    /* lv_label_set_text always reallocates and re-renders; periodic
     * refreshers compare against the current text instead. */
    const char *current = lv_label_get_text(label);
    if (current != NULL && strcmp(current, text) == 0)
    {
        return;
    }
    lv_label_set_text(label, text);
}

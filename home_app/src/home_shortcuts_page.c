#define DBG_TAG "home_shortcuts_page"
#include "mt_log.h"

#include "home_shortcuts_page.h"

#include "app_ui.h"
#include "app_ui_theme.h"
#include "home_shortcuts.h"

#include <stdio.h>
#include <string.h>

#define HOME_SHORTCUT_CARD_COLUMNS 3U
#define HOME_SHORTCUT_CARD_GAP     8
#define HOME_SHORTCUT_CARD_HEIGHT  88
#define HOME_SHORTCUT_CARD_IMAGE   52
#define HOME_SHORTCUT_BADGE_SIZE   18
#define HOME_SHORTCUT_DISABLED_OPA 128
/* Launcher SVG records are authored at 72 px and scaled into the card well. */
#define HOME_SHORTCUT_ARTWORK     72

typedef struct home_shortcuts_page_state home_shortcuts_page_state_t;

typedef struct home_shortcut_choice
{
    home_shortcuts_page_state_t *owner;
    const app_manager_app_desc_t *app;
    lv_obj_t *card;
    lv_obj_t *badge;
    lv_obj_t *caption;
} home_shortcut_choice_t;

struct home_shortcuts_page_state
{
    app_ui_page_t page;
    lv_obj_t *summary;
    lv_obj_t *notice;
    lv_obj_t *grid;
    home_shortcut_choice_t choices[HOME_SHORTCUT_CATALOG_MAX];
    size_t choice_count;
};

_Static_assert(sizeof(home_shortcuts_page_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Home shortcuts page state exceeds the lifecycle arena slot");

static lv_style_t s_card;
static lv_style_t s_card_pressed;
static lv_style_t s_card_selected;
static lv_style_t s_card_selected_pressed;
static bool s_styles_ready;

static void _home_shortcut_styles_init(void)
{
    if (s_styles_ready)
    {
        return;
    }

    lv_style_init(&s_card);
    lv_style_set_radius(&s_card, APP_UI_RADIUS_CARD);
    lv_style_set_bg_color(&s_card, lv_color_hex(APP_UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_color(&s_card, lv_color_hex(APP_UI_COLOR_SURFACE));
    lv_style_set_border_opa(&s_card, LV_OPA_COVER);
    lv_style_set_shadow_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, 3);
    lv_style_set_pad_row(&s_card, 2);

    lv_style_init(&s_card_pressed);
    lv_style_set_bg_color(&s_card_pressed, lv_color_hex(APP_UI_COLOR_SURFACE_HI));
    lv_style_set_bg_opa(&s_card_pressed, LV_OPA_COVER);

    lv_style_init(&s_card_selected);
    lv_style_set_bg_color(&s_card_selected, lv_color_hex(APP_UI_COLOR_RAIN_TINT));
    lv_style_set_bg_opa(&s_card_selected, LV_OPA_COVER);
    lv_style_set_border_color(&s_card_selected,
                              lv_color_hex(APP_UI_COLOR_RAIN));

    lv_style_init(&s_card_selected_pressed);
    lv_style_set_bg_color(&s_card_selected_pressed,
                          lv_color_hex(APP_UI_COLOR_TRACK));
    lv_style_set_border_color(&s_card_selected_pressed,
                              lv_color_hex(APP_UI_COLOR_RAIN_HI));

    s_styles_ready = true;
}

static lv_obj_t *_home_shortcuts_small_label(lv_obj_t *parent,
        const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static void _home_shortcuts_notice(home_shortcuts_page_state_t *state,
                                   const char *text, app_ui_status_t status)
{
    if (state->notice == NULL || text == NULL)
    {
        return;
    }
    lv_obj_set_hidden(state->notice, false);
    app_ui_set_status_text(state->notice, text, status);
}

static void _home_shortcuts_clear_notice(home_shortcuts_page_state_t *state)
{
    if (state->notice != NULL)
    {
        lv_obj_set_hidden(state->notice, true);
    }
}

static void _home_shortcuts_limit_notice(home_shortcuts_page_state_t *state)
{
    char text[32];
    (void)snprintf(text, sizeof(text), "最多选择 %u 个应用",
                   (unsigned)HOME_SHORTCUT_SLOTS);
    _home_shortcuts_notice(state, text, APP_UI_STATUS_WARNING);
}

static size_t _home_shortcuts_selected_count(
    const home_shortcuts_page_state_t *state)
{
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    if (!prefs->configured)
    {
        const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
        const size_t catalog_count = home_shortcut_catalog(catalog,
            HOME_SHORTCUT_CATALOG_MAX);
        const app_manager_app_desc_t *effective[HOME_SHORTCUT_SLOTS];
        return home_shortcut_effective(prefs, catalog, catalog_count,
                                       effective, HOME_SHORTCUT_SLOTS);
    }
    size_t count = 0U;
    for (size_t index = 0U; index < state->choice_count; ++index)
    {
        if (home_shortcuts_is_selected(prefs, state->choices[index].app->id))
        {
            ++count;
        }
    }
    return count;
}

static void _home_shortcuts_sync(home_shortcuts_page_state_t *state)
{
    const size_t selected_count = _home_shortcuts_selected_count(state);
    char text[32];
    (void)snprintf(text, sizeof(text), "已选择 %u/%u",
                   (unsigned)selected_count,
                   (unsigned)HOME_SHORTCUT_SLOTS);
    if (state->summary != NULL)
    {
        app_ui_set_status_text(state->summary, text,
                               selected_count == HOME_SHORTCUT_SLOTS ?
                               APP_UI_STATUS_ACCENT : APP_UI_STATUS_NEUTRAL);
    }

    const bool full = selected_count >= HOME_SHORTCUT_SLOTS;
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    for (size_t index = 0U; index < state->choice_count; ++index)
    {
        home_shortcut_choice_t *choice = &state->choices[index];
        if (choice->card == NULL)
        {
            continue;
        }
        const bool selected = home_shortcuts_is_selected(prefs,
            choice->app->id);
        if (selected)
        {
            lv_obj_add_state(choice->card, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(choice->card, LV_STATE_CHECKED);
        }
        if (choice->badge != NULL)
        {
            lv_obj_set_hidden(choice->badge, !selected);
        }
        if (choice->caption != NULL)
        {
            lv_obj_set_style_text_color(choice->caption,
                                        lv_color_hex(selected ?
                                                APP_UI_COLOR_TEXT :
                                                APP_UI_COLOR_MUTED), 0);
        }
        if (!selected && full)
        {
            lv_obj_add_state(choice->card, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_remove_state(choice->card, LV_STATE_DISABLED);
        }
    }
}

static void _home_shortcut_card_clicked(lv_event_t *event)
{
    home_shortcut_choice_t *choice = lv_event_get_user_data(event);
    if (choice == NULL || choice->owner == NULL || choice->app == NULL ||
            lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    const bool selected = home_shortcuts_is_selected(prefs, choice->app->id);
    if (!selected && _home_shortcuts_selected_count(choice->owner) >=
            HOME_SHORTCUT_SLOTS)
    {
        _home_shortcuts_limit_notice(choice->owner);
        _home_shortcuts_sync(choice->owner);
        return;
    }
    const esp_err_t result = home_shortcuts_set_selected(choice->app,
        !selected);
    if (result == ESP_ERR_INVALID_STATE)
    {
        _home_shortcuts_limit_notice(choice->owner);
    }
    else if (result != ESP_OK)
    {
        _home_shortcuts_notice(choice->owner, "保存失败，重启后可能丢失",
                               APP_UI_STATUS_ERROR);
    }
    else
    {
        _home_shortcuts_clear_notice(choice->owner);
    }
    _home_shortcuts_sync(choice->owner);
}

static lv_obj_t *_home_shortcut_card(lv_obj_t *parent,
                                     const app_manager_app_desc_t *app,
                                     home_shortcut_choice_t *choice)
{
    lv_obj_t *card = lv_button_create(parent);
    if (card == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(card);
    app_ui_click_only(card);
    lv_obj_set_width(card, 0);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, HOME_SHORTCUT_CARD_HEIGHT);
    lv_obj_add_style(card, &s_card, 0);
    lv_obj_add_style(card, &s_card_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(card, &s_card_selected, LV_STATE_CHECKED);
    lv_obj_add_style(card, &s_card_selected_pressed,
                     LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_opa(card, HOME_SHORTCUT_DISABLED_OPA, LV_STATE_DISABLED);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(card, _home_shortcut_card_clicked, LV_EVENT_CLICKED,
                        choice);

    lv_obj_t *well = lv_obj_create(card);
    lv_obj_remove_style_all(well);
    lv_obj_set_size(well, HOME_SHORTCUT_CARD_IMAGE, HOME_SHORTCUT_CARD_IMAGE);
    lv_obj_set_style_radius(well, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(well, lv_color_hex(APP_UI_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
    app_ui_make_passive(well, false);

    const lv_image_dsc_t *descriptor = NULL;
    if (app->icon_id != 0U &&
            app_manager_get_image(app->icon_id, &descriptor) == ESP_OK &&
            descriptor != NULL)
    {
        lv_obj_t *image = lv_image_create(well);
        lv_obj_set_size(image, HOME_SHORTCUT_CARD_IMAGE,
                        HOME_SHORTCUT_CARD_IMAGE);
        lv_image_set_src(image, descriptor);
        lv_image_set_scale(image,
                           (uint32_t)HOME_SHORTCUT_CARD_IMAGE * 256U /
                           HOME_SHORTCUT_ARTWORK);
        app_ui_make_passive(image, false);
        lv_obj_center(image);
    }
    else
    {
        lv_obj_t *symbol = lv_label_create(well);
        lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(APP_UI_COLOR_TEXT), 0);
        lv_label_set_text(symbol, LV_SYMBOL_IMAGE);
        app_ui_make_passive(symbol, false);
        lv_obj_center(symbol);
    }

    lv_obj_t *caption = lv_label_create(card);
    lv_obj_set_width(caption, LV_PCT(100));
    lv_obj_set_height(caption, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(caption, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_label_set_text(caption, app->display_name != NULL ?
                      app->display_name : app->name);
    app_ui_make_passive(caption, false);

    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, HOME_SHORTCUT_BADGE_SIZE,
                    HOME_SHORTCUT_BADGE_SIZE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_ignore_layout(badge, true);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -3, 3);
    app_ui_make_passive(badge, false);
    lv_obj_t *badge_glyph = lv_label_create(badge);
    lv_obj_set_style_text_font(badge_glyph, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(badge_glyph,
                                lv_color_hex(APP_UI_COLOR_ON_ACCENT), 0);
    lv_label_set_text(badge_glyph, LV_SYMBOL_OK);
    app_ui_make_passive(badge_glyph, false);
    lv_obj_center(badge_glyph);
    lv_obj_set_hidden(badge, true);

    choice->card = card;
    choice->badge = badge;
    choice->caption = caption;
    return card;
}

static lv_obj_t *_home_shortcut_row(lv_obj_t *grid)
{
    lv_obj_t *row = lv_obj_create(grid);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, HOME_SHORTCUT_CARD_GAP, 0);
    app_ui_make_passive(row, false);
    return row;
}

static void _home_shortcut_row_spacer(lv_obj_t *row)
{
    lv_obj_t *spacer = lv_obj_create(row);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, 0);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    app_ui_make_passive(spacer, false);
}

static void _home_shortcuts_build_grid(home_shortcuts_page_state_t *state,
                                       const app_manager_app_desc_t *const *catalog,
                                       size_t catalog_count)
{
    lv_obj_t *grid = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, HOME_SHORTCUT_CARD_GAP, 0);
    app_ui_make_passive(grid, false);
    state->grid = grid;

    for (size_t index = 0U; index < catalog_count;
            index += HOME_SHORTCUT_CARD_COLUMNS)
    {
        lv_obj_t *row = _home_shortcut_row(grid);
        for (size_t column = 0U; column < HOME_SHORTCUT_CARD_COLUMNS;
                ++column)
        {
            const size_t item = index + column;
            if (item >= catalog_count)
            {
                _home_shortcut_row_spacer(row);
                continue;
            }
            home_shortcut_choice_t *choice =
                &state->choices[state->choice_count++];
            choice->owner = state;
            choice->app = catalog[item];
            (void)_home_shortcut_card(row, choice->app, choice);
        }
    }
}

static void _home_shortcuts_page_mount(
    const app_manager_page_context_t *context)
{
    home_shortcuts_page_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    _home_shortcut_styles_init();
    const esp_err_t load_result = home_shortcuts_ensure_loaded();

    app_ui_page_create(&state->page, "主页快捷方式", true);
    char subtitle[32];
    (void)snprintf(subtitle, sizeof(subtitle), "最多选择 %u 个应用",
                   (unsigned)HOME_SHORTCUT_SLOTS);
    app_ui_page_set_subtitle(&state->page, subtitle);
    lv_obj_t *content = state->page.content;
    lv_obj_set_style_pad_top(content, 4, 0);
    lv_obj_set_style_pad_bottom(content, 16, 0);
    char summary[32];
    (void)snprintf(summary, sizeof(summary), "已选择 0/%u",
                   (unsigned)HOME_SHORTCUT_SLOTS);
    state->summary = app_ui_add_body_label(content, summary);
    state->notice = _home_shortcuts_small_label(content, "");
    lv_obj_set_hidden(state->notice, true);
    if (load_result != ESP_OK)
    {
        _home_shortcuts_notice(state, "设置读取失败，当前使用默认选择",
                               APP_UI_STATUS_WARNING);
    }

    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t catalog_count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    if (catalog_count == 0U)
    {
        (void)app_ui_add_empty_state(content, "没有可添加的应用",
                                     "当前产品没有可自定义的 App。");
        _home_shortcuts_sync(state);
        return;
    }

    _home_shortcuts_build_grid(state, catalog, catalog_count);
    _home_shortcuts_sync(state);
}

static void _home_shortcuts_page_unmount(
    const app_manager_page_context_t *context)
{
    home_shortcuts_page_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->summary = NULL;
    state->notice = NULL;
    state->grid = NULL;
    for (size_t index = 0U; index < state->choice_count; ++index)
    {
        state->choices[index].card = NULL;
        state->choices[index].badge = NULL;
        state->choices[index].caption = NULL;
    }
    state->choice_count = 0U;
}

static const app_manager_page_ops_t s_home_shortcuts_ops =
{
    .mount = _home_shortcuts_page_mount,
    .unmount = _home_shortcuts_page_unmount,
};

const app_manager_page_definition_t home_shortcuts_page_definition =
{
    .ops = &s_home_shortcuts_ops,
    .memory_size = sizeof(home_shortcuts_page_state_t),
};

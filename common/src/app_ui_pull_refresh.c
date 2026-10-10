#include "app_ui.h"
#include "app_ui_theme.h"

#include <stdint.h>
#include <string.h>

#define APP_UI_PULL_HEIGHT   56
#define APP_UI_PULL_TRIGGER  25
#define APP_UI_PULL_ANIM_MS  180
#define APP_UI_PULL_TIMEOUT_MS 8000

static void _pull_translate(app_ui_pull_refresh_t *refresh, int32_t value)
{
    refresh->offset = value;
    if (refresh->header != NULL)
    {
        lv_obj_set_style_translate_y(refresh->header, value, 0);
    }
    if (refresh->content != NULL)
    {
        lv_obj_set_style_translate_y(refresh->content, value, 0);
    }
}

static void _pull_anim_cb(void *var, int32_t value)
{
    _pull_translate((app_ui_pull_refresh_t *)var, value);
}

static void _pull_animate_to(app_ui_pull_refresh_t *refresh, int32_t target)
{
    if (refresh->offset == target)
    {
        return;
    }
    (void)lv_anim_delete(refresh, _pull_anim_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, refresh);
    lv_anim_set_values(&animation, refresh->offset, target);
    lv_anim_set_duration(&animation, APP_UI_PULL_ANIM_MS);
    lv_anim_set_exec_cb(&animation, _pull_anim_cb);
    (void)lv_anim_start(&animation);
}

static void _pull_set_hint(app_ui_pull_refresh_t *refresh, const char *text)
{
    if (refresh->hint_label != NULL)
    {
        lv_label_set_text(refresh->hint_label, text);
    }
}

static void _pull_timeout_cb(lv_timer_t *timer)
{
    app_ui_pull_refresh_t *refresh = lv_timer_get_user_data(timer);
    if (refresh != NULL && refresh->refreshing)
    {
        app_ui_pull_refresh_set_refreshing(refresh, false);
    }
}

static void _pull_arm_timeout(app_ui_pull_refresh_t *refresh)
{
    if (refresh->timeout != NULL)
    {
        lv_timer_delete((lv_timer_t *)refresh->timeout);
    }
    refresh->timeout = lv_timer_create(_pull_timeout_cb,
                                       APP_UI_PULL_TIMEOUT_MS, refresh);
}

static void _pull_event(lv_event_t *event)
{
    app_ui_pull_refresh_t *refresh = lv_event_get_user_data(event);
    if (refresh == NULL || refresh->content == NULL)
    {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED)
    {
        lv_indev_t *indev = lv_event_get_indev(event);
        if (indev == NULL)
        {
            return;
        }
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        refresh->start_y = point.y;
        refresh->armed = false;
        refresh->tracking = !refresh->refreshing &&
                            lv_obj_get_scroll_y(refresh->content) == 0;
    }
    else if (code == LV_EVENT_PRESSING && refresh->tracking)
    {
        lv_indev_t *indev = lv_event_get_indev(event);
        if (indev == NULL)
        {
            return;
        }
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        int32_t dy = point.y - refresh->start_y;
        if (dy <= 0 || lv_obj_get_scroll_y(refresh->content) > 0)
        {
            _pull_translate(refresh, 0);
            refresh->armed = false;
            return;
        }
        if (dy > APP_UI_PULL_HEIGHT)
        {
            dy = APP_UI_PULL_HEIGHT;
        }
        refresh->armed = dy >= APP_UI_PULL_TRIGGER;
        _pull_set_hint(refresh, refresh->armed ? "松开刷新" : "下拉刷新");
        _pull_translate(refresh, dy);
    }
    else if (code == LV_EVENT_RELEASED)
    {
        /* A stray tap during an in-flight refresh must not collapse the hold. */
        if (refresh->refreshing)
        {
            refresh->tracking = false;
            refresh->armed = false;
            return;
        }
        if (refresh->armed && refresh->on_refresh != NULL)
        {
            refresh->refreshing = true;
            _pull_set_hint(refresh, "正在刷新…");
            _pull_translate(refresh, APP_UI_PULL_HEIGHT);
            _pull_arm_timeout(refresh);
            refresh->on_refresh(refresh->user_data);
        }
        else
        {
            _pull_animate_to(refresh, 0);
        }
        refresh->tracking = false;
        refresh->armed = false;
    }
    else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET)
    {
        if (!refresh->refreshing)
        {
            _pull_animate_to(refresh, 0);
        }
        refresh->tracking = false;
        refresh->armed = false;
    }
}

void app_ui_pull_refresh_attach(app_ui_pull_refresh_t *refresh,
                                app_ui_page_t *page,
                                app_ui_pull_refresh_cb_t on_refresh,
                                void *user_data)
{
    if (refresh == NULL || page == NULL || page->root == NULL ||
            page->content == NULL)
    {
        return;
    }
    memset(refresh, 0, sizeof(*refresh));
    refresh->root = page->root;
    refresh->header = page->header;
    refresh->content = page->content;
    refresh->on_refresh = on_refresh;
    refresh->user_data = user_data;

    lv_obj_t *hint = lv_obj_create(page->root);
    if (hint != NULL)
    {
        lv_obj_remove_style_all(hint);
        lv_obj_set_size(hint, LV_PCT(100), APP_UI_PULL_HEIGHT);
        lv_obj_set_style_bg_color(hint, lv_color_hex(APP_UI_COLOR_BACKGROUND),
                                  0);
        lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
        lv_obj_set_ignore_layout(hint, true);
        lv_obj_set_floating(hint, true);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 0);
        app_ui_make_passive(hint, false);
        /* Behind the header and content so it stays occluded until pulled. */
        lv_obj_move_to_index(hint, 0);

        lv_obj_t *label = lv_label_create(hint);
        if (label != NULL)
        {
            lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_MUTED),
                                        0);
            lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL),
                                       0);
            lv_label_set_text(label, "下拉刷新");
            lv_obj_center(label);
        }
        refresh->hint = hint;
        refresh->hint_label = label;
    }

    /* The header sits above the content; a clickable root lets a drag that
     * starts in the header area (below the control-center band) pull too. */
    lv_obj_set_clickable(page->root, true);
    lv_obj_add_event_cb(page->root, _pull_event, LV_EVENT_ALL, refresh);
    lv_obj_add_event_cb(page->content, _pull_event, LV_EVENT_ALL, refresh);
}

void app_ui_pull_refresh_set_refreshing(app_ui_pull_refresh_t *refresh,
                                        bool refreshing)
{
    if (refresh == NULL || refreshing || !refresh->refreshing)
    {
        return;
    }
    refresh->refreshing = false;
    if (refresh->timeout != NULL)
    {
        lv_timer_delete((lv_timer_t *)refresh->timeout);
        refresh->timeout = NULL;
    }
    _pull_set_hint(refresh, "下拉刷新");
    _pull_animate_to(refresh, 0);
}

void app_ui_pull_refresh_detach(app_ui_pull_refresh_t *refresh)
{
    if (refresh == NULL)
    {
        return;
    }
    (void)lv_anim_delete(refresh, _pull_anim_cb);
    if (refresh->timeout != NULL)
    {
        lv_timer_delete((lv_timer_t *)refresh->timeout);
        refresh->timeout = NULL;
    }
    /* The root, hint, header, and content are Page-owned and released with the
     * Page Screen; only the caller's state is cleared here. */
    memset(refresh, 0, sizeof(*refresh));
}

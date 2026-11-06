#include "app_ui.h"
#include "app_ui_theme.h"

#include <stdint.h>

#define APP_UI_TOAST_DWELL_MS   2500U
#define APP_UI_TOAST_ENTER_MS   140U
#define APP_UI_TOAST_EXIT_MS    120U
#define APP_UI_TOAST_SLIDE_PX   8
#define APP_UI_TOAST_WIDTH_PCT  90
#define APP_UI_TOAST_BOTTOM_INSET (APP_UI_BUTTON_HEIGHT + APP_UI_PAGE_MARGIN)

static void _toast_set_progress(lv_obj_t *toast, int32_t progress)
{
    lv_obj_set_style_opa(toast,
                         (lv_opa_t)((progress * LV_OPA_COVER) / 100), 0);
    lv_obj_set_style_translate_y(toast,
                                 ((100 - progress) * APP_UI_TOAST_SLIDE_PX) /
                                 100, 0);
}

static void _toast_animation_exec(void *object, int32_t progress)
{
    _toast_set_progress((lv_obj_t *)object, progress);
}

static void _toast_exit_completed(lv_anim_t *animation)
{
    lv_obj_t *toast = (lv_obj_t *)animation->var;
    if (toast != NULL)
    {
        lv_obj_delete(toast);
    }
}

static void _toast_start_animation(lv_obj_t *toast, bool entering)
{
    (void)lv_anim_delete(toast, NULL);
    const int32_t from = entering ? 0 : 100;
    const int32_t to = entering ? 100 : 0;
    _toast_set_progress(toast, from);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, toast);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, entering ? APP_UI_TOAST_ENTER_MS :
                         APP_UI_TOAST_EXIT_MS);
    lv_anim_set_path_cb(&animation, entering ? lv_anim_path_ease_out :
                        lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&animation, _toast_animation_exec);
    if (!entering)
    {
        lv_anim_set_completed_cb(&animation, _toast_exit_completed);
    }
    (void)lv_anim_start(&animation);
}

static void _toast_delete_event(lv_event_t *event)
{
    lv_obj_t *toast = lv_event_get_target(event);
    if (toast == NULL)
    {
        return;
    }
    lv_timer_t *timer = lv_obj_get_user_data(toast);
    if (timer != NULL)
    {
        lv_obj_set_user_data(toast, NULL);
        lv_timer_delete(timer);
    }
}

static void _toast_dwell_elapsed(lv_timer_t *timer)
{
    lv_obj_t *toast = lv_timer_get_user_data(timer);
    if (toast == NULL)
    {
        return;
    }
    lv_obj_set_user_data(toast, NULL);
    lv_timer_delete(timer);
    _toast_start_animation(toast, false);
}

static lv_obj_t *_toast_find(lv_obj_t *parent)
{
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0U; index < count; ++index)
    {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)index);
        if (child != NULL && lv_obj_get_user_flag(child, 0U))
        {
            return child;
        }
    }
    return NULL;
}

static lv_obj_t *_toast_create(lv_obj_t *parent)
{
    lv_obj_t *toast = lv_obj_create(parent);
    if (toast == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(toast);
    lv_obj_set_width(toast, LV_PCT(APP_UI_TOAST_WIDTH_PCT));
    lv_obj_set_height(toast, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(toast, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(APP_UI_COLOR_SHEET), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(APP_UI_COLOR_HAIRLINE),
                                  0);
    lv_obj_set_style_border_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(toast, 12, 0);
    lv_obj_set_style_pad_left(toast, 16, 0);
    lv_obj_set_style_pad_right(toast, 16, 0);
    lv_obj_set_ignore_layout(toast, true);
    lv_obj_set_floating(toast, true);
    app_ui_make_passive(toast, false);
    lv_obj_set_user_flag(toast, 0U, true);
    lv_obj_add_event_cb(toast, _toast_delete_event, LV_EVENT_DELETE, NULL);

    lv_obj_t *label = lv_label_create(toast);
    if (label == NULL)
    {
        lv_obj_delete(toast);
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    app_ui_make_passive(label, false);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -APP_UI_TOAST_BOTTOM_INSET);
    return toast;
}

lv_obj_t *app_ui_toast_show(lv_obj_t *parent, const char *text)
{
    if (parent == NULL || text == NULL || text[0] == '\0')
    {
        return NULL;
    }
    lv_obj_t *toast = _toast_find(parent);
    if (toast == NULL)
    {
        toast = _toast_create(parent);
        if (toast == NULL)
        {
            return NULL;
        }
    }
    else
    {
        lv_timer_t *timer = lv_obj_get_user_data(toast);
        if (timer != NULL)
        {
            lv_obj_set_user_data(toast, NULL);
            lv_timer_delete(timer);
        }
    }

    lv_obj_t *label = lv_obj_get_child(toast, 0);
    if (label == NULL)
    {
        return NULL;
    }
    app_ui_label_set_text_if(label, text);
    lv_obj_set_hidden(toast, false);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -APP_UI_TOAST_BOTTOM_INSET);
    _toast_start_animation(toast, true);

    lv_timer_t *timer = lv_timer_create(_toast_dwell_elapsed,
                                        APP_UI_TOAST_DWELL_MS, toast);
    if (timer == NULL)
    {
        /* Without a dwell timer the toast would never dismiss. */
        lv_obj_delete(toast);
        return NULL;
    }
    lv_obj_set_user_data(toast, timer);
    return toast;
}

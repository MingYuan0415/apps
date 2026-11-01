#include "music_app_internal.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_UI_COVER_RADIUS       APP_UI_RADIUS_CONTROL
#define MUSIC_UI_LIST_COVER_RADIUS  10
#define MUSIC_UI_ROW_PAD            8
#define MUSIC_UI_ORDINAL_WIDTH      30

static lv_obj_t *_music_ui_passive_box(lv_obj_t *parent);
static lv_obj_t *_music_ui_row_label(lv_obj_t *parent, const char *text,
                                     app_theme_font_id_t font,
                                     uint32_t color, bool one_line);

static void _music_ui_back_clicked(lv_event_t *event)
{
    (void)event;
    app_ui_request_back();
}

static void _music_ui_remove_page(const char *page_id);

lv_obj_t *music_ui_image(lv_obj_t *parent, uint32_t image_id,
                         const char *fallback_symbol, int32_t size)
{
    if (parent == NULL)
    {
        return NULL;
    }
    (void)image_id;
    if (size <= 0)
    {
        size = 28;
    }
#ifndef HOST_TEST
    const lv_image_dsc_t *descriptor = NULL;
    if (image_id != 0U &&
            app_manager_get_image(image_id, &descriptor) == ESP_OK &&
            descriptor != NULL && descriptor->header.w > 0U)
    {
        lv_obj_t *image = lv_image_create(parent);
        if (image == NULL)
        {
            return NULL;
        }
        lv_obj_set_size(image, size, size);
        lv_image_set_src(image, descriptor);
        lv_image_set_scale(image, (uint32_t)size * 256U /
                           descriptor->header.w);
        app_ui_make_passive(image, false);
        return image;
    }
#endif
    return app_ui_image(parent, 0U, fallback_symbol, size);
}

void music_ui_flat_button_reset(lv_obj_t *button, int32_t radius)
{
    if (button == NULL)
    {
        return;
    }
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(button, 0, 0);
    lv_obj_set_style_outline_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
}

static void _music_ui_set_x_anim(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

lv_obj_t *music_ui_marquee_label(lv_obj_t *parent, const char *text,
                                 app_theme_font_id_t font, uint32_t color)
{
    if (parent == NULL)
    {
        return NULL;
    }
    lv_obj_t *viewport = lv_obj_create(parent);
    if (viewport == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(viewport);
    lv_obj_set_width(viewport, LV_PCT(100));
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_height(viewport, lv_font_get_line_height(font_ptr));
    lv_obj_set_style_clip_corner(viewport, true, 0);
    app_ui_make_passive(viewport, false);
    lv_obj_t *label = lv_label_create(viewport);
    if (label == NULL)
    {
        return viewport;
    }
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    lv_obj_update_layout(viewport);
    const int32_t overflow = lv_obj_get_width(label) -
                             lv_obj_get_width(viewport);
    if (overflow > 0 && lv_obj_get_width(viewport) > 0)
    {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, label);
        lv_anim_set_exec_cb(&animation, _music_ui_set_x_anim);
        lv_anim_set_values(&animation, 0, -overflow);
        lv_anim_set_duration(&animation, 1600U);
        (void)lv_anim_start(&animation);
    }
    return viewport;
}

lv_obj_t *music_ui_plain_text_button(lv_obj_t *parent, const char *text,
                                     int32_t size, app_theme_font_id_t font,
                                     uint32_t color, lv_event_cb_t callback,
                                     void *user_data)
{
    if (parent == NULL)
    {
        return NULL;
    }
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    const int32_t target = size > 0 ? size : MUSIC_UI_CONTROL_SIZE;
    app_ui_click_only(button);
    lv_obj_set_size(button, target, target);
    music_ui_flat_button_reset(button, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *label = lv_label_create(button);
    if (label != NULL)
    {
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_font(label, app_ui_font(font), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, text != NULL ? text : "");
        app_ui_make_passive(label, false);
        lv_obj_center(label);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

lv_obj_t *music_ui_icon_button(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, int32_t size,
                               lv_event_cb_t callback, void *user_data)
{
    if (parent == NULL)
    {
        return NULL;
    }
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    const int32_t target = size > 0 ? size : MUSIC_UI_CONTROL_SIZE;
    lv_obj_set_size(button, target, target);
    music_ui_flat_button_reset(button, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *glyph = music_ui_image(button, image_id, fallback_symbol,
                                     target >= MUSIC_UI_PLAY_SIZE ? 30 : 28);
    if (glyph != NULL)
    {
        app_ui_icon_set_color(glyph, APP_UI_COLOR_TEXT);
        lv_obj_center(glyph);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

lv_obj_t *music_ui_plain_icon_button(lv_obj_t *parent, uint32_t image_id,
                                     const char *fallback_symbol, int32_t size,
                                     int32_t glyph_size, uint32_t color,
                                     lv_event_cb_t callback, void *user_data)
{
    if (parent == NULL)
    {
        return NULL;
    }
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    const int32_t target = size > 0 ? size : MUSIC_UI_CONTROL_SIZE;
    const int32_t glyph = glyph_size > 0 ? glyph_size : 28;
    lv_obj_set_size(button, target, target);
    music_ui_flat_button_reset(button, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *icon = music_ui_image(button, image_id, fallback_symbol, glyph);
    if (icon != NULL)
    {
        lv_obj_set_user_data(icon, (void *)(uintptr_t)image_id);
        app_ui_icon_set_color(icon, color);
        lv_obj_center(icon);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

void music_ui_set_plain_icon_button(lv_obj_t *button, uint32_t image_id,
                                    const char *fallback_symbol,
                                    int32_t glyph_size, uint32_t color)
{
    if (button == NULL)
    {
        return;
    }
    lv_obj_t *old_icon = lv_obj_get_child(button, 0);
    if (old_icon != NULL &&
            (uint32_t)(uintptr_t)lv_obj_get_user_data(old_icon) == image_id)
    {
        app_ui_icon_set_color(old_icon, color);
        return;
    }
    if (old_icon != NULL)
    {
        lv_obj_delete(old_icon);
    }
    lv_obj_t *icon = music_ui_image(button, image_id, fallback_symbol,
                                    glyph_size > 0 ? glyph_size : 28);
    if (icon != NULL)
    {
        lv_obj_set_user_data(icon, (void *)(uintptr_t)image_id);
        app_ui_icon_set_color(icon, color);
        lv_obj_center(icon);
    }
}

void music_ui_set_icon_button(lv_obj_t *button, uint32_t image_id,
                              const char *fallback_symbol, uint32_t color)
{
    if (button == NULL)
    {
        return;
    }
    lv_obj_t *old_icon = lv_obj_get_child(button, 0);
    if (old_icon != NULL &&
            (uint32_t)(uintptr_t)lv_obj_get_user_data(old_icon) == image_id)
    {
        app_ui_icon_set_color(old_icon, color);
        return;
    }
    if (old_icon != NULL)
    {
        lv_obj_delete(old_icon);
    }
    lv_obj_t *icon = music_ui_image(button, image_id, fallback_symbol, 28);
    if (icon != NULL)
    {
        lv_obj_set_user_data(icon, (void *)(uintptr_t)image_id);
        app_ui_icon_set_color(icon, color);
        lv_obj_center(icon);
    }
}

void music_ui_set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

lv_obj_t *music_ui_add_header_back(app_ui_page_t *page)
{
    if (page == NULL || page->header == NULL)
    {
        return NULL;
    }
    lv_obj_t *button = music_ui_plain_icon_button(
                           page->header, APP_IMAGE_MUSIC_BACK,
                           LV_SYMBOL_LEFT, MUSIC_UI_CONTROL_SIZE, 32,
                           APP_UI_COLOR_RAIN, _music_ui_back_clicked, NULL);
    if (button != NULL)
    {
        lv_obj_move_to_index(button, 0);
    }
    if (page->title != NULL)
    {
        lv_obj_set_style_text_color(page->title,
                                    lv_color_hex(APP_UI_COLOR_RAIN), 0);
    }
    return button;
}

lv_obj_t *music_ui_add_nav_row(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, const char *title,
                               const char *subtitle, lv_event_cb_t callback,
                               void *user_data, bool danger)
{
    if (parent == NULL)
    {
        return NULL;
    }
    lv_obj_t *row = lv_button_create(parent);
    if (row == NULL)
    {
        return NULL;
    }
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, MUSIC_UI_ROW_HEIGHT, 0);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, MUSIC_UI_ROW_PAD, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *well = lv_obj_create(row);
    if (well != NULL)
    {
        lv_obj_remove_style_all(well);
        lv_obj_set_size(well, MUSIC_UI_WELL_SIZE, MUSIC_UI_WELL_SIZE);
        lv_obj_set_style_radius(well, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(well, lv_color_hex(
                                      danger ? APP_UI_COLOR_WARNING_TINT :
                                      APP_UI_COLOR_RAIN_TINT), 0);
        lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
        app_ui_make_passive(well, false);
        lv_obj_t *icon = music_ui_image(well, image_id, fallback_symbol, 28);
        if (icon != NULL)
        {
            app_ui_icon_set_color(icon,
                                  danger ? APP_UI_COLOR_WARNING :
                                  APP_UI_COLOR_RAIN);
            lv_obj_center(icon);
        }
    }
    lv_obj_t *text = _music_ui_passive_box(row);
    if (text == NULL)
    {
        return row;
    }
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    (void)music_ui_marquee_label(text, title, APP_THEME_FONT_BODY,
                                 danger ? APP_UI_COLOR_WARNING :
                                 APP_UI_COLOR_TEXT);
    if (subtitle != NULL && subtitle[0] != '\0')
    {
        (void)_music_ui_row_label(text, subtitle, APP_THEME_FONT_SMALL,
                                  APP_UI_COLOR_MUTED, true);
    }
    lv_obj_t *chevron = music_ui_image(row, APP_IMAGE_UI_CHEVRON_RIGHT,
                                       LV_SYMBOL_RIGHT, 20);
    if (chevron != NULL)
    {
        app_ui_icon_set_color(chevron, APP_UI_COLOR_FAINT);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

static lv_obj_t *_music_ui_passive_box(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    if (box == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(box);
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(box, 4, 0);
    app_ui_make_passive(box, false);
    return box;
}

static lv_obj_t *_music_ui_row_label(lv_obj_t *parent, const char *text,
                                     app_theme_font_id_t font,
                                     uint32_t color, bool one_line)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, one_line ? LV_LABEL_LONG_DOT :
                           LV_LABEL_LONG_WRAP);
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    if (one_line)
    {
        lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

void music_ui_show_image(lv_obj_t *object, const music_image_t *image,
                         lv_image_dsc_t *descriptor,
                         uint16_t box_width, uint16_t box_height)
{
#ifdef HOST_TEST
    (void)object;
    (void)image;
    (void)descriptor;
    (void)box_width;
    (void)box_height;
#else
    if (object == NULL || image == NULL || descriptor == NULL)
    {
        return;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor->header.w = image->width;
    descriptor->header.h = image->height;
    descriptor->header.stride = (uint32_t)image->width * 2U;
    descriptor->data_size = image->data_size;
    descriptor->data = image->pixels;
    uint32_t zoom = 256U;
    if (box_width > 0U && box_height > 0U &&
            image->width > 0U && image->height > 0U)
    {
        const uint32_t zoom_x = (uint32_t)box_width * 256U / image->width;
        const uint32_t zoom_y = (uint32_t)box_height * 256U / image->height;
        zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
        if (zoom == 0U)
        {
            zoom = 1U;
        }
    }
    lv_image_set_scale(object, zoom);
    lv_image_set_src(object, descriptor);
#endif
}

void music_ui_cover_create(music_ui_cover_t *cover, lv_obj_t *parent,
                           const char *fallback_symbol, int32_t size)
{
    if (cover == NULL)
    {
        return;
    }
    memset(cover, 0, sizeof(*cover));
    cover->size = size > 0 ? size : MUSIC_UI_LIST_COVER;
    cover->frame = lv_obj_create(parent);
    if (cover->frame == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(cover->frame);
    lv_obj_set_size(cover->frame, cover->size, cover->size);
    lv_obj_set_style_radius(cover->frame, MUSIC_UI_COVER_RADIUS, 0);
    lv_obj_set_style_bg_color(cover->frame,
                              lv_color_hex(APP_UI_COLOR_RAIN_TINT), 0);
    lv_obj_set_style_bg_opa(cover->frame, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(cover->frame, true, 0);
    app_ui_make_passive(cover->frame, false);

    cover->placeholder = music_ui_image(cover->frame, APP_IMAGE_MUSIC_ICON,
                                        fallback_symbol != NULL ?
                                        fallback_symbol : LV_SYMBOL_AUDIO,
                                        cover->size / 2);
    if (cover->placeholder != NULL)
    {
        app_ui_icon_set_color(cover->placeholder, APP_UI_COLOR_RAIN);
        lv_obj_center(cover->placeholder);
    }
    cover->image_object = lv_image_create(cover->frame);
    if (cover->image_object != NULL)
    {
        lv_obj_set_size(cover->image_object, cover->size, cover->size);
        lv_obj_center(cover->image_object);
        app_ui_make_passive(cover->image_object, false);
        lv_obj_set_hidden(cover->image_object, true);
    }
}

void music_ui_cover_set_image(music_ui_cover_t *cover, music_image_t *image)
{
    if (cover == NULL || image == NULL || cover->image_object == NULL)
    {
        if (cover != NULL && image != NULL)
        {
            music_service_image_release(image);
        }
        return;
    }
    if (cover->image != NULL)
    {
        music_service_image_release(cover->image);
    }
    cover->image = image;
    music_ui_show_image(cover->image_object, image, &cover->descriptor,
                        (uint16_t)cover->size, (uint16_t)cover->size);
    lv_image_set_inner_align(cover->image_object, LV_IMAGE_ALIGN_COVER);
    lv_obj_set_hidden(cover->image_object, false);
    if (cover->placeholder != NULL)
    {
        lv_obj_set_hidden(cover->placeholder, true);
    }
}

bool music_ui_cover_accept_event(music_ui_cover_t *cover, uint32_t token,
                                 const char *url,
                                 const music_service_event_t *event)
{
    if (cover == NULL || url == NULL || url[0] == '\0' || event == NULL ||
            event->token != token ||
            event->failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        return false;
    }
    const music_image_t *image = NULL;
    if (music_service_image_acquire(url, (uint16_t)cover->size,
                                    (uint16_t)cover->size, token,
                                    &image) != ESP_OK ||
            image == NULL)
    {
        return false;
    }
    music_ui_cover_set_image(cover, (music_image_t *)image);
    return true;
}

void music_ui_cover_release(music_ui_cover_t *cover)
{
    if (cover == NULL)
    {
        return;
    }
    if (cover->image_object != NULL)
    {
        lv_image_set_src(cover->image_object, NULL);
        lv_obj_set_hidden(cover->image_object, true);
    }
    if (cover->placeholder != NULL)
    {
        lv_obj_set_hidden(cover->placeholder, false);
    }
    if (cover->image != NULL)
    {
        music_service_image_release(cover->image);
        cover->image = NULL;
    }
    memset(&cover->descriptor, 0, sizeof(cover->descriptor));
}

static lv_obj_t *_music_ui_row_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_button_create(parent);
    if (row == NULL)
    {
        return NULL;
    }
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, MUSIC_UI_ROW_HEIGHT, 0);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, MUSIC_UI_ROW_PAD, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

lv_obj_t *music_ui_add_playlist_row(lv_obj_t *parent,
                                    const music_playlist_t *playlist,
                                    lv_event_cb_t callback,
                                    void *user_data)
{
    if (parent == NULL || playlist == NULL)
    {
        return NULL;
    }
    lv_obj_t *row = _music_ui_row_create(parent);
    if (row == NULL)
    {
        return NULL;
    }
    music_ui_cover_t cover;
    music_ui_cover_create(&cover, row, LV_SYMBOL_AUDIO, MUSIC_UI_LIST_COVER);
    lv_obj_t *text = _music_ui_passive_box(row);
    if (text == NULL)
    {
        return row;
    }
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    (void)music_ui_marquee_label(text, playlist->title,
                                 APP_THEME_FONT_BODY, APP_UI_COLOR_TEXT);
    char subtitle[96];
    (void)snprintf(subtitle, sizeof(subtitle), "%s · %u 首",
                   playlist->creator[0] == '\0' ? "QQ 音乐" : playlist->creator,
                   (unsigned)playlist->track_count);
    (void)_music_ui_row_label(text, subtitle, APP_THEME_FONT_SMALL,
                              APP_UI_COLOR_MUTED, true);
    lv_obj_t *chevron = music_ui_image(row, APP_IMAGE_UI_CHEVRON_RIGHT,
                                       LV_SYMBOL_RIGHT, 20);
    if (chevron != NULL)
    {
        app_ui_icon_set_color(chevron, APP_UI_COLOR_FAINT);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

lv_obj_t *music_ui_add_song_row(lv_obj_t *parent, const music_song_t *song,
                                uint16_t index, lv_event_cb_t callback,
                                void *user_data)
{
    if (parent == NULL || song == NULL)
    {
        return NULL;
    }
    lv_obj_t *row = _music_ui_row_create(parent);
    if (row == NULL)
    {
        return NULL;
    }
    char ordinal[8];
    (void)snprintf(ordinal, sizeof(ordinal), "%u", (unsigned)(index + 1U));
    lv_obj_t *number = _music_ui_row_label(row, ordinal, APP_THEME_FONT_SMALL,
                                           APP_UI_COLOR_FAINT, true);
    lv_obj_set_width(number, MUSIC_UI_ORDINAL_WIDTH);
    lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *text = _music_ui_passive_box(row);
    if (text == NULL)
    {
        return row;
    }
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    (void)_music_ui_row_label(text, song->title, APP_THEME_FONT_BODY,
                              APP_UI_COLOR_TEXT, true);
    char detail[MUSIC_SERVICE_ARTIST_BYTES + MUSIC_SERVICE_ALBUM_BYTES + 4U];
    if (song->album[0] != '\0')
    {
        (void)snprintf(detail, sizeof(detail), "%s · %s",
                       song->artist[0] == '\0' ? "未知歌手" : song->artist,
                       song->album);
    }
    else
    {
        (void)snprintf(detail, sizeof(detail), "%s",
                       song->artist[0] == '\0' ? "未知歌手" : song->artist);
    }
    (void)_music_ui_row_label(text, detail, APP_THEME_FONT_SMALL,
                              APP_UI_COLOR_MUTED, true);

    char duration[16];
    music_ui_format_time(song->duration_ms, duration, sizeof(duration));
    lv_obj_t *time = _music_ui_row_label(row, duration, APP_THEME_FONT_SMALL,
                                         APP_UI_COLOR_FAINT, true);
    lv_obj_set_width(time, 46);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, 0);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

static void _music_navigation_complete(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        (void)result;
    }
}

esp_err_t music_ui_open_playlist(const music_playlist_ref_t *ref)
{
    if (ref == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const music_playlist_arguments_t arguments =
    {
        .ref = *ref,
    };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYLIST,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = MUSIC_ARGUMENT_PLAYLIST,
            .size = sizeof(arguments),
        },
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    memcpy(request.arguments.payload, &arguments, sizeof(arguments));
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}

static esp_err_t _music_ui_open_page(const char *page_id)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = page_id,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}

esp_err_t music_ui_open_sidebar(void)
{
    return _music_ui_open_page(MUSIC_PAGE_SIDEBAR);
}

esp_err_t music_ui_open_queue(void)
{
    return _music_ui_open_page(MUSIC_PAGE_QUEUE);
}

esp_err_t music_ui_open_lyrics(void)
{
    return _music_ui_open_page(MUSIC_PAGE_LYRICS);
}

esp_err_t music_ui_open_login(void)
{
    return _music_ui_open_page(MUSIC_PAGE_LOGIN_METHOD);
}

esp_err_t music_ui_open_login_qr(music_login_type_t type)
{
    const music_login_arguments_t arguments =
    {
        .type = type,
    };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LOGIN,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = MUSIC_ARGUMENT_LOGIN,
            .size = sizeof(arguments),
        },
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    memcpy(request.arguments.payload, &arguments, sizeof(arguments));
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}

static void _music_enter_login_qr_done(esp_err_t result, void *context)
{
    (void)context;
    if (result == ESP_OK)
    {
        _music_ui_remove_page(MUSIC_PAGE_START);
    }
}

esp_err_t music_ui_enter_login_qr(music_login_type_t type)
{
    const music_login_arguments_t arguments =
    {
        .type = type,
    };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LOGIN,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = MUSIC_ARGUMENT_LOGIN,
            .size = sizeof(arguments),
        },
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    memcpy(request.arguments.payload, &arguments, sizeof(arguments));
    return app_manager_navigate_async(&request, _music_enter_login_qr_done,
                                      NULL);
}

static void _music_ui_remove_page(const char *page_id)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_REMOVE_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = page_id,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_NONE },
    };
    (void)app_manager_navigate_async(&request, _music_navigation_complete,
                                     NULL);
}

static void _music_enter_player_done(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        return;
    }
    _music_ui_remove_page(MUSIC_PAGE_LOGIN);
    _music_ui_remove_page(MUSIC_PAGE_LOGIN_METHOD);
    _music_ui_remove_page(MUSIC_PAGE_START);
}

esp_err_t music_ui_enter_player(void)
{
    if (app_manager_is_page_present(APP_MANAGER_ID_MUSIC, MUSIC_PAGE_PLAYER))
    {
        return music_ui_back_to_player();
    }
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYER,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_enter_player_done,
                                      NULL);
}

static void _music_reset_to_login_done(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        return;
    }
    _music_ui_remove_page(MUSIC_PAGE_SIDEBAR);
    _music_ui_remove_page(MUSIC_PAGE_PLAYER);
    _music_ui_remove_page(MUSIC_PAGE_START);
}

esp_err_t music_ui_reset_to_login(void)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LOGIN_METHOD,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_reset_to_login_done,
                                      NULL);
}

static void _music_back_to_player_done(esp_err_t result, void *context)
{
    (void)context;
    if (result == ESP_OK)
    {
        return;
    }
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYER,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    (void)app_manager_navigate_async(&request, _music_navigation_complete,
                                     NULL);
}

esp_err_t music_ui_back_to_player(void)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_BACK_TO,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYER,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_back_to_player_done,
                                      NULL);
}

esp_err_t music_ui_play_playlist(const music_playlist_ref_t *ref,
                                 uint16_t index)
{
    const esp_err_t result = music_service_play_playlist(ref, index);
    if (result != ESP_OK)
    {
        return result;
    }
    return music_ui_back_to_player();
}

uint32_t music_ui_mode_icon(music_play_mode_t mode)
{
    switch (mode)
    {
    case MUSIC_PLAY_MODE_REPEAT_ALL:
        return APP_IMAGE_UI_LOOP;
    case MUSIC_PLAY_MODE_REPEAT_ONE:
        return APP_IMAGE_MUSIC_REPEAT_ONE;
    case MUSIC_PLAY_MODE_SHUFFLE:
        return APP_IMAGE_MUSIC_SHUFFLE;
    default:
        return APP_IMAGE_MUSIC_SEQUENTIAL;
    }
}

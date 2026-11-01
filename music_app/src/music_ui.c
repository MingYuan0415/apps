#include "music_app_internal.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_UI_COVER_RADIUS       APP_UI_RADIUS_CONTROL
#define MUSIC_UI_LIST_COVER_RADIUS  10
#define MUSIC_UI_ROW_PAD            8
#define MUSIC_UI_ORDINAL_WIDTH      28

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
    lv_obj_set_style_pad_row(box, 1, 0);
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
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
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

    cover->placeholder = app_ui_image(cover->frame, APP_IMAGE_MUSIC_ICON,
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
    lv_obj_set_height(row, MUSIC_UI_ROW_HEIGHT);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, MUSIC_UI_ROW_PAD, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
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
    (void)_music_ui_row_label(text, playlist->title, APP_THEME_FONT_BODY,
                              APP_UI_COLOR_TEXT, true);
    char subtitle[96];
    (void)snprintf(subtitle, sizeof(subtitle), "%s · %u 首",
                   playlist->creator[0] == '\0' ? "QQ 音乐" : playlist->creator,
                   (unsigned)playlist->track_count);
    (void)_music_ui_row_label(text, subtitle, APP_THEME_FONT_SMALL,
                              APP_UI_COLOR_MUTED, true);
    lv_obj_t *chevron = app_ui_image(row, APP_IMAGE_UI_CHEVRON_RIGHT,
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

esp_err_t music_ui_open_player(void)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYER,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}

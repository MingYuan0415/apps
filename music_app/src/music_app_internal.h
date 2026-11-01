#ifndef __MUSIC_APP_INTERNAL_H__
#define __MUSIC_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "music_service.h"
#include "music_ui_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_PAGE_ROOT      "root"
#define MUSIC_PAGE_PLAYLIST  "playlist"
#define MUSIC_PAGE_PLAYER    "player"
#define MUSIC_PAGE_LYRICS    "lyrics"
#define MUSIC_PAGE_LOGIN     "login"

#define MUSIC_ARGUMENT_PLAYLIST 1U

#define MUSIC_UI_PAGE_SIZE       368
#define MUSIC_UI_HEADER_HEIGHT   64
#define MUSIC_UI_PAGE_MARGIN     16
#define MUSIC_UI_ROW_HEIGHT      72
#define MUSIC_UI_CONTROL_SIZE    56
#define MUSIC_UI_HERO_COVER      88
#define MUSIC_UI_LIST_COVER      52
#define MUSIC_UI_LYRIC_LINES     7U
#define MUSIC_UI_CONTENT_WIDTH   (MUSIC_UI_PAGE_SIZE - 2 * MUSIC_UI_PAGE_MARGIN)

typedef struct music_playlist_arguments
{
    music_playlist_ref_t ref;
} music_playlist_arguments_t;

extern const app_manager_page_definition_t music_root_page_definition;
extern const app_manager_page_definition_t music_playlist_page_definition;
extern const app_manager_page_definition_t music_player_page_definition;
extern const app_manager_page_definition_t music_lyrics_page_definition;
extern const app_manager_page_definition_t music_login_page_definition;

/** @brief A fixed square cover with one service image lease at a time. */
typedef struct music_ui_cover
{
    lv_obj_t *frame;
    lv_obj_t *image_object;
    lv_obj_t *placeholder;
    music_image_t *image;
    lv_image_dsc_t descriptor;
    int32_t size;
} music_ui_cover_t;

/** @brief Show a decoded RGB565 image fitted into a fixed display box. */
void music_ui_show_image(lv_obj_t *object, const music_image_t *image,
                         lv_image_dsc_t *descriptor,
                         uint16_t box_width, uint16_t box_height);

/** @brief Create a square cover placeholder that can display one image. */
void music_ui_cover_create(music_ui_cover_t *cover, lv_obj_t *parent,
                           const char *fallback_symbol, int32_t size);

/** @brief Transfer an acquired image to the cover and display it. */
void music_ui_cover_set_image(music_ui_cover_t *cover, music_image_t *image);

/** @brief Consume an image notification and accept it only for this request. */
bool music_ui_cover_accept_event(music_ui_cover_t *cover, uint32_t token,
                                 const char *url,
                                 const music_service_event_t *event);

/** @brief Release the service image and restore the cover placeholder. */
void music_ui_cover_release(music_ui_cover_t *cover);

/** @brief Create a playlist row with a music-specific cover placeholder. */
lv_obj_t *music_ui_add_playlist_row(lv_obj_t *parent,
                                    const music_playlist_t *playlist,
                                    lv_event_cb_t callback,
                                    void *user_data);

/** @brief Create a song row with ordinal, title, artist, album and duration. */
lv_obj_t *music_ui_add_song_row(lv_obj_t *parent, const music_song_t *song,
                                uint16_t index, lv_event_cb_t callback,
                                void *user_data);

/** @brief Copy one playlist reference into a Typed Blob navigation request. */
esp_err_t music_ui_open_playlist(const music_playlist_ref_t *ref);

/** @brief Open the player page. */
esp_err_t music_ui_open_player(void);

#endif /* __MUSIC_APP_INTERNAL_H__ */

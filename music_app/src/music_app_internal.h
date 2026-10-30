#ifndef __MUSIC_APP_INTERNAL_H__
#define __MUSIC_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "music_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_PAGE_ROOT      "root"
#define MUSIC_PAGE_PLAYLIST  "playlist"
#define MUSIC_PAGE_PLAYER    "player"
#define MUSIC_PAGE_LYRICS    "lyrics"
#define MUSIC_PAGE_LOGIN     "login"

#define MUSIC_ARGUMENT_PLAYLIST 1U

typedef struct music_playlist_arguments
{
    music_playlist_ref_t ref;
} music_playlist_arguments_t;

extern const app_manager_page_definition_t music_root_page_definition;
extern const app_manager_page_definition_t music_playlist_page_definition;
extern const app_manager_page_definition_t music_player_page_definition;
extern const app_manager_page_definition_t music_lyrics_page_definition;
extern const app_manager_page_definition_t music_login_page_definition;

/** @brief Format milliseconds as minutes:seconds. */
void music_ui_format_time(uint32_t milliseconds, char *output,
                          size_t output_size);

/** @brief Show a decoded RGB565 image on an lv_image object. */
void music_ui_show_image(lv_obj_t *object, const music_image_t *image,
                         lv_image_dsc_t *descriptor);

/** @brief Human-readable account/service state. */
const char *music_ui_state_text(const music_service_status_t *status);

/** @brief Human-readable failure text. */
const char *music_ui_failure_text(music_service_failure_t failure);

/** @brief Create a playlist card/row with cover placeholder. */
lv_obj_t *music_ui_add_playlist_row(lv_obj_t *parent,
                                    const music_playlist_t *playlist,
                                    lv_event_cb_t callback,
                                    void *user_data);

/** @brief Create a song row. */
lv_obj_t *music_ui_add_song_row(lv_obj_t *parent, const music_song_t *song,
                                uint16_t index, lv_event_cb_t callback,
                                void *user_data);

/** @brief Copy one playlist reference into a Typed Blob navigation request. */
esp_err_t music_ui_open_playlist(const music_playlist_ref_t *ref);

/** @brief Open the player page. */
esp_err_t music_ui_open_player(void);

#endif /* __MUSIC_APP_INTERNAL_H__ */

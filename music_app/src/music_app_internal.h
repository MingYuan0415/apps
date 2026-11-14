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

#define MUSIC_PAGE_START         "start"
#define MUSIC_PAGE_PLAYER        "player"
#define MUSIC_PAGE_SIDEBAR       "sidebar"
#define MUSIC_PAGE_PLAYLIST      "playlist"
#define MUSIC_PAGE_QUEUE         "queue"
#define MUSIC_PAGE_LYRICS        "lyrics"
#define MUSIC_PAGE_LOGIN_METHOD  "login_method"
#define MUSIC_PAGE_LOGIN         "login"

#define MUSIC_ARGUMENT_PLAYLIST 1U
#define MUSIC_ARGUMENT_LOGIN    2U

#define MUSIC_UI_PAGE_MARGIN        16
#define MUSIC_UI_ROW_HEIGHT         64
#define MUSIC_UI_HERO_COVER         96
#define MUSIC_UI_LIST_COVER         56
#define MUSIC_UI_CONTROL_SIZE       52
#define MUSIC_UI_WELL_SIZE          44
#define MUSIC_UI_TRANSPORT_SIZE     64
#define MUSIC_UI_PLAY_SIZE          80
#define MUSIC_UI_LYRIC_LINES        7U
#define MUSIC_UI_MAX_PLAYLIST_REFS  36U

typedef struct music_playlist_arguments
{
    music_playlist_ref_t ref;
} music_playlist_arguments_t;

typedef struct music_login_arguments
{
    music_login_type_t type;
} music_login_arguments_t;

typedef struct music_playlist_refs
{
    music_playlist_ref_t items[MUSIC_UI_MAX_PLAYLIST_REFS];
    size_t count;
} music_playlist_refs_t;

extern const app_manager_page_definition_t music_start_page_definition;
extern const app_manager_page_definition_t music_player_page_definition;
extern const app_manager_page_definition_t music_sidebar_page_definition;
extern const app_manager_page_definition_t music_playlist_page_definition;
extern const app_manager_page_definition_t music_queue_page_definition;
extern const app_manager_page_definition_t music_lyrics_page_definition;
extern const app_manager_page_definition_t music_login_method_page_definition;
extern const app_manager_page_definition_t music_login_page_definition;

/** @brief Static App-owned provider for the system control-center card. */
extern const app_manager_control_center_card_ops_t
music_control_center_card_ops;

/** @brief Create a music icon scaled to the requested display box. */
lv_obj_t *music_ui_image(lv_obj_t *parent, uint32_t image_id,
                         const char *fallback_symbol, int32_t size);

/** @brief Remove theme button chrome before applying a local flat style. */
void music_ui_flat_button_reset(lv_obj_t *button, int32_t radius);

/** @brief Create one transparent text-glyph button. */
lv_obj_t *music_ui_plain_text_button(lv_obj_t *parent, const char *text,
                                     int32_t size, app_theme_font_id_t font,
                                     uint32_t color, lv_event_cb_t callback,
                                     void *user_data);

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

/** @brief Create one 52 px round icon button with pressed/disabled styling. */
lv_obj_t *music_ui_icon_button(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, int32_t size,
                               lv_event_cb_t callback, void *user_data);

/** @brief Create one borderless icon button for transport-style controls. */
lv_obj_t *music_ui_plain_icon_button(lv_obj_t *parent, uint32_t image_id,
                                     const char *fallback_symbol, int32_t size,
                                     int32_t glyph_size, uint32_t color,
                                     lv_event_cb_t callback, void *user_data);

/** @brief Replace an icon button glyph and tint. */
void music_ui_set_icon_button(lv_obj_t *button, uint32_t image_id,
                              const char *fallback_symbol, uint32_t color);

/** @brief Replace a borderless icon button glyph, size and tint. */
void music_ui_set_plain_icon_button(lv_obj_t *button, uint32_t image_id,
                                    const char *fallback_symbol,
                                    int32_t glyph_size, uint32_t color);

/** @brief Show or clear a spinner over one icon button. */
void music_ui_set_button_busy(lv_obj_t *button, bool busy);

/** @brief Enable or disable one icon button. */
void music_ui_set_button_enabled(lv_obj_t *button, bool enabled);

/** @brief Insert a borderless accent back button and accent the page title. */
lv_obj_t *music_ui_add_header_back(app_ui_page_t *page);

/** @brief Add an icon-led navigation/command row with a wrapped title. */
lv_obj_t *music_ui_add_nav_row(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, const char *title,
                               const char *subtitle, lv_event_cb_t callback,
                               void *user_data, bool danger);

/** @brief Create a song row with ordinal, wrapped title, artist, album and duration. */
lv_obj_t *music_ui_add_song_row(lv_obj_t *parent, const music_song_t *song,
                                uint16_t index, lv_event_cb_t callback,
                                void *user_data);

/** @brief Copy one playlist reference into a Typed Blob navigation request. */
esp_err_t music_ui_open_playlist(const music_playlist_ref_t *ref);

/** @brief Navigate to the sidebar page. */
esp_err_t music_ui_open_sidebar(void);

/** @brief Navigate to the current playback-queue page. */
esp_err_t music_ui_open_queue(void);

/** @brief Navigate to the full-lyrics page. */
esp_err_t music_ui_open_lyrics(void);

/** @brief Navigate to the login-method selection page. */
esp_err_t music_ui_open_login(void);

/** @brief Navigate to the QR page for one login channel. */
esp_err_t music_ui_open_login_qr(music_login_type_t type);

/** @brief Enter the QR page from start and remove the start page. */
esp_err_t music_ui_enter_login_qr(music_login_type_t type);

/** @brief Enter the player and remove the start/login pages if needed. */
esp_err_t music_ui_enter_player(void);

/** @brief Open the login method page and remove the current app pages. */
esp_err_t music_ui_reset_to_login(void);

/**
 * @brief Route to login-method when the service reports an expired session.
 *
 * Must run on the UI worker. Returns true when the status requires login.
 */
bool music_ui_guard_session(const music_service_status_t *status);

/** @brief Whether expired-account page cleanup still owns navigation. */
bool music_ui_session_cleanup_busy(void);

/** @brief Return to the player home, falling back to a page open. */
esp_err_t music_ui_back_to_player(void);

/** @brief Start a playlist entry and return to the player home. */
esp_err_t music_ui_play_playlist(const music_playlist_ref_t *ref,
                                 uint16_t index);

/** @brief Icon shown for one queue play mode. */
uint32_t music_ui_mode_icon(music_play_mode_t mode);

#endif /* __MUSIC_APP_INTERNAL_H__ */

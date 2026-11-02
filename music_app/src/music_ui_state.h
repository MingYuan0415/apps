#ifndef __MUSIC_UI_STATE_H__
#define __MUSIC_UI_STATE_H__

#include "music_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Capacity for music_ui_queue_text() output. */
#define MUSIC_UI_QUEUE_TEXT_BYTES 48U

void music_ui_format_time(uint32_t milliseconds, char *output,
                          size_t output_size);

/** @brief Human-readable account/service state. */
const char *music_ui_state_text(const music_service_status_t *status);

/** @brief Human-readable failure text. */
const char *music_ui_failure_text(music_service_failure_t failure);

/** @brief Human-readable playback state. */
const char *music_ui_playback_text(music_playback_state_t state);

/** @brief Human-readable queue mode. */
const char *music_ui_mode_text(music_play_mode_t mode);

/** @brief Next mode in the UI cycle, including shuffle. */
music_play_mode_t music_ui_next_mode(music_play_mode_t mode);

/** @brief Human-readable audio quality. */
const char *music_ui_quality_text(music_quality_t quality);

/** @brief Whether play/pause changes an existing stream. */
bool music_ui_can_toggle_playback(const music_player_status_t *status);

/** @brief Whether the service can step in the requested queue direction. */
bool music_ui_can_step(const music_player_status_t *status, bool forward);

/** @brief Whether an error can be recovered by replaying the current queue item. */
bool music_ui_can_retry_playback(const music_player_status_t *status);

/** @brief Whether a service failure should offer an explicit retry action. */
bool music_ui_failure_is_retryable(music_service_failure_t failure);

/** @brief Stable token for an image URL, shared by cover requesters. */
uint32_t music_ui_url_token(const char *url);

/** @brief Whether a playlist reference can be opened or played. */
bool music_ui_playlist_ref_valid(const music_playlist_ref_t *ref);

/**
 * @brief Number of pages remaining for the rows loaded so far.
 *
 * `loaded_pages` counts contiguous pages from the first page and `loaded_rows`
 * is their actual row count. Using returned rows rather than the request size
 * keeps later page numbers correct when the provider returns short pages.
 */
uint16_t music_ui_playlist_page_count(uint32_t total, uint32_t loaded_rows,
                                      uint16_t loaded_pages);

/** @brief Format the queue position without inventing unavailable data. */
void music_ui_queue_text(const music_player_status_t *status, char *output,
                         size_t output_size);

/** @brief Find the first visible lyric line for a seven-line window. */
size_t music_ui_lyric_window_start(size_t line_count, size_t current_line);

/** @brief Find the active lyric line at a playback position. */
size_t music_ui_lyric_index_at(const music_lyric_snapshot_t *lyric,
                               uint32_t position_ms);

/**
 * @brief Return the previous, current and next lyric line at a position.
 *
 * Missing neighbours are returned as NULL; empty lines are returned as an
 * empty string. Returns false when no lyric snapshot or line is available.
 */
bool music_ui_lyric_triplet(const music_lyric_snapshot_t *lyric,
                            uint32_t position_ms,
                            const char **previous,
                            const char **current,
                            const char **next);

/**
 * @brief Find the playlist used by the "喜欢" sidebar shortcut.
 *
 * The service exposes the dirId 201 liked-songs playlist in my_favorite.
 * Returns NULL when the account has no such playlist.
 */
const music_playlist_t *music_ui_liked_playlist(
    const music_home_snapshot_t *home);

/** @brief Pick the recommended playlist used as the personal radio source. */
const music_playlist_t *music_ui_radio_playlist(
    const music_home_snapshot_t *home);

#endif /* __MUSIC_UI_STATE_H__ */

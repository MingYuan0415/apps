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

/** @brief Format the queue position without inventing unavailable data. */
void music_ui_queue_text(const music_player_status_t *status, char *output,
                         size_t output_size);

/** @brief Find the first visible lyric line for a seven-line window. */
size_t music_ui_lyric_window_start(size_t line_count, size_t current_line);

/** @brief Find the active lyric line at a playback position. */
size_t music_ui_lyric_index_at(const music_lyric_snapshot_t *lyric,
                               uint32_t position_ms);

#endif /* __MUSIC_UI_STATE_H__ */

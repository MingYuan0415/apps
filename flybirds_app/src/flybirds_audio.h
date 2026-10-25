/**
 * @brief Flybirds audio session: the app-owned PCM stream and its writer task.
 *
 * audio_service has no mixer or decoder and no arbiter between writers, so the
 * app takes the stream only from READY, never stops a stream it did not start,
 * and mixes music with effects inside one task before writing.
 */

#ifndef __FLYBIRDS_AUDIO_H__
#define __FLYBIRDS_AUDIO_H__

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "flybirds_synth.h"

/** Frames rendered per audio block (20 ms at 16 kHz). */
#define FLYBIRDS_AUDIO_BLOCK_FRAMES FLYBIRDS_SYNTH_BLOCK_FRAMES
/** Worker priority: below the UI and app-control tasks. */
#define FLYBIRDS_AUDIO_TASK_PRIORITY 4U
/** Worker stack in bytes. */
#define FLYBIRDS_AUDIO_TASK_STACK 4096U
/** Bounded wait for the writer task to leave the loop. */
#define FLYBIRDS_AUDIO_STOP_TIMEOUT_MS 300U
/** Per-block PCM write deadline. */
#define FLYBIRDS_AUDIO_WRITE_TIMEOUT_MS 20U

/**
 * @brief Claim the shared PCM path and start the writer task.
 *
 * When audio is unavailable or another app already owns the stream, nothing is
 * started and ESP_ERR_NOT_FOUND is returned; the caller keeps playing silently.
 *
 * @param music enables the background music bus.
 * @param sfx enables the effect bus.
 * @return ESP_OK when streaming, ESP_ERR_NOT_FOUND when silent, or an
 *         allocation/start error.
 */
esp_err_t flybirds_audio_open(bool music, bool sfx);

/**
 * @brief Stop the writer task and release the PCM path when this app owns it.
 *
 * A failed task join keeps the handles in place so the next call can retry; a
 * failed stop keeps ownership recorded for the same reason.
 *
 * @return ESP_OK, ESP_ERR_TIMEOUT while the task is still leaving, or the
 *         audio_service stop error.
 */
esp_err_t flybirds_audio_close(void);

/** @brief Switch the music bus; ignored when the stream is not open. */
void flybirds_audio_set_music(bool enabled);

/** @brief Switch the effect bus; ignored when the stream is not open. */
void flybirds_audio_set_sfx(bool enabled);

/** @brief Queue one effect; a later trigger replaces an earlier queued one. */
void flybirds_audio_trigger(flybirds_sfx_t sfx);

/**
 * @brief Report whether a live writer task is currently running.
 *
 * The page polls this so a transport that dies after a successful open is
 * retried on the same cadence as an open that never succeeded.
 */
bool flybirds_audio_is_open(void);

/**
 * @brief Claim the PCM path when audio_service is READY.
 * @return true when this call started the stream.
 */
bool flybirds_audio_session_acquire(void);

/**
 * @brief Release the PCM path only when this app started it.
 * @return ESP_OK when not owned or stopped, otherwise the stop error.
 */
esp_err_t flybirds_audio_session_release(void);

/**
 * @brief Report whether this app started the currently running stream.
 *
 * Test observation point: production code only needs open/close.
 */
bool flybirds_audio_session_is_owned(void);

/**
 * @brief Write interleaved stereo frames to the owned stream.
 *
 * A short write is reported through @p written_frames together with the
 * transport error, so the caller can resume the same block from the first
 * unwritten frame instead of replaying it.
 *
 * @param interleaved is the PCM block.
 * @param frames is the requested frame count.
 * @param written_frames receives the frames the transport accepted.
 * @return ESP_ERR_INVALID_STATE when the stream is not owned or an argument is
 *         missing; otherwise the audio_service write result.
 */
esp_err_t flybirds_audio_session_write(const int16_t *interleaved,
                                       size_t frames,
                                       size_t *written_frames);

#endif /* __FLYBIRDS_AUDIO_H__ */

#ifndef __AUDIO_DEMO_ADAPTER_H__
#define __AUDIO_DEMO_ADAPTER_H__

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Audio demo worker lifecycle visible to the page. */
typedef enum
{
    AUDIO_DEMO_ADAPTER_CLOSED = 0, /**< The adapter owns no worker. */
    AUDIO_DEMO_ADAPTER_STARTING,   /**< The worker is opening the stream. */
    AUDIO_DEMO_ADAPTER_RUNNING,    /**< Capture and commands are available. */
    AUDIO_DEMO_ADAPTER_UNAVAILABLE, /**< The board audio path is unavailable. */
    AUDIO_DEMO_ADAPTER_STOPPING,   /**< The worker is releasing the stream. */
    AUDIO_DEMO_ADAPTER_ERROR,      /**< A worker or cleanup operation failed. */
} audio_demo_adapter_state_t;

/** @brief State of the bounded 440 Hz playback operation. */
typedef enum
{
    AUDIO_DEMO_TONE_IDLE = 0, /**< No tone has run in this session. */
    AUDIO_DEMO_TONE_PLAYING,  /**< PCM chunks are being written. */
    AUDIO_DEMO_TONE_COMPLETE, /**< One second of PCM was written. */
    AUDIO_DEMO_TONE_CANCELLED, /**< Playback stopped at a chunk boundary. */
    AUDIO_DEMO_TONE_ERROR,     /**< Playback failed before completion. */
} audio_demo_tone_state_t;

/** @brief Immutable audio status copied under the adapter snapshot lock. */
typedef struct audio_demo_snapshot
{
    uint32_t generation; /**< Monotonic snapshot revision. */
    uint32_t volume_request_id; /**< Last completed volume request ID. */
    uint32_t mute_request_id; /**< Last completed mute request ID. */
    esp_err_t last_error; /**< Latest non-successful worker operation. */
    esp_err_t volume_result; /**< Result paired with volume_request_id. */
    esp_err_t mute_result; /**< Result paired with mute_request_id. */
    audio_demo_adapter_state_t state; /**< Worker lifecycle state. */
    audio_demo_tone_state_t tone_state; /**< Current tone state. */
    uint8_t mic_level_percent; /**< Smoothed instantaneous input level. */
    uint8_t volume_percent; /**< Last successfully applied speaker volume. */
    bool muted; /**< Requested speaker mute state. */
} audio_demo_snapshot_t;

/**
 * @brief Page-private ownership for one audio demo worker.
 *
 * @note A zeroed context owns no resources. APIs are LVGL-worker-only; the
 *       internal task accesses the context until close succeeds.
 */
typedef struct audio_demo_adapter
{
    QueueHandle_t commands; /**< Serialized worker command queue. */
    SemaphoreHandle_t snapshot_lock; /**< Protects the published snapshot. */
    SemaphoreHandle_t close_done; /**< Signals one worker close attempt. */
    TaskHandle_t worker; /**< Owned audio worker task. */
    audio_demo_snapshot_t snapshot; /**< Last published worker snapshot. */
    uint32_t next_volume_request_id; /**< Last queued volume request ID. */
    uint32_t next_mute_request_id; /**< Last queued mute request ID. */
    atomic_uint cancel_generation; /**< Tone cancellation generation. */
    atomic_bool accepting_commands; /**< Command admission remains open. */
    atomic_bool tone_pending; /**< Tone is queued or playing. */
    atomic_bool shutdown_requested; /**< Worker teardown request. */
    atomic_bool worker_exited; /**< Worker reached its terminal state. */
    atomic_uint close_request_id; /**< Latest close attempt requested. */
    atomic_uint close_completion_id; /**< Latest close attempt completed. */
    atomic_int close_result; /**< Result of the latest close attempt. */
} audio_demo_adapter_t;

/**
 * @brief Start the worker and asynchronously claim the audio stream.
 * @param adapter is a zeroed page-private adapter context.
 * @return ESP_OK when the worker is running, otherwise an allocation error.
 */
esp_err_t audio_demo_adapter_open(audio_demo_adapter_t *adapter);

/**
 * @brief Cancel playback, stop the stream, and release worker resources.
 *
 * @note A failed or timed-out close preserves all handles for a later retry.
 *       Commands remain rejected after the first close attempt begins.
 *
 * @param adapter owns the worker resources.
 * @return ESP_OK when fully released, otherwise a retryable cleanup error.
 */
esp_err_t audio_demo_adapter_close(audio_demo_adapter_t *adapter);

/**
 * @brief Copy the latest worker snapshot without accessing audio hardware.
 * @param adapter owns an open worker.
 * @param snapshot receives a consistent snapshot.
 * @return ESP_OK when copied, otherwise an adapter state or lock error.
 */
esp_err_t audio_demo_adapter_get_snapshot(
    audio_demo_adapter_t *adapter,
    audio_demo_snapshot_t *snapshot);

/**
 * @brief Queue one second of 440 Hz PCM playback.
 * @param adapter owns an open worker.
 * @return ESP_OK when queued, otherwise an admission error.
 */
esp_err_t audio_demo_adapter_play_tone(audio_demo_adapter_t *adapter);

/**
 * @brief Atomically cancel a queued or active tone.
 * @param adapter owns an open worker.
 * @return ESP_OK when a tone was pending, otherwise ESP_ERR_NOT_FOUND.
 */
esp_err_t audio_demo_adapter_cancel_tone(audio_demo_adapter_t *adapter);

/**
 * @brief Queue a speaker-volume update.
 * @param adapter owns an open worker.
 * @param percent is the requested volume in the range 0..100.
 * @param request_id optionally receives the nonzero queued request ID. The
 *                   snapshot reports completion by pairing this ID with
 *                   volume_result; volume_percent changes only on success.
 * @return ESP_OK when queued, otherwise an admission error.
 */
esp_err_t audio_demo_adapter_set_volume(audio_demo_adapter_t *adapter,
                                        uint8_t percent,
                                        uint32_t *request_id);

/**
 * @brief Queue a speaker-mute update.
 * @param adapter owns an open worker.
 * @param muted selects the requested mute state.
 * @param request_id optionally receives the nonzero queued request ID. The
 *                   snapshot reports completion by pairing this ID with
 *                   mute_result; muted changes only on success.
 * @return ESP_OK when queued, otherwise an admission error.
 */
esp_err_t audio_demo_adapter_set_mute(audio_demo_adapter_t *adapter,
                                      bool muted,
                                      uint32_t *request_id);

/**
 * @brief Report whether the adapter still owns worker resources.
 * @param adapter is the page-private adapter context.
 * @return true while open or awaiting cleanup; false otherwise.
 */
bool audio_demo_adapter_is_open(const audio_demo_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_DEMO_ADAPTER_H__ */

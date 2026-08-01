#define DBG_TAG "audio_demo_adapter"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "audio_demo_adapter.h"

#include "audio_service.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

#include <stddef.h>
#include <string.h>

#define AUDIO_DEMO_COMMAND_COUNT       6U
#define AUDIO_DEMO_TASK_STACK_BYTES    4096U
#define AUDIO_DEMO_TASK_PRIORITY       (tskIDLE_PRIORITY + 2U)
#define AUDIO_DEMO_IO_TIMEOUT_MS       20U
#define AUDIO_DEMO_CLOSE_TIMEOUT_MS    500U
#define AUDIO_DEMO_IDLE_WAIT_MS        50U
#define AUDIO_DEMO_PCM_FRAMES          160U
#define AUDIO_DEMO_PCM_CHANNELS_MAX    2U
#define AUDIO_DEMO_TONE_FREQUENCY_HZ   440U
#define AUDIO_DEMO_TONE_AMPLITUDE      6144
#define AUDIO_DEMO_TASK_STACK_CAPS      (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define AUDIO_DEMO_METER_NOISE_FLOOR    32U
#define AUDIO_DEMO_METER_LOG2_SPAN      9U

typedef enum
{
    AUDIO_DEMO_COMMAND_SET_VOLUME = 0,
    AUDIO_DEMO_COMMAND_SET_MUTE,
    AUDIO_DEMO_COMMAND_PLAY_TONE,
} audio_demo_command_type_t;

typedef struct audio_demo_command
{
    audio_demo_command_type_t type;
    uint32_t request_id;
    union
    {
        uint8_t volume_percent;
        bool muted;
        unsigned cancel_generation;
    } value;
} audio_demo_command_t;

static void _audio_demo_publish(audio_demo_adapter_t *adapter,
                                audio_demo_snapshot_t *snapshot)
{
    ++snapshot->generation;
    if (xSemaphoreTake(adapter->snapshot_lock, portMAX_DELAY) == pdTRUE)
    {
        adapter->snapshot = *snapshot;
        (void)xSemaphoreGive(adapter->snapshot_lock);
    }
}

static void _audio_demo_set_error(audio_demo_adapter_t *adapter,
                                  audio_demo_snapshot_t *snapshot,
                                  esp_err_t result)
{
    snapshot->last_error = result;
    _audio_demo_publish(adapter, snapshot);
}

static bool _audio_demo_format_supported(
    const audio_service_config_t *config)
{
    return config->sample_rate_hz != 0U && config->bits_per_sample == 16U &&
           config->channels > 0U &&
           config->channels <= AUDIO_DEMO_PCM_CHANNELS_MAX;
}

static esp_err_t _audio_demo_start_stream(
    audio_demo_adapter_t *adapter,
    audio_demo_snapshot_t *snapshot,
    audio_service_config_t *config,
    bool *stop_required,
    bool *pa_disable_required)
{
    if (!audio_service_is_available())
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_UNAVAILABLE;
        snapshot->last_error = ESP_ERR_NOT_FOUND;
        _audio_demo_publish(adapter, snapshot);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = audio_service_get_config(config);
    if (result != ESP_OK)
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
        snapshot->last_error = result;
        _audio_demo_publish(adapter, snapshot);
        return result;
    }
    if (!_audio_demo_format_supported(config))
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
        snapshot->last_error = ESP_ERR_NOT_SUPPORTED;
        _audio_demo_publish(adapter, snapshot);
        return ESP_ERR_NOT_SUPPORTED;
    }

    audio_service_state_t service_state = audio_service_get_state();
    if (service_state == AUDIO_SERVICE_STATE_ERROR)
    {
        *stop_required = true;
        esp_err_t result = audio_service_stop();
        if (result != ESP_OK)
        {
            snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
            snapshot->last_error = result;
            _audio_demo_publish(adapter, snapshot);
            return result;
        }
        *stop_required = false;
        service_state = AUDIO_SERVICE_STATE_READY;
    }
    if (service_state != AUDIO_SERVICE_STATE_READY &&
            service_state != AUDIO_SERVICE_STATE_RUNNING)
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_UNAVAILABLE;
        snapshot->last_error = ESP_ERR_INVALID_STATE;
        _audio_demo_publish(adapter, snapshot);
        return ESP_ERR_INVALID_STATE;
    }

    result = audio_service_get_volume(&snapshot->volume_percent);
    if (result == ESP_OK)
    {
        result = audio_service_get_mute(&snapshot->muted);
    }
    if (result == ESP_OK)
    {
        result = audio_service_start();
        *stop_required = result == ESP_OK ||
                         audio_service_get_state() == AUDIO_SERVICE_STATE_ERROR;
    }
    if (result == ESP_OK)
    {
        *pa_disable_required = true;
        result = audio_service_set_pa(false);
        if (result == ESP_OK)
        {
            *pa_disable_required = false;
        }
        else
        {
            const esp_err_t first_error = result;
            const esp_err_t stop_result = audio_service_stop();
            if (stop_result == ESP_OK)
            {
                *stop_required = false;
            }
            result = first_error;
        }
    }
    if (result != ESP_OK)
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
        snapshot->last_error = result;
        _audio_demo_publish(adapter, snapshot);
        return result;
    }

    snapshot->state = AUDIO_DEMO_ADAPTER_RUNNING;
    snapshot->last_error = ESP_OK;
    _audio_demo_publish(adapter, snapshot);
    return ESP_OK;
}

static uint8_t _audio_demo_calculate_level(const int16_t *samples,
        size_t sample_count, uint8_t previous)
{
    uint32_t peak = 0U;
    for (size_t index = 0; index < sample_count; ++index)
    {
        int32_t value = samples[index];
        if (value < 0)
        {
            value = -value;
        }
        if ((uint32_t)value > peak)
        {
            peak = (uint32_t)value;
        }
    }

    uint32_t current = 0U;
    if (peak > AUDIO_DEMO_METER_NOISE_FLOOR)
    {
        uint32_t base = AUDIO_DEMO_METER_NOISE_FLOOR;
        uint32_t octave = 0U;
        while (octave < AUDIO_DEMO_METER_LOG2_SPAN &&
                peak >= (base << 1U))
        {
            base <<= 1U;
            ++octave;
        }
        const uint32_t fraction = ((peak - base) * 100U) / base;
        current = (octave * 100U + fraction) /
                  AUDIO_DEMO_METER_LOG2_SPAN;
        if (current > 100U)
        {
            current = 100U;
        }
    }
    if (current >= previous)
    {
        return (uint8_t)current;
    }
    return (uint8_t)(((uint32_t)previous * 7U + current) / 8U);
}

static void _audio_demo_capture_level(audio_demo_adapter_t *adapter,
                                      audio_demo_snapshot_t *snapshot,
                                      const audio_service_config_t *config,
                                      int16_t *pcm)
{
    const size_t requested = AUDIO_DEMO_PCM_FRAMES * config->channels *
                             sizeof(*pcm);
    size_t received = 0U;
    esp_err_t result = audio_service_read(pcm, requested, &received,
                                          AUDIO_DEMO_IO_TIMEOUT_MS);
    if (result == ESP_OK && received >= sizeof(*pcm))
    {
        snapshot->mic_level_percent = _audio_demo_calculate_level(
                                          pcm, received / sizeof(*pcm),
                                          snapshot->mic_level_percent);
        snapshot->last_error = ESP_OK;
        _audio_demo_publish(adapter, snapshot);
        return;
    }
    if (result == ESP_ERR_TIMEOUT || (result == ESP_OK && received == 0U))
    {
        snapshot->mic_level_percent = (uint8_t)(
                                          ((uint32_t)snapshot->mic_level_percent * 7U) /
                                          8U);
        _audio_demo_publish(adapter, snapshot);
        return;
    }

    snapshot->mic_level_percent = (uint8_t)(
                                      ((uint32_t)snapshot->mic_level_percent * 7U) / 8U);
    _audio_demo_set_error(adapter, snapshot,
                          result == ESP_OK ? ESP_ERR_INVALID_SIZE : result);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_DEMO_IDLE_WAIT_MS));
}

static int16_t _audio_demo_triangle_sample(uint32_t phase)
{
    const uint32_t position = phase >> 16;
    int32_t value;
    if (position < 16384U)
    {
        value = (int32_t)position;
    }
    else if (position < 49152U)
    {
        value = 32768 - (int32_t)position;
    }
    else
    {
        value = (int32_t)position - 65536;
    }
    return (int16_t)((value * AUDIO_DEMO_TONE_AMPLITUDE) / 16384);
}

static size_t _audio_demo_fill_tone(int16_t *pcm, size_t frames,
                                    uint8_t channels, uint32_t phase_step,
                                    uint32_t *phase)
{
    for (size_t frame = 0; frame < frames; ++frame)
    {
        const int16_t sample = _audio_demo_triangle_sample(*phase);
        *phase += phase_step;
        for (uint8_t channel = 0U; channel < channels; ++channel)
        {
            pcm[frame * channels + channel] = sample;
        }
    }
    return frames * channels * sizeof(*pcm);
}

static esp_err_t _audio_demo_drain_input(
    const audio_service_config_t *config, int16_t *pcm)
{
    const size_t requested = AUDIO_DEMO_PCM_FRAMES * config->channels *
                             sizeof(*pcm);
    size_t received = 0U;
    const esp_err_t result = audio_service_read(pcm, requested, &received, 0U);
    return result == ESP_ERR_TIMEOUT ? ESP_OK : result;
}

static esp_err_t _audio_demo_play_tone(
    audio_demo_adapter_t *adapter,
    const audio_service_config_t *config,
    unsigned cancel_generation,
    int16_t *pcm,
    bool *cancelled)
{
    *cancelled = false;
    uint32_t phase = 0U;
    const uint32_t phase_step = (uint32_t)(
                                    ((uint64_t)AUDIO_DEMO_TONE_FREQUENCY_HZ << 32) /
                                    config->sample_rate_hz);
    uint32_t frames_remaining = config->sample_rate_hz;
    while (frames_remaining != 0U)
    {
        if (atomic_load_explicit(&adapter->shutdown_requested,
                                 memory_order_acquire) ||
                atomic_load_explicit(&adapter->cancel_generation,
                                     memory_order_acquire) != cancel_generation)
        {
            *cancelled = true;
            return ESP_OK;
        }

        const size_t frames = frames_remaining > AUDIO_DEMO_PCM_FRAMES ?
                              AUDIO_DEMO_PCM_FRAMES : frames_remaining;
        const size_t bytes = _audio_demo_fill_tone(
                                 pcm, frames, config->channels, phase_step, &phase);
        size_t written = 0U;
        esp_err_t result = audio_service_write(pcm, bytes, &written,
                                               AUDIO_DEMO_IO_TIMEOUT_MS);
        if (result != ESP_OK)
        {
            return result;
        }
        if (written != bytes)
        {
            return ESP_ERR_INVALID_SIZE;
        }
        result = _audio_demo_drain_input(config, pcm);
        if (result != ESP_OK)
        {
            return result;
        }
        frames_remaining -= (uint32_t)frames;
    }
    return ESP_OK;
}

static void _audio_demo_process_command(
    audio_demo_adapter_t *adapter,
    audio_demo_snapshot_t *snapshot,
    const audio_service_config_t *config,
    const audio_demo_command_t *command,
    bool *stream_running,
    bool *stop_required,
    bool *pa_disable_required,
    int16_t *pcm)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    switch (command->type)
    {
    case AUDIO_DEMO_COMMAND_SET_VOLUME:
        if (*stream_running)
        {
            result = audio_service_set_volume(command->value.volume_percent);
            if (result == ESP_OK)
            {
                snapshot->volume_percent = command->value.volume_percent;
                snapshot->last_error = ESP_OK;
            }
        }
        snapshot->volume_request_id = command->request_id;
        snapshot->volume_result = result;
        if (result == ESP_OK)
        {
            _audio_demo_publish(adapter, snapshot);
        }
        break;
    case AUDIO_DEMO_COMMAND_SET_MUTE:
        if (*stream_running)
        {
            result = audio_service_set_mute(command->value.muted);
            if (result == ESP_OK)
            {
                snapshot->muted = command->value.muted;
                snapshot->last_error = ESP_OK;
            }
        }
        snapshot->mute_request_id = command->request_id;
        snapshot->mute_result = result;
        if (result == ESP_OK)
        {
            _audio_demo_publish(adapter, snapshot);
        }
        break;
    case AUDIO_DEMO_COMMAND_PLAY_TONE:
        if (*stream_running &&
                atomic_load_explicit(&adapter->cancel_generation,
                                     memory_order_acquire) ==
                command->value.cancel_generation)
        {
            snapshot->tone_state = AUDIO_DEMO_TONE_PLAYING;
            snapshot->last_error = ESP_OK;
            _audio_demo_publish(adapter, snapshot);
            bool cancelled = false;
            *pa_disable_required = true;
            result = audio_service_set_pa(true);
            if (result == ESP_OK)
            {
                result = _audio_demo_play_tone(
                             adapter, config, command->value.cancel_generation,
                             pcm, &cancelled);
            }
            const esp_err_t pa_result = audio_service_set_pa(false);
            if (pa_result == ESP_OK)
            {
                *pa_disable_required = false;
            }
            else
            {
                if (result == ESP_OK)
                {
                    result = pa_result;
                }
                *stream_running = false;
                const esp_err_t stop_result = audio_service_stop();
                if (stop_result == ESP_OK)
                {
                    *stop_required = false;
                }
                snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
            }
            snapshot->tone_state = result != ESP_OK ? AUDIO_DEMO_TONE_ERROR :
                                   (cancelled ? AUDIO_DEMO_TONE_CANCELLED :
                                    AUDIO_DEMO_TONE_COMPLETE);
        }
        else
        {
            snapshot->tone_state = AUDIO_DEMO_TONE_CANCELLED;
            result = ESP_OK;
        }
        atomic_store_explicit(&adapter->tone_pending, false,
                              memory_order_release);
        break;
    default:
        result = ESP_ERR_INVALID_ARG;
        break;
    }

    if (result != ESP_OK)
    {
        _audio_demo_set_error(adapter, snapshot, result);
    }
    else if (command->type == AUDIO_DEMO_COMMAND_PLAY_TONE)
    {
        snapshot->last_error = ESP_OK;
        _audio_demo_publish(adapter, snapshot);
    }
}

static bool _audio_demo_close_stream(audio_demo_adapter_t *adapter,
                                     audio_demo_snapshot_t *snapshot,
                                     bool *stop_required,
                                     bool *pa_disable_required,
                                     unsigned request_id)
{
    snapshot->state = AUDIO_DEMO_ADAPTER_STOPPING;
    snapshot->mic_level_percent = 0U;
    _audio_demo_publish(adapter, snapshot);

    esp_err_t result = ESP_OK;
    if (*pa_disable_required)
    {
        const esp_err_t pa_result = audio_service_set_pa(false);
        if (pa_result == ESP_OK)
        {
            *pa_disable_required = false;
        }
        else
        {
            result = pa_result;
        }
    }
    if (*stop_required)
    {
        const esp_err_t stop_result = audio_service_stop();
        if (stop_result == ESP_OK)
        {
            *stop_required = false;
        }
        else if (result == ESP_OK)
        {
            result = stop_result;
        }
    }
    atomic_store_explicit(&adapter->tone_pending, false, memory_order_release);
    atomic_store_explicit(&adapter->close_result, result, memory_order_release);
    if (result != ESP_OK)
    {
        snapshot->state = AUDIO_DEMO_ADAPTER_ERROR;
        snapshot->last_error = result;
        _audio_demo_publish(adapter, snapshot);
        atomic_store_explicit(&adapter->shutdown_requested, false,
                              memory_order_release);
        if (atomic_load_explicit(&adapter->close_request_id,
                                 memory_order_acquire) != request_id)
        {
            atomic_store_explicit(&adapter->shutdown_requested, true,
                                  memory_order_release);
        }
        atomic_store_explicit(&adapter->close_completion_id, request_id,
                              memory_order_release);
        (void)xSemaphoreGive(adapter->close_done);
        return false;
    }

    snapshot->state = AUDIO_DEMO_ADAPTER_CLOSED;
    snapshot->last_error = ESP_OK;
    _audio_demo_publish(adapter, snapshot);
    const unsigned completion_id = atomic_load_explicit(
                                       &adapter->close_request_id,
                                       memory_order_acquire);
    atomic_store_explicit(&adapter->close_completion_id, completion_id,
                          memory_order_release);
    atomic_store_explicit(&adapter->worker_exited, true, memory_order_release);
    (void)xSemaphoreGive(adapter->close_done);
    return true;
}

static void _audio_demo_worker(void *context)
{
    audio_demo_adapter_t *adapter = context;
    audio_demo_snapshot_t snapshot = adapter->snapshot;
    audio_service_config_t config = {0};
    bool stop_required = false;
    bool pa_disable_required = false;
    bool stream_running = _audio_demo_start_stream(
                              adapter, &snapshot, &config,
                              &stop_required,
                              &pa_disable_required) == ESP_OK;
    bool cleanup_pending = false;
    int16_t pcm[AUDIO_DEMO_PCM_FRAMES * AUDIO_DEMO_PCM_CHANNELS_MAX];

    for (;;)
    {
        if (atomic_load_explicit(&adapter->shutdown_requested,
                                 memory_order_acquire))
        {
            const unsigned request_id = atomic_load_explicit(
                                            &adapter->close_request_id,
                                            memory_order_acquire);
            if (_audio_demo_close_stream(adapter, &snapshot, &stop_required,
                                         &pa_disable_required, request_id))
            {
                break;
            }
            cleanup_pending = true;
        }
        if (cleanup_pending)
        {
            if (!atomic_load_explicit(&adapter->shutdown_requested,
                                      memory_order_acquire))
            {
                (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            continue;
        }

        (void)ulTaskNotifyTake(pdTRUE, 0U);

        audio_demo_command_t command;
        if (xQueueReceive(adapter->commands, &command, 0U) == pdTRUE)
        {
            _audio_demo_process_command(adapter, &snapshot, &config,
                                        &command, &stream_running,
                                        &stop_required, &pa_disable_required,
                                        pcm);
            continue;
        }

        if (stream_running)
        {
            _audio_demo_capture_level(adapter, &snapshot, &config, pcm);
        }
        else
        {
            (void)ulTaskNotifyTake(pdTRUE,
                                   pdMS_TO_TICKS(AUDIO_DEMO_IDLE_WAIT_MS));
        }
    }

    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static bool _audio_demo_adapter_empty(const audio_demo_adapter_t *adapter)
{
    return adapter->commands == NULL && adapter->snapshot_lock == NULL &&
           adapter->close_done == NULL && adapter->worker == NULL;
}

static void _audio_demo_release_handles(audio_demo_adapter_t *adapter)
{
    atomic_store_explicit(&adapter->accepting_commands, false,
                          memory_order_release);
    if (adapter->close_done != NULL)
    {
        vSemaphoreDelete(adapter->close_done);
        adapter->close_done = NULL;
    }
    if (adapter->commands != NULL)
    {
        vQueueDelete(adapter->commands);
        adapter->commands = NULL;
    }
    if (adapter->snapshot_lock != NULL)
    {
        vSemaphoreDelete(adapter->snapshot_lock);
        adapter->snapshot_lock = NULL;
    }
    adapter->worker = NULL;
}

esp_err_t audio_demo_adapter_open(audio_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_audio_demo_adapter_empty(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(adapter, 0, sizeof(*adapter));
    atomic_init(&adapter->cancel_generation, 1U);
    atomic_init(&adapter->accepting_commands, false);
    atomic_init(&adapter->tone_pending, false);
    atomic_init(&adapter->shutdown_requested, false);
    atomic_init(&adapter->worker_exited, false);
    atomic_init(&adapter->close_request_id, 0U);
    atomic_init(&adapter->close_completion_id, 0U);
    atomic_init(&adapter->close_result, ESP_OK);
    adapter->snapshot.state = AUDIO_DEMO_ADAPTER_STARTING;
    adapter->snapshot.last_error = ESP_OK;
    adapter->snapshot.volume_result = ESP_OK;
    adapter->snapshot.mute_result = ESP_OK;
    adapter->snapshot.generation = 1U;

    adapter->snapshot_lock = xSemaphoreCreateMutex();
    if (adapter->snapshot_lock == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    adapter->commands = xQueueCreate(AUDIO_DEMO_COMMAND_COUNT,
                                     sizeof(audio_demo_command_t));
    if (adapter->commands == NULL)
    {
        _audio_demo_release_handles(adapter);
        return ESP_ERR_NO_MEM;
    }
    adapter->close_done = xSemaphoreCreateBinary();
    if (adapter->close_done == NULL)
    {
        _audio_demo_release_handles(adapter);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreateWithCaps(_audio_demo_worker, "audio_demo",
                            AUDIO_DEMO_TASK_STACK_BYTES, adapter,
                            AUDIO_DEMO_TASK_PRIORITY, &adapter->worker,
                            AUDIO_DEMO_TASK_STACK_CAPS) != pdPASS)
    {
        _audio_demo_release_handles(adapter);
        return ESP_ERR_NO_MEM;
    }
    atomic_store_explicit(&adapter->accepting_commands, true,
                          memory_order_release);
    return ESP_OK;
}

esp_err_t audio_demo_adapter_close(audio_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (_audio_demo_adapter_empty(adapter))
    {
        return ESP_OK;
    }
    if (adapter->worker == NULL || adapter->close_done == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store_explicit(&adapter->accepting_commands, false,
                          memory_order_release);
    unsigned request_id = atomic_load_explicit(&adapter->close_request_id,
                          memory_order_acquire);
    if (!atomic_load_explicit(&adapter->worker_exited, memory_order_acquire))
    {
        request_id = atomic_fetch_add_explicit(&adapter->close_request_id, 1U,
                                               memory_order_acq_rel) + 1U;
        (void)atomic_fetch_add_explicit(&adapter->cancel_generation, 1U,
                                        memory_order_acq_rel);
        atomic_store_explicit(&adapter->shutdown_requested, true,
                              memory_order_release);
        (void)xTaskNotifyGive(adapter->worker);
    }

    for (;;)
    {
        if (xSemaphoreTake(
                    adapter->close_done,
                    pdMS_TO_TICKS(AUDIO_DEMO_CLOSE_TIMEOUT_MS)) != pdTRUE)
        {
            return ESP_ERR_TIMEOUT;
        }
        const esp_err_t completed_result = (esp_err_t)atomic_load_explicit(
                                               &adapter->close_result,
                                               memory_order_acquire);
        if (atomic_load_explicit(&adapter->worker_exited,
                                 memory_order_acquire))
        {
            if (completed_result != ESP_OK)
            {
                return completed_result;
            }
            break;
        }
        if (atomic_load_explicit(&adapter->close_completion_id,
                                 memory_order_acquire) == request_id)
        {
            return completed_result == ESP_OK ? ESP_ERR_INVALID_STATE :
                   completed_result;
        }
    }

    esp_err_t result = (esp_err_t)atomic_load_explicit(
                           &adapter->close_result, memory_order_acquire);
    if (result != ESP_OK)
    {
        return result;
    }
    if (!atomic_load_explicit(&adapter->worker_exited, memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDeleteWithCaps(adapter->worker);
    _audio_demo_release_handles(adapter);
    memset(&adapter->snapshot, 0, sizeof(adapter->snapshot));
    return ESP_OK;
}

esp_err_t audio_demo_adapter_get_snapshot(
    audio_demo_adapter_t *adapter,
    audio_demo_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->snapshot_lock == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(adapter->snapshot_lock, 0U) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    *snapshot = adapter->snapshot;
    (void)xSemaphoreGive(adapter->snapshot_lock);
    return ESP_OK;
}

static esp_err_t _audio_demo_queue_command(
    audio_demo_adapter_t *adapter,
    const audio_demo_command_t *command)
{
    if (adapter == NULL || command == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->commands == NULL || adapter->worker == NULL ||
            !atomic_load_explicit(&adapter->accepting_commands,
                                  memory_order_acquire) ||
            atomic_load_explicit(&adapter->shutdown_requested,
                                 memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(adapter->commands, command, 0U) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    (void)xTaskNotifyGive(adapter->worker);
    return ESP_OK;
}

esp_err_t audio_demo_adapter_play_tone(audio_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->commands == NULL || adapter->worker == NULL ||
            !atomic_load_explicit(&adapter->accepting_commands,
                                  memory_order_acquire) ||
            atomic_load_explicit(&adapter->worker_exited,
                                 memory_order_acquire) ||
            atomic_load_explicit(&adapter->shutdown_requested,
                                 memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
                &adapter->tone_pending, &expected, true,
                memory_order_acq_rel, memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }

    const audio_demo_command_t command =
    {
        .type = AUDIO_DEMO_COMMAND_PLAY_TONE,
        .value.cancel_generation = atomic_load_explicit(
            &adapter->cancel_generation,
            memory_order_acquire),
    };
    esp_err_t result = _audio_demo_queue_command(adapter, &command);
    if (result != ESP_OK)
    {
        atomic_store_explicit(&adapter->tone_pending, false,
                              memory_order_release);
    }
    return result;
}

esp_err_t audio_demo_adapter_cancel_tone(audio_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->worker == NULL ||
            !atomic_load_explicit(&adapter->accepting_commands,
                                  memory_order_acquire) ||
            atomic_load_explicit(&adapter->worker_exited,
                                 memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_load_explicit(&adapter->tone_pending, memory_order_acquire))
    {
        return ESP_ERR_NOT_FOUND;
    }
    (void)atomic_fetch_add_explicit(&adapter->cancel_generation, 1U,
                                    memory_order_acq_rel);
    if (adapter->worker != NULL)
    {
        (void)xTaskNotifyGive(adapter->worker);
    }
    return ESP_OK;
}

esp_err_t audio_demo_adapter_set_volume(audio_demo_adapter_t *adapter,
                                        uint8_t percent,
                                        uint32_t *request_id)
{
    if (adapter == NULL || percent > 100U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t next_request_id = adapter->next_volume_request_id + 1U;
    if (next_request_id == 0U)
    {
        next_request_id = 1U;
    }
    const audio_demo_command_t command =
    {
        .type = AUDIO_DEMO_COMMAND_SET_VOLUME,
        .request_id = next_request_id,
        .value.volume_percent = percent,
    };
    esp_err_t result = _audio_demo_queue_command(adapter, &command);
    if (result == ESP_OK)
    {
        adapter->next_volume_request_id = next_request_id;
        if (request_id != NULL)
        {
            *request_id = next_request_id;
        }
    }
    return result;
}

esp_err_t audio_demo_adapter_set_mute(audio_demo_adapter_t *adapter,
                                      bool muted,
                                      uint32_t *request_id)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t next_request_id = adapter->next_mute_request_id + 1U;
    if (next_request_id == 0U)
    {
        next_request_id = 1U;
    }
    const audio_demo_command_t command =
    {
        .type = AUDIO_DEMO_COMMAND_SET_MUTE,
        .request_id = next_request_id,
        .value.muted = muted,
    };
    esp_err_t result = _audio_demo_queue_command(adapter, &command);
    if (result == ESP_OK)
    {
        adapter->next_mute_request_id = next_request_id;
        if (request_id != NULL)
        {
            *request_id = next_request_id;
        }
    }
    return result;
}

bool audio_demo_adapter_is_open(const audio_demo_adapter_t *adapter)
{
    return adapter != NULL && !_audio_demo_adapter_empty(adapter);
}

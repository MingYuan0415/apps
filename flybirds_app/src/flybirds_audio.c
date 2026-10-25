#define DBG_TAG "flybirds_audio"
#include "mt_log.h"

#include "flybirds_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLYBIRDS_AUDIO_BLOCK_MS \
    ((FLYBIRDS_SYNTH_BLOCK_FRAMES * 1000U) / FLYBIRDS_SYNTH_SAMPLE_RATE)
/** Retries for the unsent tail of one block before it is dropped. */
#define FLYBIRDS_AUDIO_BLOCK_RETRIES 3U
/** Pause between two attempts at the same block tail. */
#define FLYBIRDS_AUDIO_RETRY_DELAY_MS 5U
/** Dropped blocks that mark a stalled transport. */
#define FLYBIRDS_AUDIO_MAX_DROPPED_BLOCKS 5U
/** Let the DMA drain before rendering fresh blocks after a stall. */
#define FLYBIRDS_AUDIO_RESYNC_DELAY_MS 200U

static flybirds_synth_t s_synth;
static int16_t s_pcm[FLYBIRDS_AUDIO_BLOCK_FRAMES *
                     FLYBIRDS_SYNTH_CHANNELS];
static TaskHandle_t s_task;
static SemaphoreHandle_t s_exit;
static atomic_bool s_run;
static atomic_bool s_task_alive;
static atomic_bool s_music;
static atomic_bool s_sfx;
static atomic_uint s_pending_sfx;
static atomic_bool s_fault_reported;

static void _flybirds_audio_apply_control(void)
{
    flybirds_synth_set_music(&s_synth,
                             atomic_load_explicit(&s_music,
                                     memory_order_relaxed));
    flybirds_synth_set_sfx(&s_synth,
                           atomic_load_explicit(&s_sfx,
                                   memory_order_relaxed));
    const uint32_t pending = atomic_exchange_explicit(&s_pending_sfx, 0U,
        memory_order_acquire);
    for (uint32_t kind = 0U; kind < (uint32_t)FLYBIRDS_SFX_COUNT; ++kind)
    {
        if ((pending & (1U << kind)) != 0U)
        {
            flybirds_synth_trigger(&s_synth, (flybirds_sfx_t)kind);
        }
    }
}

static void _flybirds_audio_task(void *argument)
{
    (void)argument;
    TickType_t next = xTaskGetTickCount();
    size_t offset = 0U;
    uint32_t retries = 0U;
    uint32_t dropped = 0U;
    bool block_ready = false;

    while (atomic_load_explicit(&s_run, memory_order_acquire))
    {
        if (!block_ready)
        {
            _flybirds_audio_apply_control();
            flybirds_synth_render(&s_synth, s_pcm,
                                  FLYBIRDS_AUDIO_BLOCK_FRAMES);
            offset = 0U;
            retries = 0U;
            block_ready = true;
        }

        size_t written = 0U;
        const esp_err_t result = flybirds_audio_session_write(
                                     s_pcm + offset * FLYBIRDS_SYNTH_CHANNELS,
                                     FLYBIRDS_AUDIO_BLOCK_FRAMES - offset,
                                     &written);
        if (written > FLYBIRDS_AUDIO_BLOCK_FRAMES - offset)
        {
            written = FLYBIRDS_AUDIO_BLOCK_FRAMES - offset;
        }

        if (result == ESP_OK)
        {
            offset += written;
            if (offset < FLYBIRDS_AUDIO_BLOCK_FRAMES)
            {
                continue; /* Short success: top up the same block. */
            }
            block_ready = false; /* Full block: render and pace afresh. */
            retries = 0U;
            dropped = 0U;
            next += pdMS_TO_TICKS(FLYBIRDS_AUDIO_BLOCK_MS);
            const TickType_t now = xTaskGetTickCount();
            if ((int32_t)(next - now) > 0)
            {
                vTaskDelay(next - now);
            }
            else
            {
                next = now;
            }
            continue;
        }

        if (result == ESP_ERR_TIMEOUT)
        {
            offset += written;
            if (offset >= FLYBIRDS_AUDIO_BLOCK_FRAMES)
            {
                block_ready = false;
                retries = 0U;
                dropped = 0U;
                continue;
            }
            ++retries;
            if (retries >= FLYBIRDS_AUDIO_BLOCK_RETRIES)
            {
                /* The transport is behind: abandon the tail and render a fresh
                 * block instead of replaying audio that is already late. */
                block_ready = false;
                retries = 0U;
                ++dropped;
                if (dropped >= FLYBIRDS_AUDIO_MAX_DROPPED_BLOCKS)
                {
                    if (!atomic_exchange_explicit(&s_fault_reported, true,
                                                  memory_order_relaxed))
                    {
                        LOG_W("audio transport stalled; resynchronising");
                    }
                    vTaskDelay(pdMS_TO_TICKS(FLYBIRDS_AUDIO_RESYNC_DELAY_MS));
                    dropped = 0U;
                }
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(FLYBIRDS_AUDIO_RETRY_DELAY_MS));
            continue;
        }

        /* The stream was stopped or rejected: leave quietly instead of
         * spinning on a dead path. The page reopens on its retry tick. */
        if (!atomic_exchange_explicit(&s_fault_reported, true,
                                      memory_order_relaxed))
        {
            LOG_W("audio stream lost (%d)", (int)result);
        }
        break;
    }

    /* Completion publishes the last access to shared writer state. */
    atomic_store_explicit(&s_task_alive, false, memory_order_release);
    (void)xSemaphoreGive(s_exit);
    vTaskDelete(NULL);
}

/** @brief Consume completion before reusing the writer's handles or buffers. */
static esp_err_t _flybirds_audio_reap(void)
{
    if (s_exit == NULL)
    {
        s_task = NULL;
        return ESP_OK;
    }
    atomic_store_explicit(&s_run, false, memory_order_release);
    if (xSemaphoreTake(s_exit, pdMS_TO_TICKS(FLYBIRDS_AUDIO_STOP_TIMEOUT_MS)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(s_exit);
    s_exit = NULL;
    s_task = NULL;
    return ESP_OK;
}

esp_err_t flybirds_audio_open(bool music, bool sfx)
{
    if (s_task != NULL &&
            atomic_load_explicit(&s_task_alive, memory_order_acquire) &&
            atomic_load_explicit(&s_run, memory_order_acquire))
    {
        return ESP_OK;
    }
    esp_err_t result = _flybirds_audio_reap();
    if (result != ESP_OK)
    {
        return result;
    }
    result = flybirds_audio_session_release();
    if (result != ESP_OK)
    {
        return result;
    }

    if (!flybirds_audio_session_acquire())
    {
        return ESP_ERR_NOT_FOUND;
    }
    flybirds_synth_init(&s_synth);
    atomic_store_explicit(&s_music, music, memory_order_relaxed);
    atomic_store_explicit(&s_sfx, sfx, memory_order_relaxed);
    atomic_store_explicit(&s_pending_sfx, 0U, memory_order_relaxed);
    atomic_store_explicit(&s_fault_reported, false, memory_order_relaxed);
    s_exit = xSemaphoreCreateBinary();
    if (s_exit == NULL)
    {
        (void)flybirds_audio_session_release();
        return ESP_ERR_NO_MEM;
    }
    atomic_store_explicit(&s_run, true, memory_order_release);
    atomic_store_explicit(&s_task_alive, true, memory_order_release);
    if (xTaskCreatePinnedToCore(_flybirds_audio_task, "flybirds_audio",
                                FLYBIRDS_AUDIO_TASK_STACK, NULL,
                                FLYBIRDS_AUDIO_TASK_PRIORITY, &s_task,
                                CONFIG_MAIN_PROJECT_TASK_CORE_ID) != pdPASS)
    {
        s_task = NULL;
        atomic_store_explicit(&s_task_alive, false, memory_order_release);
        atomic_store_explicit(&s_run, false, memory_order_release);
        vSemaphoreDelete(s_exit);
        s_exit = NULL;
        (void)flybirds_audio_session_release();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t flybirds_audio_close(void)
{
    const esp_err_t result = _flybirds_audio_reap();
    if (result != ESP_OK)
    {
        return result;
    }
    return flybirds_audio_session_release();
}

void flybirds_audio_set_music(bool enabled)
{
    atomic_store_explicit(&s_music, enabled, memory_order_relaxed);
}

void flybirds_audio_set_sfx(bool enabled)
{
    atomic_store_explicit(&s_sfx, enabled, memory_order_relaxed);
    if (!enabled)
    {
        atomic_store_explicit(&s_pending_sfx, 0U, memory_order_relaxed);
    }
}

void flybirds_audio_trigger(flybirds_sfx_t sfx)
{
    if ((unsigned)sfx >= (unsigned)FLYBIRDS_SFX_COUNT)
    {
        return;
    }
    (void)atomic_fetch_or_explicit(&s_pending_sfx, 1U << (unsigned)sfx,
                                   memory_order_release);
}

bool flybirds_audio_is_open(void)
{
    return s_task != NULL &&
           atomic_load_explicit(&s_task_alive, memory_order_acquire) &&
           atomic_load_explicit(&s_run, memory_order_acquire);
}

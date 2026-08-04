#define DBG_TAG "clock_demo_adapter"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "clock_demo_adapter.h"

#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

#include <string.h>
#include <time.h>

#include "time_service.h"

#define CLOCK_DEMO_COMMAND_SYNC       BIT0
#define CLOCK_DEMO_COMMAND_ARM        BIT1
#define CLOCK_DEMO_COMMAND_DISARM     BIT2
#define CLOCK_DEMO_COMMAND_STOP       BIT3
#define CLOCK_DEMO_EVENT_CLOSE_DONE   BIT0
#define CLOCK_DEMO_EVENT_WORKER_EXIT  BIT1
#define CLOCK_DEMO_TASK_STACK         3072U
#define CLOCK_DEMO_TASK_PRIORITY      4U
#define CLOCK_DEMO_CLOSE_TIMEOUT_MS   2000U
#define CLOCK_DEMO_ALARM_DELAY_SEC    10
#define CLOCK_DEMO_TASK_STACK_CAPS    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static void _clock_demo_revision_next(clock_demo_adapter_snapshot_t *snapshot)
{
    ++snapshot->revision;
    if (snapshot->revision == 0U)
    {
        snapshot->revision = 1U;
    }
}

static bool _clock_demo_resources_present(const clock_demo_adapter_t *adapter)
{
    return adapter != NULL &&
           (adapter->worker != NULL || adapter->lock != NULL ||
            adapter->events != NULL);
}

static void _clock_demo_set_command_running(clock_demo_adapter_t *adapter,
        uint32_t command)
{
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (command == CLOCK_DEMO_COMMAND_SYNC)
    {
        adapter->snapshot.sync_state = CLOCK_DEMO_OPERATION_RUNNING;
    }
    else
    {
        adapter->snapshot.alarm_state = CLOCK_DEMO_OPERATION_RUNNING;
    }
    _clock_demo_revision_next(&adapter->snapshot);
    xSemaphoreGive(adapter->lock);
}

static void _clock_demo_complete_sync(clock_demo_adapter_t *adapter,
                                      esp_err_t result)
{
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.sync_result = result;
    adapter->snapshot.sync_state = result == ESP_OK ?
                                   CLOCK_DEMO_OPERATION_DONE :
                                   CLOCK_DEMO_OPERATION_FAILED;
    adapter->command_pending = false;
    _clock_demo_revision_next(&adapter->snapshot);
    xSemaphoreGive(adapter->lock);
}

static void _clock_demo_complete_alarm(clock_demo_adapter_t *adapter,
                                       esp_err_t result)
{
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.alarm_result = result;
    adapter->snapshot.alarm_state = result == ESP_OK ?
                                    CLOCK_DEMO_OPERATION_DONE :
                                    CLOCK_DEMO_OPERATION_FAILED;
    adapter->command_pending = false;
    _clock_demo_revision_next(&adapter->snapshot);
    xSemaphoreGive(adapter->lock);
}

static esp_err_t _clock_demo_request_sync(clock_demo_adapter_t *adapter)
{
    esp_err_t result = time_service_request_sync();
    _clock_demo_complete_sync(adapter, result);
    return result;
}

static esp_err_t _clock_demo_disable_alarm(clock_demo_adapter_t *adapter)
{
    bool owned;
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    owned = adapter->snapshot.alarm_owned;
    xSemaphoreGive(adapter->lock);

    esp_err_t result = owned ? time_service_alarm_disable() : ESP_OK;
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (result == ESP_OK)
    {
        adapter->snapshot.alarm_owned = false;
    }
    xSemaphoreGive(adapter->lock);
    return result;
}

static esp_err_t _clock_demo_alarm_target(struct tm *target)
{
    if (time_service_get_quality() == TIME_SERVICE_QUALITY_INVALID)
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm utc_time;
    esp_err_t result = time_service_get_utc(&utc_time);
    if (result != ESP_OK)
    {
        return result;
    }

    time_t now = time(NULL);
    if (now == (time_t) -1)
    {
        return ESP_FAIL;
    }
    const time_t target_epoch = now + CLOCK_DEMO_ALARM_DELAY_SEC;
    return gmtime_r(&target_epoch, target) != NULL ? ESP_OK : ESP_FAIL;
}

static esp_err_t _clock_demo_arm_alarm(clock_demo_adapter_t *adapter)
{
    bool owned;
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    owned = adapter->snapshot.alarm_owned;
    xSemaphoreGive(adapter->lock);

    esp_err_t result = ESP_OK;
    if (owned)
    {
        result = _clock_demo_disable_alarm(adapter);
        if (result != ESP_OK)
        {
            return result;
        }
    }
    else
    {
        time_service_alarm_status_t status;
        result = time_service_alarm_get_status(&status);
        if (result != ESP_OK)
        {
            return result;
        }
        if (status.enabled)
        {
            return ESP_ERR_INVALID_STATE;
        }
    }

    struct tm target;
    result = _clock_demo_alarm_target(&target);
    if (result != ESP_OK)
    {
        return result;
    }
    const time_service_alarm_config_t config =
    {
        .match_second = true,
        .second = (uint8_t)target.tm_sec,
        .match_minute = true,
        .minute = (uint8_t)target.tm_min,
        .match_hour = true,
        .hour = (uint8_t)target.tm_hour,
        .match_day = true,
        .day = (uint8_t)target.tm_mday,
        .match_weekday = true,
        .weekday = (uint8_t)target.tm_wday,
    };
    result = time_service_alarm_configure(&config);

    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (result == ESP_OK)
    {
        adapter->snapshot.alarm_owned = true;
        adapter->snapshot.alarm_hour = config.hour;
        adapter->snapshot.alarm_minute = config.minute;
        adapter->snapshot.alarm_second = config.second;
    }
    xSemaphoreGive(adapter->lock);
    return result;
}

static void _clock_demo_process_command(clock_demo_adapter_t *adapter,
                                        uint32_t command)
{
    _clock_demo_set_command_running(adapter, command);
    if (command == CLOCK_DEMO_COMMAND_SYNC)
    {
        (void)_clock_demo_request_sync(adapter);
        return;
    }

    esp_err_t result = command == CLOCK_DEMO_COMMAND_ARM ?
                       _clock_demo_arm_alarm(adapter) :
                       _clock_demo_disable_alarm(adapter);
    _clock_demo_complete_alarm(adapter, result);
}

static esp_err_t _clock_demo_cleanup_owned(clock_demo_adapter_t *adapter)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t result = _clock_demo_disable_alarm(adapter);
    if (result != ESP_OK)
    {
        first_error = result;
    }

    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.cleanup_result = first_error;
    adapter->snapshot.alarm_state = adapter->snapshot.alarm_owned ?
                                    CLOCK_DEMO_OPERATION_FAILED :
                                    CLOCK_DEMO_OPERATION_IDLE;
    adapter->snapshot.sync_state = CLOCK_DEMO_OPERATION_IDLE;
    adapter->snapshot.alarm_result = adapter->snapshot.alarm_owned ?
                                     first_error : ESP_OK;
    adapter->snapshot.sync_result = ESP_OK;
    adapter->command_pending = false;
    _clock_demo_revision_next(&adapter->snapshot);
    xSemaphoreGive(adapter->lock);
    return first_error;
}

static void _clock_demo_worker(void *context)
{
    clock_demo_adapter_t *adapter = context;
    for (;;)
    {
        uint32_t commands = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &commands, portMAX_DELAY);
        if ((commands & CLOCK_DEMO_COMMAND_STOP) != 0U)
        {
            const esp_err_t result = _clock_demo_cleanup_owned(adapter);
            if (result == ESP_OK)
            {
                xEventGroupSetBits(adapter->events,
                                   CLOCK_DEMO_EVENT_CLOSE_DONE |
                                   CLOCK_DEMO_EVENT_WORKER_EXIT);
                atomic_store_explicit(&adapter->worker_tail_complete, true,
                                      memory_order_release);
                for (;;)
                {
                    vTaskDelay(portMAX_DELAY);
                }
            }
            xEventGroupSetBits(adapter->events, CLOCK_DEMO_EVENT_CLOSE_DONE);
            continue;
        }
        if ((commands & CLOCK_DEMO_COMMAND_SYNC) != 0U)
        {
            _clock_demo_process_command(adapter, CLOCK_DEMO_COMMAND_SYNC);
        }
        else if ((commands & CLOCK_DEMO_COMMAND_ARM) != 0U)
        {
            _clock_demo_process_command(adapter, CLOCK_DEMO_COMMAND_ARM);
        }
        else if ((commands & CLOCK_DEMO_COMMAND_DISARM) != 0U)
        {
            _clock_demo_process_command(adapter, CLOCK_DEMO_COMMAND_DISARM);
        }
    }
}

static esp_err_t _clock_demo_queue_command(clock_demo_adapter_t *adapter,
        uint32_t command)
{
    if (adapter == NULL || adapter->lock == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    if (adapter->worker == NULL || adapter->snapshot.closing ||
            adapter->command_pending)
    {
        xSemaphoreGive(adapter->lock);
        return result;
    }

    adapter->command_pending = true;
    if (command == CLOCK_DEMO_COMMAND_SYNC)
    {
        adapter->snapshot.sync_state = CLOCK_DEMO_OPERATION_QUEUED;
    }
    else
    {
        adapter->snapshot.alarm_state = CLOCK_DEMO_OPERATION_QUEUED;
    }
    _clock_demo_revision_next(&adapter->snapshot);
    result = xTaskNotify(adapter->worker, command, eSetBits) == pdPASS ?
             ESP_OK : ESP_FAIL;
    if (result != ESP_OK)
    {
        adapter->command_pending = false;
        if (command == CLOCK_DEMO_COMMAND_SYNC)
        {
            adapter->snapshot.sync_state = CLOCK_DEMO_OPERATION_FAILED;
            adapter->snapshot.sync_result = result;
        }
        else
        {
            adapter->snapshot.alarm_state = CLOCK_DEMO_OPERATION_FAILED;
            adapter->snapshot.alarm_result = result;
        }
        _clock_demo_revision_next(&adapter->snapshot);
    }
    xSemaphoreGive(adapter->lock);
    return result;
}

esp_err_t clock_demo_adapter_open(clock_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (_clock_demo_resources_present(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(adapter, 0, sizeof(*adapter));
    atomic_init(&adapter->worker_tail_complete, false);
    adapter->lock = xSemaphoreCreateMutex();
    if (adapter->lock == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    adapter->events = xEventGroupCreate();
    if (adapter->events == NULL)
    {
        vSemaphoreDelete(adapter->lock);
        memset(adapter, 0, sizeof(*adapter));
        return ESP_ERR_NO_MEM;
    }
    adapter->snapshot.sync_result = ESP_OK;
    adapter->snapshot.alarm_result = ESP_OK;
    adapter->snapshot.cleanup_result = ESP_OK;
    adapter->snapshot.revision = 1U;

    if (xTaskCreatePinnedToCoreWithCaps(
                _clock_demo_worker, "clock_demo", CLOCK_DEMO_TASK_STACK,
                adapter, CLOCK_DEMO_TASK_PRIORITY, &adapter->worker,
                CONFIG_MAIN_PROJECT_TASK_CORE_ID,
                CLOCK_DEMO_TASK_STACK_CAPS) != pdPASS)
    {
        vEventGroupDelete(adapter->events);
        vSemaphoreDelete(adapter->lock);
        memset(adapter, 0, sizeof(*adapter));
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU0 || \
    CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU1
    LOG_I("task affinity name=clock_demo core=%d",
          (int)xTaskGetCoreID(adapter->worker));
#endif
    return ESP_OK;
}

esp_err_t clock_demo_adapter_request_sync(clock_demo_adapter_t *adapter)
{
    return _clock_demo_queue_command(adapter, CLOCK_DEMO_COMMAND_SYNC);
}

esp_err_t clock_demo_adapter_arm_alarm(clock_demo_adapter_t *adapter)
{
    return _clock_demo_queue_command(adapter, CLOCK_DEMO_COMMAND_ARM);
}

esp_err_t clock_demo_adapter_disarm_alarm(clock_demo_adapter_t *adapter)
{
    return _clock_demo_queue_command(adapter, CLOCK_DEMO_COMMAND_DISARM);
}

esp_err_t clock_demo_adapter_get_snapshot(
    clock_demo_adapter_t *adapter,
    clock_demo_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL || adapter->lock == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    *snapshot = adapter->snapshot;
    xSemaphoreGive(adapter->lock);
    return ESP_OK;
}

static esp_err_t _clock_demo_finish_close(clock_demo_adapter_t *adapter)
{
    while (!atomic_load_explicit(&adapter->worker_tail_complete,
                                 memory_order_acquire))
    {
        vTaskDelay(1U);
    }
    vTaskDeleteWithCaps(adapter->worker);
    vEventGroupDelete(adapter->events);
    vSemaphoreDelete(adapter->lock);
    memset(adapter, 0, sizeof(*adapter));
    return ESP_OK;
}

esp_err_t clock_demo_adapter_close(clock_demo_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_clock_demo_resources_present(adapter))
    {
        return ESP_OK;
    }
    if (adapter->worker == NULL || adapter->lock == NULL ||
            adapter->events == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupGetBits(adapter->events);
    if ((bits & CLOCK_DEMO_EVENT_WORKER_EXIT) != 0U)
    {
        return _clock_demo_finish_close(adapter);
    }

    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.closing = true;
    _clock_demo_revision_next(&adapter->snapshot);
    xSemaphoreGive(adapter->lock);
    xEventGroupClearBits(adapter->events, CLOCK_DEMO_EVENT_CLOSE_DONE);
    if (xTaskNotify(adapter->worker, CLOCK_DEMO_COMMAND_STOP,
                    eSetBits) != pdPASS)
    {
        return ESP_FAIL;
    }

    bits = xEventGroupWaitBits(
               adapter->events,
               CLOCK_DEMO_EVENT_CLOSE_DONE | CLOCK_DEMO_EVENT_WORKER_EXIT,
               pdFALSE, pdFALSE,
               pdMS_TO_TICKS(CLOCK_DEMO_CLOSE_TIMEOUT_MS));
    if ((bits & CLOCK_DEMO_EVENT_WORKER_EXIT) != 0U)
    {
        return _clock_demo_finish_close(adapter);
    }
    if ((bits & CLOCK_DEMO_EVENT_CLOSE_DONE) == 0U)
    {
        return ESP_ERR_TIMEOUT;
    }

    clock_demo_adapter_snapshot_t snapshot;
    esp_err_t result = clock_demo_adapter_get_snapshot(adapter, &snapshot);
    return result == ESP_OK ? snapshot.cleanup_result : result;
}

bool clock_demo_adapter_is_open(const clock_demo_adapter_t *adapter)
{
    return _clock_demo_resources_present(adapter);
}

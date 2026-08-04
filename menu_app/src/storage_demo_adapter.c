#define DBG_TAG "storage_demo"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "storage_demo_adapter.h"

#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_storage_service.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STORAGE_DEMO_MOUNT_PATH       "/sdcard"
#define STORAGE_DEMO_TEST_BYTES       4096U
#define STORAGE_DEMO_IO_CHUNK_BYTES   512U
#define STORAGE_DEMO_PATH_BYTES       64U
#define STORAGE_DEMO_CREATE_ATTEMPTS  16U
#define STORAGE_DEMO_COMMAND_DEPTH    4U
#define STORAGE_DEMO_WORKER_STACK     3072U
#define STORAGE_DEMO_WORKER_PRIORITY  (tskIDLE_PRIORITY + 2U)
#define STORAGE_DEMO_CLOSE_TIMEOUT_MS 1500U
#define STORAGE_DEMO_WORKER_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

typedef enum
{
    STORAGE_DEMO_COMMAND_REFRESH = 0,
    STORAGE_DEMO_COMMAND_SELF_TEST,
    STORAGE_DEMO_COMMAND_STOP,
} storage_demo_command_kind_t;

typedef struct storage_demo_command
{
    storage_demo_command_kind_t kind;
    uint32_t sequence;
} storage_demo_command_t;

typedef struct storage_demo_capacity
{
    uint64_t total_bytes;
    uint64_t free_bytes;
    esp_err_t error;
    bool mounted;
    bool valid;
} storage_demo_capacity_t;

typedef struct storage_demo_test_outcome
{
    storage_demo_capacity_t capacity;
    esp_err_t error;
    int filesystem_errno;
} storage_demo_test_outcome_t;

struct storage_demo_adapter
{
    QueueHandle_t command_queue;
    SemaphoreHandle_t lock;
    TaskHandle_t worker;
    storage_demo_snapshot_t snapshot;
    char owned_path[STORAGE_DEMO_PATH_BYTES];
    uint32_t next_sequence;
    uint32_t close_request_sequence;
    uint32_t close_ack_sequence;
    esp_err_t close_result;
    bool command_pending;
    bool close_pending;
    atomic_bool stop_requested;
    atomic_bool worker_done;
};

static void _storage_demo_bump_generation(storage_demo_adapter_t *adapter)
{
    ++adapter->snapshot.generation;
    if (adapter->snapshot.generation == 0U)
    {
        adapter->snapshot.generation = 1U;
    }
}

static uint32_t _storage_demo_next_sequence(storage_demo_adapter_t *adapter)
{
    ++adapter->next_sequence;
    if (adapter->next_sequence == 0U)
    {
        adapter->next_sequence = 1U;
    }
    return adapter->next_sequence;
}

static bool _storage_demo_mount_ready(void)
{
    if (!sd_storage_service_is_mounted())
    {
        return false;
    }

    const char *mount_path = sd_storage_service_get_mount_path();
    return mount_path != NULL &&
           strcmp(mount_path, STORAGE_DEMO_MOUNT_PATH) == 0;
}

static storage_demo_capacity_t _storage_demo_query_capacity(void)
{
    storage_demo_capacity_t capacity =
    {
        .error = ESP_ERR_INVALID_STATE,
    };
    if (!_storage_demo_mount_ready())
    {
        return capacity;
    }

    capacity.mounted = true;
    capacity.error = esp_vfs_fat_info(STORAGE_DEMO_MOUNT_PATH,
                                      &capacity.total_bytes,
                                      &capacity.free_bytes);
    capacity.valid = capacity.error == ESP_OK;
    return capacity;
}

static bool _storage_demo_cancel_requested(
    const storage_demo_adapter_t *adapter)
{
    return atomic_load_explicit(&adapter->stop_requested,
                                memory_order_acquire);
}

static void _storage_demo_fill_pattern(uint8_t *buffer, size_t length,
                                       size_t offset)
{
    for (size_t index = 0; index < length; ++index)
    {
        const size_t position = offset + index;
        buffer[index] = (uint8_t)((position * 33U + 0x5AU) & 0xFFU);
    }
}

static esp_err_t _storage_demo_cleanup_owned_file(
    storage_demo_adapter_t *adapter, int *filesystem_errno)
{
    if (adapter->owned_path[0] == '\0')
    {
        return ESP_OK;
    }

    if (unlink(adapter->owned_path) == 0 || errno == ENOENT)
    {
        memset(adapter->owned_path, 0, sizeof(adapter->owned_path));
        return ESP_OK;
    }

    if (filesystem_errno != NULL)
    {
        *filesystem_errno = errno;
    }
    return ESP_FAIL;
}

static esp_err_t _storage_demo_create_test_file(
    storage_demo_adapter_t *adapter, int *file_descriptor,
    int *filesystem_errno)
{
    for (size_t attempt = 0; attempt < STORAGE_DEMO_CREATE_ATTEMPTS; ++attempt)
    {
        const unsigned long nonce = (unsigned long)esp_random();
        int length = snprintf(adapter->owned_path,
                              sizeof(adapter->owned_path),
                              STORAGE_DEMO_MOUNT_PATH
                              "/.mt-storage-%08lx.tmp", nonce);
        if (length < 0 || (size_t)length >= sizeof(adapter->owned_path))
        {
            memset(adapter->owned_path, 0, sizeof(adapter->owned_path));
            return ESP_ERR_INVALID_SIZE;
        }

        int descriptor = open(adapter->owned_path,
                              O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0)
        {
            *file_descriptor = descriptor;
            return ESP_OK;
        }

        const int open_errno = errno;
        memset(adapter->owned_path, 0, sizeof(adapter->owned_path));
        if (open_errno != EEXIST)
        {
            *filesystem_errno = open_errno;
            return ESP_FAIL;
        }
    }

    *filesystem_errno = EEXIST;
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t _storage_demo_write_test_data(
    storage_demo_adapter_t *adapter, int file_descriptor,
    int *filesystem_errno)
{
    uint8_t buffer[STORAGE_DEMO_IO_CHUNK_BYTES];
    for (size_t offset = 0; offset < STORAGE_DEMO_TEST_BYTES;
            offset += sizeof(buffer))
    {
        if (_storage_demo_cancel_requested(adapter))
        {
            *filesystem_errno = ECANCELED;
            return ESP_ERR_INVALID_STATE;
        }
        _storage_demo_fill_pattern(buffer, sizeof(buffer), offset);

        size_t written = 0U;
        while (written < sizeof(buffer))
        {
            if (_storage_demo_cancel_requested(adapter))
            {
                *filesystem_errno = ECANCELED;
                return ESP_ERR_INVALID_STATE;
            }
            ssize_t count = write(file_descriptor, buffer + written,
                                  sizeof(buffer) - written);
            if (count > 0)
            {
                written += (size_t)count;
            }
            else if (count < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                *filesystem_errno = count < 0 ? errno : EIO;
                return ESP_FAIL;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t _storage_demo_read_test_data(
    storage_demo_adapter_t *adapter, int file_descriptor,
    int *filesystem_errno)
{
    uint8_t actual[STORAGE_DEMO_IO_CHUNK_BYTES];
    for (size_t offset = 0; offset < STORAGE_DEMO_TEST_BYTES;
            offset += sizeof(actual))
    {
        if (_storage_demo_cancel_requested(adapter))
        {
            *filesystem_errno = ECANCELED;
            return ESP_ERR_INVALID_STATE;
        }

        size_t received = 0U;
        while (received < sizeof(actual))
        {
            if (_storage_demo_cancel_requested(adapter))
            {
                *filesystem_errno = ECANCELED;
                return ESP_ERR_INVALID_STATE;
            }
            ssize_t count = read(file_descriptor, actual + received,
                                 sizeof(actual) - received);
            if (count > 0)
            {
                received += (size_t)count;
            }
            else if (count < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                *filesystem_errno = count < 0 ? errno : EIO;
                return ESP_FAIL;
            }
        }

        for (size_t index = 0; index < sizeof(actual); ++index)
        {
            const size_t position = offset + index;
            const uint8_t expected =
                (uint8_t)((position * 33U + 0x5AU) & 0xFFU);
            if (actual[index] != expected)
            {
                *filesystem_errno = EILSEQ;
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    uint8_t extra;
    ssize_t count;
    do
    {
        count = read(file_descriptor, &extra, sizeof(extra));
    }
    while (count < 0 && errno == EINTR);
    if (count != 0)
    {
        *filesystem_errno = count < 0 ? errno : EFBIG;
        return count < 0 ? ESP_FAIL : ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static storage_demo_test_outcome_t _storage_demo_run_test(
    storage_demo_adapter_t *adapter)
{
    storage_demo_test_outcome_t outcome =
    {
        .error = ESP_OK,
    };
    int file_descriptor = -1;

    outcome.capacity = _storage_demo_query_capacity();
    if (!outcome.capacity.mounted)
    {
        outcome.error = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (_storage_demo_cancel_requested(adapter))
    {
        outcome.error = ESP_ERR_INVALID_STATE;
        outcome.filesystem_errno = ECANCELED;
        goto exit;
    }

    outcome.error = _storage_demo_cleanup_owned_file(
                        adapter, &outcome.filesystem_errno);
    if (outcome.error != ESP_OK)
    {
        goto exit;
    }
    outcome.error = _storage_demo_create_test_file(
                        adapter, &file_descriptor,
                        &outcome.filesystem_errno);
    if (outcome.error != ESP_OK)
    {
        goto exit;
    }
    outcome.error = _storage_demo_write_test_data(
                        adapter, file_descriptor,
                        &outcome.filesystem_errno);
    if (outcome.error != ESP_OK)
    {
        goto exit;
    }
    if (_storage_demo_cancel_requested(adapter))
    {
        outcome.error = ESP_ERR_INVALID_STATE;
        outcome.filesystem_errno = ECANCELED;
        goto exit;
    }
    if (fsync(file_descriptor) != 0)
    {
        outcome.filesystem_errno = errno;
        outcome.error = ESP_FAIL;
        goto exit;
    }
    if (close(file_descriptor) != 0)
    {
        outcome.filesystem_errno = errno;
        outcome.error = ESP_FAIL;
        file_descriptor = -1;
        goto exit;
    }
    file_descriptor = -1;

    if (_storage_demo_cancel_requested(adapter))
    {
        outcome.error = ESP_ERR_INVALID_STATE;
        outcome.filesystem_errno = ECANCELED;
        goto exit;
    }
    file_descriptor = open(adapter->owned_path, O_RDONLY);
    if (file_descriptor < 0)
    {
        outcome.filesystem_errno = errno;
        outcome.error = ESP_FAIL;
        goto exit;
    }
    outcome.error = _storage_demo_read_test_data(
                        adapter, file_descriptor,
                        &outcome.filesystem_errno);

exit:
    if (file_descriptor >= 0 && close(file_descriptor) != 0 &&
            outcome.error == ESP_OK)
    {
        outcome.filesystem_errno = errno;
        outcome.error = ESP_FAIL;
    }

    int cleanup_errno = 0;
    esp_err_t cleanup_result = _storage_demo_cleanup_owned_file(
                                   adapter, &cleanup_errno);
    if (outcome.error == ESP_OK && cleanup_result != ESP_OK)
    {
        outcome.error = cleanup_result;
        outcome.filesystem_errno = cleanup_errno;
    }
    if (!_storage_demo_cancel_requested(adapter))
    {
        outcome.capacity = _storage_demo_query_capacity();
    }
    return outcome;
}

static void _storage_demo_publish_refresh(
    storage_demo_adapter_t *adapter,
    const storage_demo_capacity_t *capacity)
{
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.operation = STORAGE_DEMO_OPERATION_NONE;
    adapter->snapshot.ready = true;
    adapter->snapshot.mounted = capacity->mounted;
    adapter->snapshot.capacity_valid = capacity->valid;
    adapter->snapshot.total_bytes = capacity->total_bytes;
    adapter->snapshot.free_bytes = capacity->free_bytes;
    adapter->snapshot.last_error = capacity->error;
    adapter->snapshot.filesystem_errno = 0;
    adapter->command_pending = false;
    _storage_demo_bump_generation(adapter);
    xSemaphoreGive(adapter->lock);
}

static void _storage_demo_publish_test(
    storage_demo_adapter_t *adapter,
    const storage_demo_test_outcome_t *outcome)
{
    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->snapshot.operation = STORAGE_DEMO_OPERATION_NONE;
    adapter->snapshot.ready = true;
    adapter->snapshot.mounted = outcome->capacity.mounted;
    adapter->snapshot.capacity_valid = outcome->capacity.valid;
    adapter->snapshot.total_bytes = outcome->capacity.total_bytes;
    adapter->snapshot.free_bytes = outcome->capacity.free_bytes;
    adapter->snapshot.last_error = outcome->error;
    adapter->snapshot.filesystem_errno = outcome->filesystem_errno;
    adapter->snapshot.self_test = outcome->error == ESP_OK ?
                                  STORAGE_DEMO_SELF_TEST_PASSED :
                                  STORAGE_DEMO_SELF_TEST_FAILED;
    if (outcome->error == ESP_OK)
    {
        ++adapter->snapshot.self_test_count;
    }
    adapter->command_pending = false;
    _storage_demo_bump_generation(adapter);
    xSemaphoreGive(adapter->lock);
}

static esp_err_t _storage_demo_worker_stop(
    storage_demo_adapter_t *adapter,
    const storage_demo_command_t *command)
{
    int filesystem_errno = 0;
    esp_err_t result = _storage_demo_cleanup_owned_file(
                           adapter, &filesystem_errno);

    xSemaphoreTake(adapter->lock, portMAX_DELAY);
    adapter->close_ack_sequence = command->sequence;
    adapter->close_result = result;
    adapter->close_pending = false;
    adapter->command_pending = false;
    if (result != ESP_OK)
    {
        adapter->snapshot.operation = STORAGE_DEMO_OPERATION_NONE;
        adapter->snapshot.last_error = result;
        adapter->snapshot.filesystem_errno = filesystem_errno;
        adapter->snapshot.self_test = STORAGE_DEMO_SELF_TEST_FAILED;
        _storage_demo_bump_generation(adapter);
    }
    xSemaphoreGive(adapter->lock);

    if (result == ESP_OK)
    {
        atomic_store_explicit(&adapter->worker_done, true,
                              memory_order_release);
    }
    return result;
}

static void _storage_demo_worker(void *context)
{
    storage_demo_adapter_t *adapter = context;
    for (;;)
    {
        storage_demo_command_t command;
        if (xQueueReceive(adapter->command_queue, &command,
                          portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        if (command.kind == STORAGE_DEMO_COMMAND_STOP)
        {
            if (_storage_demo_worker_stop(adapter, &command) == ESP_OK)
            {
                break;
            }
            continue;
        }
        if (_storage_demo_cancel_requested(adapter))
        {
            xSemaphoreTake(adapter->lock, portMAX_DELAY);
            adapter->command_pending = false;
            adapter->snapshot.operation = STORAGE_DEMO_OPERATION_NONE;
            _storage_demo_bump_generation(adapter);
            xSemaphoreGive(adapter->lock);
            continue;
        }

        if (command.kind == STORAGE_DEMO_COMMAND_REFRESH)
        {
            const storage_demo_capacity_t capacity =
                _storage_demo_query_capacity();
            _storage_demo_publish_refresh(adapter, &capacity);
        }
        else if (command.kind == STORAGE_DEMO_COMMAND_SELF_TEST)
        {
            const storage_demo_test_outcome_t outcome =
                _storage_demo_run_test(adapter);
            _storage_demo_publish_test(adapter, &outcome);
        }
    }

    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static esp_err_t _storage_demo_queue_operation(
    storage_demo_adapter_t *adapter, storage_demo_command_kind_t kind)
{
    if (adapter == NULL || adapter->lock == NULL ||
            adapter->command_queue == NULL ||
            atomic_load_explicit(&adapter->stop_requested,
                                 memory_order_acquire) ||
            atomic_load_explicit(&adapter->worker_done,
                                 memory_order_acquire))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(adapter->lock, 0) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (adapter->command_pending)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    const storage_demo_snapshot_t previous = adapter->snapshot;
    adapter->command_pending = true;
    adapter->snapshot.operation = kind == STORAGE_DEMO_COMMAND_REFRESH ?
                                  STORAGE_DEMO_OPERATION_REFRESH :
                                  STORAGE_DEMO_OPERATION_SELF_TEST;
    if (kind == STORAGE_DEMO_COMMAND_SELF_TEST)
    {
        adapter->snapshot.self_test = STORAGE_DEMO_SELF_TEST_RUNNING;
    }
    adapter->snapshot.last_error = ESP_OK;
    adapter->snapshot.filesystem_errno = 0;
    _storage_demo_bump_generation(adapter);

    const storage_demo_command_t command =
    {
        .kind = kind,
        .sequence = _storage_demo_next_sequence(adapter),
    };
    if (xQueueSend(adapter->command_queue, &command, 0) != pdPASS)
    {
        adapter->snapshot = previous;
        adapter->command_pending = false;
        result = ESP_ERR_NO_MEM;
    }

exit:
    xSemaphoreGive(adapter->lock);
    return result;
}

static void _storage_demo_adapter_destroy(storage_demo_adapter_t **adapter)
{
    storage_demo_adapter_t *current = *adapter;
    if (current->worker != NULL)
    {
        vTaskDeleteWithCaps(current->worker);
        current->worker = NULL;
    }
    if (current->command_queue != NULL)
    {
        vQueueDelete(current->command_queue);
    }
    if (current->lock != NULL)
    {
        vSemaphoreDelete(current->lock);
    }
    memset(current, 0, sizeof(*current));
    free(current);
    *adapter = NULL;
}

esp_err_t storage_demo_adapter_open(storage_demo_adapter_t **adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (*adapter != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    storage_demo_adapter_t *current = calloc(1, sizeof(*current));
    if (current == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = ESP_ERR_NO_MEM;
    current->snapshot.self_test = STORAGE_DEMO_SELF_TEST_NOT_RUN;
    current->snapshot.accepting_commands = true;
    current->lock = xSemaphoreCreateMutex();
    if (current->lock == NULL)
    {
        goto cleanup;
    }
    current->command_queue = xQueueCreate(STORAGE_DEMO_COMMAND_DEPTH,
                                          sizeof(storage_demo_command_t));
    if (current->command_queue == NULL)
    {
        goto cleanup;
    }
    atomic_init(&current->stop_requested, false);
    atomic_init(&current->worker_done, false);
    if (xTaskCreatePinnedToCoreWithCaps(
                _storage_demo_worker, "storage_demo",
                STORAGE_DEMO_WORKER_STACK, current,
                STORAGE_DEMO_WORKER_PRIORITY, &current->worker,
                CONFIG_MAIN_PROJECT_TASK_CORE_ID,
                STORAGE_DEMO_WORKER_STACK_CAPS) != pdPASS)
    {
        goto cleanup;
    }
#if CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU0 || \
    CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU1
    LOG_I("task affinity name=storage_demo core=%d",
          (int)xTaskGetCoreID(current->worker));
#endif

    *adapter = current;
    result = storage_demo_adapter_refresh(current);
    if (result != ESP_OK)
    {
        esp_err_t cleanup_result = storage_demo_adapter_close(adapter);
        if (cleanup_result != ESP_OK)
        {
            LOG_W("initial refresh rollback failed: 0x%x", cleanup_result);
        }
    }
    return result;

cleanup:
    if (current->command_queue != NULL)
    {
        vQueueDelete(current->command_queue);
    }
    if (current->lock != NULL)
    {
        vSemaphoreDelete(current->lock);
    }
    free(current);
    return result;
}

esp_err_t storage_demo_adapter_refresh(storage_demo_adapter_t *adapter)
{
    return _storage_demo_queue_operation(adapter,
                                         STORAGE_DEMO_COMMAND_REFRESH);
}

esp_err_t storage_demo_adapter_run_self_test(
    storage_demo_adapter_t *adapter)
{
    return _storage_demo_queue_operation(adapter,
                                         STORAGE_DEMO_COMMAND_SELF_TEST);
}

esp_err_t storage_demo_adapter_get_snapshot(
    storage_demo_adapter_t *adapter,
    storage_demo_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL || adapter->lock == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(adapter->lock, 0) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    *snapshot = adapter->snapshot;
    xSemaphoreGive(adapter->lock);
    return ESP_OK;
}

esp_err_t storage_demo_adapter_close(storage_demo_adapter_t **adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    storage_demo_adapter_t *current = *adapter;
    if (current == NULL)
    {
        return ESP_OK;
    }
    if (atomic_load_explicit(&current->worker_done, memory_order_acquire))
    {
        _storage_demo_adapter_destroy(adapter);
        return ESP_OK;
    }

    atomic_store_explicit(&current->stop_requested, true,
                          memory_order_release);
    xSemaphoreTake(current->lock, portMAX_DELAY);
    current->snapshot.accepting_commands = false;
    _storage_demo_bump_generation(current);
    bool send_command = !current->close_pending;
    if (send_command)
    {
        current->close_request_sequence =
            _storage_demo_next_sequence(current);
        current->close_pending = true;
    }
    const uint32_t sequence = current->close_request_sequence;
    xSemaphoreGive(current->lock);

    if (send_command)
    {
        const storage_demo_command_t command =
        {
            .kind = STORAGE_DEMO_COMMAND_STOP,
            .sequence = sequence,
        };
        if (xQueueSendToFront(current->command_queue, &command, 0) != pdPASS)
        {
            xSemaphoreTake(current->lock, portMAX_DELAY);
            if (current->close_request_sequence == sequence)
            {
                current->close_pending = false;
            }
            xSemaphoreGive(current->lock);
            return ESP_ERR_NO_MEM;
        }
    }

    TickType_t timeout = pdMS_TO_TICKS(STORAGE_DEMO_CLOSE_TIMEOUT_MS);
    if (timeout == 0U)
    {
        timeout = 1U;
    }
    const TickType_t started_at = xTaskGetTickCount();
    for (;;)
    {
        if (atomic_load_explicit(&current->worker_done,
                                 memory_order_acquire))
        {
            _storage_demo_adapter_destroy(adapter);
            return ESP_OK;
        }

        xSemaphoreTake(current->lock, portMAX_DELAY);
        const bool acknowledged = current->close_ack_sequence == sequence;
        const esp_err_t close_result = current->close_result;
        xSemaphoreGive(current->lock);
        if (acknowledged && close_result != ESP_OK)
        {
            return close_result;
        }
        if ((TickType_t)(xTaskGetTickCount() - started_at) >= timeout)
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1U);
    }
}

/** @file Deterministic lifecycle and PCM-tail regressions for the real writer. */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_service.h"
#include "flybirds_audio.h"
#include "flybirds_freertos_fake.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef enum test_write_mode
{
    WRITE_OK,
    WRITE_TIMEOUT_ALWAYS,
    WRITE_TIMEOUT_PARTIAL,
    WRITE_LOST,
    WRITE_SCRIPT,
} test_write_mode_t;

typedef enum test_exit_gate
{
    EXIT_GATE_NONE,
    EXIT_GATE_BEFORE_GIVE,
    EXIT_GATE_AFTER_GIVE,
} test_exit_gate_t;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static audio_service_state_t s_state = AUDIO_SERVICE_STATE_READY;
static audio_service_lease_t s_owner;
static uint32_t s_generation;
static test_write_mode_t s_mode;
static unsigned s_starts;
static unsigned s_stops;
static unsigned s_writes;
static unsigned s_exits;
static unsigned s_release_failures;
static unsigned s_io_active;
static bool s_block_write;
static test_exit_gate_t s_gate;
static bool s_gate_blocked;
static bool s_gate_open;
static pthread_t s_old_thread;
static bool s_old_valid;
static bool s_old_deleted;
static bool s_fail_task;
static bool s_fail_semaphore;
static bool s_create_wait_exit;
static int16_t s_expected[FLYBIRDS_AUDIO_BLOCK_FRAMES * FLYBIRDS_SYNTH_CHANNELS];
static int16_t s_captured[FLYBIRDS_AUDIO_BLOCK_FRAMES * FLYBIRDS_SYNTH_CHANNELS];
static size_t s_capture_frames;
static unsigned s_script_calls;

BaseType_t __real_xSemaphoreGive(SemaphoreHandle_t semaphore);
SemaphoreHandle_t __real_xSemaphoreCreateBinary(void);
BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
        uint32_t stack, void *argument, UBaseType_t priority,
        TaskHandle_t *handle, BaseType_t core);
void __real_vTaskDelete(TaskHandle_t task);

static struct timespec _deadline(void)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    return deadline;
}

static void _wait_unsigned(const unsigned *value, unsigned minimum)
{
    const struct timespec deadline = _deadline();
    assert(pthread_mutex_lock(&s_lock) == 0);
    while (*value < minimum)
    {
        assert(pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == 0);
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
}

static void _wait_flag(const bool *value)
{
    const struct timespec deadline = _deadline();
    assert(pthread_mutex_lock(&s_lock) == 0);
    while (!*value)
    {
        assert(pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == 0);
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
}

static void _open_gate(void)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    s_gate_open = true;
    assert(pthread_cond_broadcast(&s_changed) == 0);
    assert(pthread_mutex_unlock(&s_lock) == 0);
}

BaseType_t __wrap_xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    const test_exit_gate_t gate = s_gate;
    s_gate = EXIT_GATE_NONE;
    if (gate != EXIT_GATE_NONE)
    {
        s_old_thread = pthread_self();
        s_old_valid = true;
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
    BaseType_t result = pdFALSE;
    if (gate != EXIT_GATE_BEFORE_GIVE)
    {
        result = __real_xSemaphoreGive(semaphore);
    }
    if (gate != EXIT_GATE_NONE)
    {
        assert(pthread_mutex_lock(&s_lock) == 0);
        s_gate_blocked = true;
        assert(pthread_cond_broadcast(&s_changed) == 0);
        while (!s_gate_open)
        {
            assert(pthread_cond_wait(&s_changed, &s_lock) == 0);
        }
        assert(pthread_mutex_unlock(&s_lock) == 0);
    }
    if (gate == EXIT_GATE_BEFORE_GIVE)
    {
        result = __real_xSemaphoreGive(semaphore);
    }
    /* A post-Give gate must never dereference the old semaphore again. */
    return result;
}

void __wrap_vTaskDelete(TaskHandle_t task)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    ++s_exits;
    if (s_old_valid && pthread_equal(pthread_self(), s_old_thread))
    {
        s_old_deleted = true;
    }
    assert(pthread_cond_broadcast(&s_changed) == 0);
    assert(pthread_mutex_unlock(&s_lock) == 0);
    __real_vTaskDelete(task);
}

SemaphoreHandle_t __wrap_xSemaphoreCreateBinary(void)
{
    return s_fail_semaphore ? NULL : __real_xSemaphoreCreateBinary();
}

BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
        uint32_t stack, void *argument, UBaseType_t priority,
        TaskHandle_t *handle, BaseType_t core)
{
    if (s_fail_task)
    {
        return pdFAIL;
    }
    const unsigned target = s_exits + 1U;
    const BaseType_t result = __real_xTaskCreatePinnedToCore(entry, name, stack,
        argument, priority, handle, core);
    if (s_create_wait_exit && result == pdPASS)
    {
        _wait_unsigned(&s_exits, target);
    }
    return result;
}

static bool _valid_owner(audio_service_lease_t lease)
{
    return s_owner.token != 0U && lease.token == s_owner.token &&
           lease.generation == s_owner.generation;
}

bool audio_service_is_available(void)
{
    return true;
}

audio_service_state_t audio_service_get_state(void)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    const audio_service_state_t state = s_state;
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return state;
}

esp_err_t audio_service_acquire(audio_service_session_kind_t kind,
                                audio_service_lease_t *lease)
{
    assert(lease != NULL && kind == AUDIO_SERVICE_SESSION_PLAYBACK);
    assert(pthread_mutex_lock(&s_lock) == 0);
    *lease = (audio_service_lease_t)
    {
        0
    };
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_owner.token == 0U && s_state == AUDIO_SERVICE_STATE_READY)
    {
        ++s_generation;
        if (s_generation == 0U)
        {
            ++s_generation;
        }
        s_owner = (audio_service_lease_t)
        {
            s_generation, s_generation
        };
        *lease = s_owner;
        s_state = AUDIO_SERVICE_STATE_RUNNING;
        ++s_starts;
        result = ESP_OK;
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return result;
}

esp_err_t audio_service_release(audio_service_lease_t lease)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (_valid_owner(lease))
    {
        assert(s_io_active == 0U);
        ++s_stops;
        if (s_release_failures != 0U)
        {
            --s_release_failures;
            s_state = AUDIO_SERVICE_STATE_ERROR;
            result = ESP_FAIL;
        }
        else
        {
            s_owner = (audio_service_lease_t)
            {
                0
            };
            s_state = AUDIO_SERVICE_STATE_READY;
            result = ESP_OK;
        }
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return result;
}

esp_err_t audio_service_write_leased(audio_service_lease_t lease, void *data,
                                     size_t bytes, size_t *written,
                                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (written != NULL)
    {
        *written = 0U;
    }
    assert(pthread_mutex_lock(&s_lock) == 0);
    if (!_valid_owner(lease) || s_state != AUDIO_SERVICE_STATE_RUNNING)
    {
        assert(pthread_mutex_unlock(&s_lock) == 0);
        return ESP_ERR_INVALID_STATE;
    }
    assert(data != NULL && bytes > 0U);
    ++s_writes;
    ++s_io_active;
    assert(pthread_cond_broadcast(&s_changed) == 0);
    while (s_block_write)
    {
        assert(pthread_cond_wait(&s_changed, &s_lock) == 0);
    }
    esp_err_t result = ESP_OK;
    size_t accepted = bytes;
    switch (s_mode)
    {
    case WRITE_TIMEOUT_ALWAYS:
        accepted = 0U;
        result = ESP_ERR_TIMEOUT;
        break;
    case WRITE_TIMEOUT_PARTIAL:
        accepted = (bytes / (sizeof(int16_t) * 2U) / 2U) * sizeof(int16_t) * 2U;
        result = ESP_ERR_TIMEOUT;
        break;
    case WRITE_LOST:
        accepted = 0U;
        result = ESP_ERR_INVALID_STATE;
        break;
    case WRITE_SCRIPT:
    {
        const size_t frame_bytes = sizeof(int16_t) * FLYBIRDS_SYNTH_CHANNELS;
        assert(bytes == (FLYBIRDS_AUDIO_BLOCK_FRAMES - s_capture_frames) * frame_bytes);
        assert(memcmp(data, s_expected + s_capture_frames * FLYBIRDS_SYNTH_CHANNELS, bytes) == 0);
        if (s_script_calls == 0U)
        {
            accepted = 80U * frame_bytes;
        }
        else if (s_script_calls == 1U)
        {
            accepted = 40U * frame_bytes;
            result = ESP_ERR_TIMEOUT;
        }
        else if (s_script_calls == 2U)
        {
            accepted = 50U * frame_bytes;
        }
        memcpy(s_captured + s_capture_frames * FLYBIRDS_SYNTH_CHANNELS, data, accepted);
        s_capture_frames += accepted / frame_bytes;
        ++s_script_calls;
        if (s_capture_frames == FLYBIRDS_AUDIO_BLOCK_FRAMES)
        {
            s_mode = WRITE_OK;
        }
        break;
    }
    default:
        break;
    }
    if (written != NULL)
    {
        *written = accepted;
    }
    --s_io_active;
    assert(pthread_cond_broadcast(&s_changed) == 0);
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return result;
}

static void _reset(void)
{
    assert(flybirds_fake_wait_idle(1000U));
    assert(pthread_mutex_lock(&s_lock) == 0);
    assert(s_owner.token == 0U && s_io_active == 0U);
    s_state = AUDIO_SERVICE_STATE_READY;
    s_mode = WRITE_OK;
    s_starts = 0U;
    s_stops = 0U;
    s_writes = 0U;
    s_exits = 0U;
    s_release_failures = 0U;
    s_block_write = false;
    s_gate = EXIT_GATE_NONE;
    s_gate_blocked = false;
    s_gate_open = false;
    s_old_valid = false;
    s_old_deleted = false;
    s_fail_task = false;
    s_fail_semaphore = false;
    s_create_wait_exit = false;
    s_capture_frames = 0U;
    s_script_calls = 0U;
    assert(pthread_mutex_unlock(&s_lock) == 0);
}

static void _close(void)
{
    assert(flybirds_audio_close() == ESP_OK);
    assert(flybirds_fake_wait_idle(1000U));
    assert(flybirds_fake_task_count() == 0U);
    assert(!flybirds_audio_is_open());
    assert(s_owner.token == 0U);
}

static void _test_transport_and_repeated_reopen(void)
{
    _reset();
    assert(flybirds_audio_close() == ESP_OK);
    assert(s_stops == 0U);
    for (unsigned cycle = 0U; cycle < 20U; ++cycle)
    {
        assert(flybirds_audio_open(true, true) == ESP_OK);
        _wait_unsigned(&s_writes, cycle + 1U);
        assert(flybirds_audio_is_open());
        _close();
    }
    assert(s_starts == 20U && s_stops == 20U);
}

static void _test_timeout_recovery(test_write_mode_t mode)
{
    _reset();
    s_mode = mode;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_unsigned(&s_writes, 16U);
    assert(flybirds_audio_is_open());
    assert(flybirds_audio_open(true, true) == ESP_OK);
    assert(s_starts == 1U);
    assert(pthread_mutex_lock(&s_lock) == 0);
    const unsigned next = s_writes + 1U;
    s_mode = WRITE_OK;
    assert(pthread_mutex_unlock(&s_lock) == 0);
    _wait_unsigned(&s_writes, next);
    _close();
}

static void _test_old_completion_cannot_clobber_new_writer(void)
{
    _reset();
    s_mode = WRITE_LOST;
    s_gate = EXIT_GATE_AFTER_GIVE;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_flag(&s_gate_blocked);
    assert(!flybirds_audio_is_open());
    assert(flybirds_audio_close() == ESP_OK);
    assert(pthread_mutex_lock(&s_lock) == 0);
    s_mode = WRITE_OK;
    const unsigned next = s_writes + 1U;
    assert(pthread_mutex_unlock(&s_lock) == 0);
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_unsigned(&s_writes, next);
    _open_gate();
    _wait_flag(&s_old_deleted);
    assert(flybirds_audio_is_open());
    _wait_unsigned(&s_writes, next + 1U);
    _close();
}

static void _test_completion_is_required_even_when_not_alive(void)
{
    _reset();
    s_mode = WRITE_LOST;
    s_gate = EXIT_GATE_BEFORE_GIVE;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_flag(&s_gate_blocked);
    assert(!flybirds_audio_is_open());
    const uint32_t generation = s_owner.generation;
    assert(flybirds_audio_close() == ESP_ERR_TIMEOUT);
    assert(s_stops == 0U && s_owner.generation == generation);
    assert(flybirds_audio_open(true, true) == ESP_ERR_TIMEOUT);
    assert(s_starts == 1U);
    _open_gate();
    _close();
}

static void _test_inflight_pcm_cannot_be_reused(void)
{
    _reset();
    s_block_write = true;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_unsigned(&s_io_active, 1U);
    assert(flybirds_audio_close() == ESP_ERR_TIMEOUT);
    assert(flybirds_audio_open(false, false) == ESP_ERR_TIMEOUT);
    assert(s_starts == 1U && s_stops == 0U);
    assert(pthread_mutex_lock(&s_lock) == 0);
    s_block_write = false;
    assert(pthread_cond_broadcast(&s_changed) == 0);
    assert(pthread_mutex_unlock(&s_lock) == 0);
    _close();
}

static void _test_retained_release_and_allocation_failures(void)
{
    for (unsigned failure = 0U; failure < 3U; ++failure)
    {
        _reset();
        s_release_failures = 2U;
        if (failure == 0U)
        {
            assert(flybirds_audio_open(true, true) == ESP_OK);
            _wait_unsigned(&s_writes, 1U);
            assert(flybirds_audio_close() == ESP_FAIL);
        }
        else
        {
            s_fail_semaphore = failure == 1U;
            s_fail_task = failure == 2U;
            assert(flybirds_audio_open(true, true) == ESP_ERR_NO_MEM);
        }
        assert(flybirds_fake_wait_idle(1000U));
        const uint32_t old = s_owner.generation;
        s_fail_task = false;
        s_fail_semaphore = false;
        assert(flybirds_audio_open(true, true) == ESP_FAIL);
        assert(s_owner.generation == old && s_starts == 1U);
        assert(flybirds_audio_open(true, true) == ESP_OK);
        assert(s_owner.generation != old && s_starts == 2U);
        _close();
    }
}

static void _test_worker_can_exit_before_create_returns(void)
{
    _reset();
    s_mode = WRITE_LOST;
    s_create_wait_exit = true;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    assert(!flybirds_audio_is_open());
    assert(flybirds_fake_wait_idle(1000U));
    s_create_wait_exit = false;
    s_mode = WRITE_OK;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    _wait_unsigned(&s_writes, 2U);
    assert(flybirds_audio_is_open() && s_starts == 2U);
    _close();
}

static void _test_partial_pcm_sequence(void)
{
    _reset();
    flybirds_synth_t synth;
    flybirds_synth_init(&synth);
    flybirds_synth_render(&synth, s_expected, FLYBIRDS_AUDIO_BLOCK_FRAMES);
    s_mode = WRITE_SCRIPT;
    assert(flybirds_audio_open(true, true) == ESP_OK);
    assert(pthread_mutex_lock(&s_lock) == 0);
    const struct timespec deadline = _deadline();
    while (s_capture_frames != FLYBIRDS_AUDIO_BLOCK_FRAMES)
    {
        assert(pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == 0);
    }
    assert(s_script_calls == 4U);
    assert(memcmp(s_captured, s_expected, sizeof(s_expected)) == 0);
    assert(pthread_mutex_unlock(&s_lock) == 0);
    _close();
}

int main(void)
{
    _test_transport_and_repeated_reopen();
    _test_timeout_recovery(WRITE_TIMEOUT_ALWAYS);
    _test_timeout_recovery(WRITE_TIMEOUT_PARTIAL);
    _test_old_completion_cannot_clobber_new_writer();
    _test_completion_is_required_even_when_not_alive();
    _test_inflight_pcm_cannot_be_reused();
    _test_retained_release_and_allocation_failures();
    _test_worker_can_exit_before_create_returns();
    _test_partial_pcm_sequence();
    puts("flybirds writer: generations isolated; completion/inflight barriers, retained release, PCM tails and 20 reopens passed");
    return 0;
}

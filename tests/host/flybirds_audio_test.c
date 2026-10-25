/**
 * @brief  Host test for the Flybirds PCM session ownership rules.
 * @note   Compiles flybirds_audio_session.c against a fake audio_service; the
 *         writer task is exercised by the simulator and cross-layer suites.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_service.h"
#include "flybirds_audio.h"

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static audio_service_lease_t s_owner;
static audio_service_session_kind_t s_kind;

static audio_service_state_t s_state = AUDIO_SERVICE_STATE_READY;
static bool s_available = true;
static esp_err_t s_stop_result = ESP_OK;
static unsigned s_start_count;
static unsigned s_lease_generation;
static unsigned s_stop_count;
static unsigned s_write_count;
static size_t s_last_write_bytes;
static bool s_partial_write;

static void _reset_fake(void)
{
    assert(s_owner.token == 0U);
    s_state = AUDIO_SERVICE_STATE_READY;
    s_available = true;
    s_stop_result = ESP_OK;
    s_start_count = 0U;
    s_stop_count = 0U;
    s_write_count = 0U;
    s_last_write_bytes = 0U;
    s_partial_write = false;
}

bool audio_service_is_available(void)
{
    return s_available;
}

audio_service_state_t audio_service_get_state(void)
{
    return s_state;
}

esp_err_t audio_service_acquire(audio_service_session_kind_t kind,
                                audio_service_lease_t *lease)
{
    if (lease == NULL || (unsigned)kind > AUDIO_SERVICE_SESSION_MAINTENANCE)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *lease = (audio_service_lease_t)
    {
        0
    };
    assert(pthread_mutex_lock(&s_lock) == 0);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_state == AUDIO_SERVICE_STATE_READY && s_owner.token == 0U)
    {
        ++s_start_count;
        if (++s_lease_generation == 0U)
        {
            ++s_lease_generation;
        }
        s_owner = (audio_service_lease_t)
        {
            s_lease_generation, s_lease_generation
        };
        s_kind = kind;
        *lease = s_owner;
        s_state = AUDIO_SERVICE_STATE_RUNNING;
        result = ESP_OK;
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return result;
}

esp_err_t audio_service_release(audio_service_lease_t lease)
{
    assert(pthread_mutex_lock(&s_lock) == 0);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_owner.token != 0U && lease.token == s_owner.token &&
            lease.generation == s_owner.generation)
    {
        ++s_stop_count;
        result = s_stop_result;
        if (result != ESP_OK)
        {
            s_state = AUDIO_SERVICE_STATE_ERROR;
        }
        else
        {
            s_owner = (audio_service_lease_t)
            {
                0
            };
            s_state = AUDIO_SERVICE_STATE_READY;
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
    if (data == NULL || bytes == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    assert(pthread_mutex_lock(&s_lock) == 0);
    if (s_state != AUDIO_SERVICE_STATE_RUNNING || s_owner.token == 0U ||
            lease.token != s_owner.token || lease.generation != s_owner.generation)
    {
        assert(pthread_mutex_unlock(&s_lock) == 0);
        return ESP_ERR_INVALID_STATE;
    }
    assert(s_kind == AUDIO_SERVICE_SESSION_PLAYBACK ||
           s_kind == AUDIO_SERVICE_SESSION_RECORD ||
           s_kind == AUDIO_SERVICE_SESSION_MAINTENANCE);
    ++s_write_count;
    s_last_write_bytes = bytes;
    if (s_partial_write)
    {
        /* Simulate a DMA timeout that accepted half the block. */
        if (written != NULL)
        {
            *written = bytes / 2U;
        }
        assert(pthread_mutex_unlock(&s_lock) == 0);
        return ESP_ERR_TIMEOUT;
    }
    if (written != NULL)
    {
        *written = bytes;
    }
    assert(pthread_mutex_unlock(&s_lock) == 0);
    return ESP_OK;
}

static void test_ready_stream_is_claimed_once(void)
{
    _reset_fake();
    assert(flybirds_audio_session_acquire());
    assert(flybirds_audio_session_is_owned());
    assert(s_start_count == 1U);
    assert(s_state == AUDIO_SERVICE_STATE_RUNNING);
    assert(flybirds_audio_session_acquire());
    assert(s_start_count == 1U);
    assert(flybirds_audio_session_release() == ESP_OK);
    assert(!flybirds_audio_session_is_owned());
    assert(s_stop_count == 1U);
    assert(flybirds_audio_session_release() == ESP_OK);
    assert(s_stop_count == 1U);
}

static void test_external_owner_is_left_alone(void)
{
    _reset_fake();
    audio_service_lease_t external;
    assert(audio_service_acquire(AUDIO_SERVICE_SESSION_RECORD, &external) == ESP_OK);
    assert(!flybirds_audio_session_acquire());
    assert(!flybirds_audio_session_is_owned());
    assert(s_start_count == 1U);
    assert(flybirds_audio_session_release() == ESP_OK);
    assert(s_stop_count == 0U);
    assert(s_state == AUDIO_SERVICE_STATE_RUNNING);
    assert(audio_service_release(external) == ESP_OK);
}

static void test_busy_stream_recovers_after_release(void)
{
    _reset_fake();
    audio_service_lease_t external;
    assert(audio_service_acquire(AUDIO_SERVICE_SESSION_RECORD, &external) == ESP_OK);
    assert(!flybirds_audio_session_acquire());
    assert(!flybirds_audio_session_is_owned());
    assert(s_start_count == 1U);

    /* The external owner released; the next attempt may take the stream. */
    assert(audio_service_release(external) == ESP_OK);
    assert(flybirds_audio_session_acquire());
    assert(s_start_count == 2U);
    assert(s_state == AUDIO_SERVICE_STATE_RUNNING);
    assert(flybirds_audio_session_release() == ESP_OK);
    assert(s_stop_count == 2U);
}

static void test_unavailable_and_transitional_states_are_refused(void)
{
    _reset_fake();
    s_available = false;
    assert(!flybirds_audio_session_acquire());
    assert(s_start_count == 0U);

    _reset_fake();
    s_state = AUDIO_SERVICE_STATE_SUSPENDING;
    assert(!flybirds_audio_session_acquire());
    assert(s_start_count == 0U);

    _reset_fake();
    s_state = AUDIO_SERVICE_STATE_ERROR;
    assert(!flybirds_audio_session_acquire());
    assert(s_start_count == 0U);

    _reset_fake();
    s_state = AUDIO_SERVICE_STATE_UNINITIALIZED;
    assert(!flybirds_audio_session_acquire());
    assert(s_start_count == 0U);
}

static void test_write_requires_ownership(void)
{
    _reset_fake();
    int16_t frame[FLYBIRDS_SYNTH_CHANNELS] = {0};
    size_t written = 99U;
    assert(flybirds_audio_session_write(frame, 1U, &written) ==
           ESP_ERR_INVALID_STATE);
    assert(written == 0U);
    assert(s_write_count == 0U);

    assert(flybirds_audio_session_acquire());
    assert(flybirds_audio_session_write(frame, 1U, &written) == ESP_OK);
    assert(written == 1U);
    assert(s_write_count == 1U);
    assert(s_last_write_bytes == sizeof(frame));
    assert(flybirds_audio_session_write(NULL, 1U, &written) ==
           ESP_ERR_INVALID_STATE);
    assert(flybirds_audio_session_write(frame, 0U, &written) ==
           ESP_ERR_INVALID_STATE);
    assert(flybirds_audio_session_write(frame, 1U, NULL) ==
           ESP_ERR_INVALID_STATE);
    assert(flybirds_audio_session_release() == ESP_OK);
}

static void test_partial_write_reports_progress(void)
{
    _reset_fake();
    int16_t frames[4U * FLYBIRDS_SYNTH_CHANNELS] = {0};
    assert(flybirds_audio_session_acquire());
    s_partial_write = true;
    size_t written = 0U;
    assert(flybirds_audio_session_write(frames, 4U, &written) ==
           ESP_ERR_TIMEOUT);
    assert(written == 2U);
    s_partial_write = false;
    assert(flybirds_audio_session_write(
               frames + written * FLYBIRDS_SYNTH_CHANNELS,
               4U - written, &written) == ESP_OK);
    assert(written == 2U);
    assert(flybirds_audio_session_release() == ESP_OK);
}

static void test_failed_stop_keeps_ownership_for_retry(void)
{
    _reset_fake();
    assert(flybirds_audio_session_acquire());
    s_stop_result = ESP_FAIL;
    assert(flybirds_audio_session_release() == ESP_FAIL);
    assert(flybirds_audio_session_is_owned());
    assert(s_stop_count == 1U);
    /* An ERROR service refuses a second start, so ownership must persist. */
    assert(flybirds_audio_session_acquire());
    assert(s_start_count == 1U);

    s_stop_result = ESP_OK;
    assert(flybirds_audio_session_release() == ESP_OK);
    assert(!flybirds_audio_session_is_owned());
    assert(s_stop_count == 2U);
}

int main(void)
{
    test_ready_stream_is_claimed_once();
    test_external_owner_is_left_alone();
    test_busy_stream_recovers_after_release();
    test_unavailable_and_transitional_states_are_refused();
    test_write_requires_ownership();
    test_partial_write_reports_progress();
    test_failed_stop_keeps_ownership_for_retry();
    printf("flybirds_audio_test: ok\n");
    return 0;
}

#include "flybirds_audio.h"

#include "audio_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static audio_service_lease_t s_lease;
static bool s_owned;

bool flybirds_audio_session_acquire(void)
{
    if (s_owned)
    {
        return true;
    }
    if (!audio_service_is_available())
    {
        return false;
    }
    audio_service_lease_t lease = {0};
    if (audio_service_acquire(AUDIO_SERVICE_SESSION_PLAYBACK, &lease) !=
            ESP_OK)
    {
        return false;
    }
    s_lease = lease;
    s_owned = true;
    return true;
}

esp_err_t flybirds_audio_session_release(void)
{
    if (!s_owned)
    {
        return ESP_OK;
    }
    const esp_err_t result = audio_service_release(s_lease);
    if (result == ESP_OK)
    {
        s_owned = false;
        memset(&s_lease, 0, sizeof(s_lease));
    }
    return result;
}

bool flybirds_audio_session_is_owned(void)
{
    return s_owned;
}

esp_err_t flybirds_audio_session_write(const int16_t *interleaved,
                                       size_t frames,
                                       size_t *written_frames)
{
    if (written_frames != NULL)
    {
        *written_frames = 0U;
    }
    if (!s_owned || interleaved == NULL || frames == 0U ||
            written_frames == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t frame_bytes = FLYBIRDS_SYNTH_CHANNELS *
                               sizeof(*interleaved);
    const size_t bytes = frames * frame_bytes;
    size_t written_bytes = 0U;
    const esp_err_t result = audio_service_write_leased(s_lease,
        (void *)interleaved, bytes,
        &written_bytes,
        FLYBIRDS_AUDIO_WRITE_TIMEOUT_MS);
    if (written_bytes > bytes)
    {
        written_bytes = bytes;
    }
    *written_frames = written_bytes / frame_bytes;
    return result;
}

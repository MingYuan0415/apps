#ifndef __MUSIC_LYRICS_VIEW_H__
#define __MUSIC_LYRICS_VIEW_H__

#include "music_service.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Page-owned lyric lease and request state shared by two pages. */
typedef struct music_lyrics_view
{
    music_lyric_snapshot_t *snapshot;
    char songmid[MUSIC_SERVICE_SONGMID_BYTES];
    uint16_t wait_ticks;
    bool loading;
    bool failed;
} music_lyrics_view_t;

/** @brief Drop the current snapshot and forget the active song. */
void music_lyrics_view_reset(music_lyrics_view_t *view);

/** @brief Release the current snapshot but keep the active song identity. */
void music_lyrics_view_release(music_lyrics_view_t *view);

/** @brief Clear a terminal failure so the next poll requests lyrics again. */
void music_lyrics_view_retry(music_lyrics_view_t *view);

/**
 * @brief Poll the service for the current track's lyric snapshot.
 *
 * Returns true when a new snapshot was acquired. While the service request is
 * pending the view re-checks each tick; a completed empty result sets failed
 * until the song changes or the caller retries.
 */
bool music_lyrics_view_poll(music_lyrics_view_t *view,
                            const music_player_status_t *status);

#endif /* __MUSIC_LYRICS_VIEW_H__ */

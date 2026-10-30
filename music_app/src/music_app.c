#include "music_app_internal.h"

static const app_manager_page_route_t s_music_routes[] =
{
    {
        .page_id = MUSIC_PAGE_ROOT,
        .definition = &music_root_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MUSIC_PAGE_PLAYLIST,
        .definition = &music_playlist_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MUSIC_PAGE_PLAYER,
        .definition = &music_player_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MUSIC_PAGE_LYRICS,
        .definition = &music_lyrics_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MUSIC_PAGE_LOGIN,
        .definition = &music_login_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(music, APP_IMAGE_MUSIC_ICON, "音乐",
                            APP_MANAGER_ID_MUSIC, MUSIC_PAGE_ROOT,
                            APP_MANAGER_APP_FLAG_NONE, s_music_routes, 18U,
                            "QQ 音乐");
